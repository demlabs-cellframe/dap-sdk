/**
 * @file vless_vision.c
 * @brief XTLS Vision flow (xtls-rprx-vision) padding codec -- client side.
 *
 * Mirrors Xray proxy/proxy.go v26.10.31:
 *   writer: [uuid 16 (first frame only)][command][content_len 2B][padding_len 2B]
 *           [content][padding]; command 0=continue, 1=end, 2=direct
 *   reader: stateful unpadding; command 2 switches to raw passthrough
 * Client policy: we never send 0x02 (no inner-TLS splicing heuristics),
 * padding ends after filter window exactly like the non-TLS path upstream.
 */

#include "reality_internal.h"

#define CMD_CONTINUE 0x00
#define CMD_END      0x01
#define CMD_DIRECT   0x02

/* default testseed {900, 500, 900, 256} */
#define SEED_LONG_MIN   900
#define SEED_LONG_RAND  500
#define SEED_SHORT_RAND 256
#define VISION_MAX_PAD  900u

void vision_writer_init(vision_writer_t *a_w, const uint8_t a_uuid[16])
{
    memcpy(a_w->uuid, a_uuid, 16);
    a_w->uuid_pending = true;
    a_w->padding = true;
    a_w->packets_to_filter = 8;
    a_w->inner_tls_seen = false;
}

void vision_reader_init(vision_reader_t *a_r, const uint8_t a_uuid[16])
{
    memcpy(a_r->uuid, a_uuid, 16);
    a_r->uuid_checked = false;
    a_r->remaining_command = -1;
    a_r->remaining_content = -1;
    a_r->remaining_padding = -1;
    a_r->current_command = 0;
    a_r->direct = false;
}

static uint32_t prng_next(reality_session_t *a_s, uint32_t a_mod)
{
    /* xorshift over crypto rand seed; not security-critical (padding length only) */
    static uint32_t s_state; static bool s_init;
    if (!s_init) {
        uint8_t seed[4];
        if (a_s && a_s->crypto && a_s->crypto->rand_bytes)
            a_s->crypto->rand_bytes(a_s->crypto->user, seed, 4);
        else { seed[0]=1; seed[1]=2; seed[2]=3; seed[3]=4; }
        s_state = ((uint32_t)seed[0] << 24) | ((uint32_t)seed[1] << 16) |
                  ((uint32_t)seed[2] << 8) | seed[3];
        if (!s_state) s_state = 0x9E3779B9;
        s_init = true;
    }
    s_state ^= s_state << 13; s_state ^= s_state >> 17; s_state ^= s_state << 5;
    return s_state % (a_mod ? a_mod : 1);
}

size_t vision_write(vision_writer_t *a_w, const uint8_t *a_src, size_t a_len,
                    uint8_t *a_dst, size_t a_dst_cap)
{
    if (!a_w || !a_dst) return 0;

    if (!a_w->padding) {
        if (a_len > a_dst_cap) return 0;
        memcpy(a_dst, a_src, a_len);
        return a_len;
    }

    /* wrap single frame; content chunked by caller policy (we keep <=2KB chunks) */
    size_t content = a_len;
    uint32_t padding_len;
    if (content < SEED_LONG_MIN) {
        /* long padding to hide the VLESS header phase */
        padding_len = prng_next(NULL, SEED_LONG_RAND) + SEED_LONG_MIN - (uint32_t)content;
    } else {
        padding_len = prng_next(NULL, SEED_SHORT_RAND);
    }
    if (padding_len > VISION_MAX_PAD) padding_len = VISION_MAX_PAD;

    size_t uuid_len = a_w->uuid_pending ? 16 : 0;
    size_t need = uuid_len + 5 + content + padding_len;
    if (need > a_dst_cap) {
        padding_len = 0;
        need = uuid_len + 5 + content;
        if (need > a_dst_cap) return 0;
    }

    uint8_t *p = a_dst;
    if (uuid_len) { memcpy(p, a_w->uuid, 16); p += 16; a_w->uuid_pending = false; }

    a_w->packets_to_filter--;
    uint8_t cmd = CMD_CONTINUE;
    if (a_w->packets_to_filter <= 1) { cmd = CMD_END; a_w->padding = false; }

    *p++ = cmd;
    *p++ = (uint8_t)(content >> 8); *p++ = (uint8_t)content;
    *p++ = (uint8_t)(padding_len >> 8); *p++ = (uint8_t)padding_len;
    if (content) { memcpy(p, a_src, content); p += content; }
    if (padding_len) {
        memset(p, 0, padding_len); /* zeros; upstream uses uninitialized-safe zeros too */
        p += padding_len;
    }
    return (size_t)(p - a_dst);
}

int64_t vision_read(vision_reader_t *a_r, const uint8_t *a_src, size_t a_len,
                    uint8_t *a_dst, size_t a_dst_cap, size_t *a_consumed)
{
    if (!a_r || !a_src || !a_consumed) return -1;
    *a_consumed = 0;
    if (a_len == 0) return 0;

    if (a_r->direct) {
        size_t n = a_len < a_dst_cap ? a_len : a_dst_cap;
        if (n < a_len) return -1; /* caller must give cap >= len */
        memcpy(a_dst, a_src, n);
        *a_consumed = a_len;
        return (int64_t)n;
    }

    /* initial state: expect [uuid16] prefix on the very first server frame */
    if (a_r->remaining_command == -1 && a_r->remaining_content == -1 &&
        a_r->remaining_padding == -1) {
        if (a_len >= 21 && memcmp(a_r->uuid, a_src, 16) == 0) {
            a_src += 16; a_len -= 16; *a_consumed += 16;
            a_r->remaining_command = 5;
        } else {
            /* upstream passes raw through when no uuid prefix (padding never
             * started on this direction) */
            size_t n = a_len < a_dst_cap ? a_len : a_dst_cap;
            if (n < a_len) return -1;
            memcpy(a_dst, a_src, n);
            *a_consumed = a_len;
            return (int64_t)n;
        }
    }

    size_t dst_len = 0;
    size_t i = 0;
    while (i < a_len) {
        if (a_r->remaining_command > 0) {
            uint8_t data = a_src[i++]; (*a_consumed)++;
            switch (a_r->remaining_command) {
            case 5: a_r->current_command = data; break;
            case 4: a_r->remaining_content = (int32_t)data << 8; break;
            case 3: a_r->remaining_content |= (int32_t)data; break;
            case 2: a_r->remaining_padding = (int32_t)data << 8; break;
            case 1:
                a_r->remaining_padding |= (int32_t)data;
                break;
            }
            a_r->remaining_command--;
        } else if (a_r->remaining_content > 0) {
            size_t take = (size_t)a_r->remaining_content;
            if (take > a_len - i) take = a_len - i;
            if (dst_len + take > a_dst_cap) return -1;
            memcpy(a_dst + dst_len, a_src + i, take);
            dst_len += take; i += take; (*a_consumed) += take;
            a_r->remaining_content -= (int32_t)take;
        } else if (a_r->remaining_padding > 0) {
            size_t skip = (size_t)a_r->remaining_padding;
            if (skip > a_len - i) skip = a_len - i;
            i += skip; (*a_consumed) += skip;
            a_r->remaining_padding -= (int32_t)skip;
        } else {
            /* block done */
            if (a_r->current_command == CMD_CONTINUE) {
                a_r->remaining_command = 5;
            } else if (a_r->current_command == CMD_DIRECT) {
                a_r->direct = true;
                /* rest is raw payload */
                size_t rest = a_len - i;
                if (dst_len + rest > a_dst_cap) return -1;
                memcpy(a_dst + dst_len, a_src + i, rest);
                dst_len += rest; *a_consumed += rest; i = a_len;
                return (int64_t)dst_len;
            } else { /* CMD_END */
                a_r->remaining_command = -1; a_r->remaining_content = -1;
                a_r->remaining_padding = -1;
                /* rest is raw payload (padding phase over) */
                size_t rest = a_len - i;
                if (dst_len + rest > a_dst_cap) return -1;
                memcpy(a_dst + dst_len, a_src + i, rest);
                dst_len += rest; *a_consumed += rest; i = a_len;
                return (int64_t)dst_len;
            }
        }
    }
    return (int64_t)dst_len;
}

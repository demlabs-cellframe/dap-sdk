/* Private, owner-thread DNS message assembly. No transport or VPN policy. */
#ifndef DAP_DNS_REASSEMBLY_H
#define DAP_DNS_REASSEMBLY_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DNS_REASM_SLOTS 16U
#define DNS_REASM_BYTES 16384U
typedef struct {
    uint32_t id, bitmap;
    uint8_t type;
    uint16_t count, lengths[32];
    size_t bytes;
    uint8_t *parts[32];
} dns_reasm_slot_t;
typedef struct dns_reasm {
    dns_reasm_slot_t slots[DNS_REASM_SLOTS];
    uint32_t completed[64];
    uint8_t completed_type[64];
    unsigned completed_count, completed_next;
} dns_reasm_t;

static void s_reasm_slot_clear(dns_reasm_slot_t *s)
{
    for (unsigned i = 0; i < 32; ++i) free(s->parts[i]);
    memset(s, 0, sizeof(*s));
}
static void s_reasm_reset(dns_reasm_t *s)
{
    if (!s) return;
    for (unsigned i = 0; i < DNS_REASM_SLOTS; ++i) s_reasm_slot_clear(&s->slots[i]);
    memset(s, 0, sizeof(*s));
}

/* -1: rejected (must not ACK), 0: retained/duplicate, 1: complete owned output.
 * Never evict acknowledged partial data to make room for another message. */
static int s_reasm_add(dns_reasm_t *r, const dap_dns_tunnel_frame_t *f,
                      uint8_t **out, size_t *out_size)
{
    *out = NULL; *out_size = 0;
    if (!f->fragment_count || f->fragment_count > 32 ||
        f->fragment_index >= f->fragment_count ||
        (f->payload_length && !f->payload)) return -1;
    for (unsigned i = 0; i < r->completed_count; ++i)
        if (r->completed[i] == f->message_id && r->completed_type[i] == f->type) return 0;
    dns_reasm_slot_t *s = NULL, *empty = NULL;
    for (unsigned i = 0; i < DNS_REASM_SLOTS; ++i) {
        dns_reasm_slot_t *p = &r->slots[i];
        if (!p->count) { if (!empty) empty = p; }
        else if (p->id == f->message_id && p->type == f->type) { s = p; break; }
    }
    if (!s) {
        if (!empty) return -1;
        s = empty; s->id = f->message_id; s->type = f->type; s->count = f->fragment_count;
    }
    if (s->count != f->fragment_count) return -1;
    uint32_t bit = UINT32_C(1) << f->fragment_index;
    if (!(s->bitmap & bit)) {
        if (f->payload_length > DNS_REASM_BYTES - s->bytes) return -1;
        uint8_t *p = f->payload_length ? malloc(f->payload_length) : NULL;
        if (f->payload_length && !p) return -1;
        if (p) memcpy(p, f->payload, f->payload_length);
        s->parts[f->fragment_index] = p;
        s->lengths[f->fragment_index] = f->payload_length;
        s->bytes += f->payload_length; s->bitmap |= bit;
    }
    uint32_t full = s->count == 32 ? UINT32_MAX : (UINT32_C(1) << s->count) - 1;
    if (s->bitmap != full) return 0;
    uint8_t *p = malloc(s->bytes ? s->bytes : 1);
    if (!p) return -1;
    size_t offset = 0;
    for (unsigned i = 0; i < s->count; ++i) {
        if (s->lengths[i]) memcpy(p + offset, s->parts[i], s->lengths[i]);
        offset += s->lengths[i];
    }
    unsigned done = r->completed_next;
    r->completed[done] = s->id; r->completed_type[done] = s->type;
    r->completed_next = (done + 1) % 64;
    if (r->completed_count < 64) ++r->completed_count;
    s_reasm_slot_clear(s);
    *out = p; *out_size = offset;
    return 1;
}
#endif

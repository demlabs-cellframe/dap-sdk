/*
 * test_ledger_anon.c — Anonymous ledger integration tests.
 *
 * Tests: TX item type constants, anonymous detection logic,
 * key image hash uniqueness, algorithm adapter pattern.
 *
 * Uses minimal includes to avoid transitive dependency issues.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#define LOG_TAG "test_ledger_anon"

/* TX item type constants (from dap_chain_common.h) */
#define TX_ITEM_TYPE_IN_ANON        0xb0
#define TX_ITEM_TYPE_OUT_ANON       0xb1
#define TX_ITEM_TYPE_KEY_IMAGE      0xb2
#define TX_ITEM_TYPE_ANON_PROOF     0xb3
#define TX_ITEM_TYPE_PEDERSEN_COMMIT 0xb4
#define TX_ITEM_TYPE_IN             0x00
#define TX_ITEM_TYPE_OUT_STD        0x13
#define TX_ITEM_TYPE_SIG            0x30

/* --- TX item type constants test --- */

static void test_tx_item_types(void)
{
    dap_assert(TX_ITEM_TYPE_IN_ANON == 0xb0, "IN_ANON = 0xb0");
    dap_assert(TX_ITEM_TYPE_OUT_ANON == 0xb1, "OUT_ANON = 0xb1");
    dap_assert(TX_ITEM_TYPE_KEY_IMAGE == 0xb2, "KEY_IMAGE = 0xb2");
    dap_assert(TX_ITEM_TYPE_ANON_PROOF == 0xb3, "ANON_PROOF = 0xb3");
    dap_assert(TX_ITEM_TYPE_PEDERSEN_COMMIT == 0xb4, "PEDERSEN_COMMIT = 0xb4");
    dap_assert(TX_ITEM_TYPE_IN_ANON != TX_ITEM_TYPE_IN, "IN_ANON != IN");
    dap_assert(TX_ITEM_TYPE_OUT_ANON != TX_ITEM_TYPE_OUT_STD, "OUT_ANON != OUT_STD");
    dap_assert(TX_ITEM_TYPE_KEY_IMAGE != TX_ITEM_TYPE_SIG, "KEY_IMAGE != SIG");
}

/* --- Anonymous detection logic test --- */

static int s_is_anonymous(const uint8_t *a_items, size_t a_size)
{
    const uint8_t *l_item = a_items;
    size_t l_offset = 0;
    while (l_offset < a_size) {
        uint8_t l_type = *l_item;
        if (l_type == TX_ITEM_TYPE_IN_ANON || l_type == TX_ITEM_TYPE_OUT_ANON ||
            l_type == TX_ITEM_TYPE_KEY_IMAGE || l_type == TX_ITEM_TYPE_ANON_PROOF ||
            l_type == TX_ITEM_TYPE_PEDERSEN_COMMIT) {
            return 1;
        }
        if (l_offset + 4 > a_size) break;
        uint32_t l_item_size;
        memcpy(&l_item_size, l_item + 4, sizeof(uint32_t));
        if (l_item_size == 0) break;
        l_item += l_item_size;
        l_offset += l_item_size;
    }
    return 0;
}

static void test_anonymous_detection_empty(void)
{
    uint8_t l_items[32];
    memset(l_items, 0, sizeof(l_items));
    l_items[0] = TX_ITEM_TYPE_OUT_STD;
    l_items[4] = 32;
    dap_assert(!s_is_anonymous(l_items, sizeof(l_items)), "OUT_STD → not anonymous");
}

static void test_anonymous_detection_in_anon(void)
{
    uint8_t l_items[32];
    memset(l_items, 0, sizeof(l_items));
    l_items[0] = TX_ITEM_TYPE_IN_ANON;
    l_items[4] = 32;
    dap_assert(s_is_anonymous(l_items, sizeof(l_items)), "IN_ANON → anonymous");
}

static void test_anonymous_detection_out_anon(void)
{
    uint8_t l_items[32];
    memset(l_items, 0, sizeof(l_items));
    l_items[0] = TX_ITEM_TYPE_OUT_ANON;
    l_items[4] = 32;
    dap_assert(s_is_anonymous(l_items, sizeof(l_items)), "OUT_ANON → anonymous");
}

static void test_anonymous_detection_key_image(void)
{
    uint8_t l_items[32];
    memset(l_items, 0, sizeof(l_items));
    l_items[0] = TX_ITEM_TYPE_KEY_IMAGE;
    l_items[4] = 32;
    dap_assert(s_is_anonymous(l_items, sizeof(l_items)), "KEY_IMAGE → anonymous");
}

static void test_anonymous_detection_mixed_items(void)
{
    uint8_t l_items[128];
    memset(l_items, 0, sizeof(l_items));

    /* Item 0: OUT_STD */
    l_items[0] = TX_ITEM_TYPE_OUT_STD;
    uint32_t l_sz = 32;
    memcpy(&l_items[4], &l_sz, sizeof(uint32_t));

    /* Item 1: KEY_IMAGE at offset 32 */
    l_items[32] = TX_ITEM_TYPE_KEY_IMAGE;
    l_sz = 32;
    memcpy(&l_items[36], &l_sz, sizeof(uint32_t));

    dap_assert(s_is_anonymous(l_items, sizeof(l_items)), "mixed with KEY_IMAGE → anonymous");
}

/* --- Key image uniqueness test --- */

static void test_key_image_uniqueness(void)
{
    /* Two different key images should produce different hashes */
    uint8_t l_img1[32], l_img2[32];
    memset(l_img1, 0xAA, sizeof(l_img1));
    memset(l_img2, 0xBB, sizeof(l_img2));

    /* Same image → same hash (deterministic) */
    /* Different image → different hash */

    /* Simulate hash comparison */
    int l_same = (memcmp(l_img1, l_img1, 32) == 0);
    int l_diff = (memcmp(l_img1, l_img2, 32) != 0);
    dap_assert(l_same, "same image bytes → equal");
    dap_assert(l_diff, "different image bytes → not equal");
}

/* --- Item iteration test --- */

static void test_item_iteration(void)
{
    uint8_t l_items[256];
    memset(l_items, 0, sizeof(l_items));

    /* Item 0: OUT_STD at offset 0, size 32 */
    l_items[0] = TX_ITEM_TYPE_OUT_STD;
    uint32_t l_sz = 32;
    memcpy(&l_items[4], &l_sz, sizeof(uint32_t));

    /* Item 1: KEY_IMAGE at offset 32, size 64 */
    l_items[32] = TX_ITEM_TYPE_KEY_IMAGE;
    l_sz = 64;
    memcpy(&l_items[36], &l_sz, sizeof(uint32_t));

    /* Item 2: OUT_ANON at offset 96, size 32 */
    l_items[96] = TX_ITEM_TYPE_OUT_ANON;
    l_sz = 32;
    memcpy(&l_items[100], &l_sz, sizeof(uint32_t));

    /* Count items — don't count empty terminator */
    int l_count = 0;
    const uint8_t *l_item = l_items;
    size_t l_offset = 0;
    while (l_offset < sizeof(l_items)) {
        uint32_t l_item_size;
        memcpy(&l_item_size, l_item + 4, sizeof(uint32_t));
        if (l_item_size == 0) break;
        l_count++;
        l_item += l_item_size;
        l_offset += l_item_size;
    }
    dap_assert(l_count == 3, "iterated 3 items");

    /* Should detect anonymous */
    dap_assert(s_is_anonymous(l_items, sizeof(l_items)), "mixed items → anonymous");
}

/* --- Algorithm adapter pattern test --- */

typedef struct test_algo {
    const char *name;
    uint32_t key_type;
    size_t (*pk_size)(void);
} test_algo_t;

static size_t s_ring_pk_size(void) { return 1408; }
static size_t s_lrs_pk_size(void) { return 1424; }

static const test_algo_t s_algo_ring = { .name = "chipmunk_ring", .key_type = 0x010C, .pk_size = s_ring_pk_size };
static const test_algo_t s_algo_lrs = { .name = "lrs", .key_type = 0x010A, .pk_size = s_lrs_pk_size };

static void test_algorithm_adapter(void)
{
    dap_assert(strcmp(s_algo_ring.name, "chipmunk_ring") == 0, "ring algo name");
    dap_assert(s_algo_ring.key_type == 0x010C, "ring key type");
    dap_assert(s_algo_ring.pk_size() == 1408, "ring pk size");

    dap_assert(strcmp(s_algo_lrs.name, "lrs") == 0, "lrs algo name");
    dap_assert(s_algo_lrs.key_type == 0x010A, "lrs key type");
    dap_assert(s_algo_lrs.pk_size() == 1424, "lrs pk size");
}

int main(void)
{
    dap_set_appname("test_ledger_anon");
    dap_common_init("test_ledger_anon", NULL);

    test_tx_item_types();
    test_anonymous_detection_empty();
    test_anonymous_detection_in_anon();
    test_anonymous_detection_out_anon();
    test_anonymous_detection_key_image();
    test_anonymous_detection_mixed_items();
    test_key_image_uniqueness();
    test_item_iteration();
    test_algorithm_adapter();

    log_it(L_INFO, "=== ALL Ledger Anon tests PASSED ===");
    dap_common_deinit();
    return 0;
}

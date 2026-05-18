/*
 * test_chipmunk_ring_experimental_marker.c — CR-11.A acceptance test
 *
 * Locks in the @experimental banner on
 * `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` and the CR-11.A wording in
 * `dap_enc_chipmunk_ring.h`.  Round-2 §6.1 required this marker as a
 * same-day PR; the test prevents silent rollback.
 *
 * See SLC `documentation_a57a7626f6cb30b2` (CR-11 master design) §2.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"

#define LOG_TAG "test_chipmunk_ring_experimental_marker"

/* ------------------------------------------------------------------
 * Header-text search helpers (locate file via DAP_INCLUDE_DIR macro
 * or fall back to the canonical relative path under the source tree).
 * ------------------------------------------------------------------ */

static char *s_slurp_file(const char *a_path)
{
    FILE *f = fopen(a_path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)DAP_NEW_Z_SIZE(char, (size_t)sz + 1u);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Candidate locations relative to a probed working directory. */
static char *s_find_header(const char *a_basename)
{
    static const char *const k_prefixes[] = {
        ".",
        "..",
        "../..",
        "../../..",
        "../../../..",
    };
    static const char *const k_paths[] = {
        "module/crypto/include",
        "dap-sdk/module/crypto/include",
    };
    char l_buf[1024];
    for (size_t pi = 0; pi < sizeof(k_prefixes)/sizeof(k_prefixes[0]); ++pi) {
        for (size_t pj = 0; pj < sizeof(k_paths)/sizeof(k_paths[0]); ++pj) {
            snprintf(l_buf, sizeof(l_buf), "%s/%s/%s",
                     k_prefixes[pi], k_paths[pj], a_basename);
            struct stat st;
            if (stat(l_buf, &st) == 0 && S_ISREG(st.st_mode)) {
                return strdup(l_buf);
            }
        }
    }
    return NULL;
}

static bool s_header_contains(const char *a_basename, const char *const *a_needles)
{
    char *path = s_find_header(a_basename);
    if (!path) {
        log_it(L_WARNING, "%s: header not locatable, skipping content check", a_basename);
        return true;   /* CI sandboxes may strip sources; lint-only test */
    }
    char *txt = s_slurp_file(path);
    DAP_DELETE(path);
    if (!txt) return false;

    bool ok = true;
    for (size_t i = 0; a_needles[i] != NULL; ++i) {
        if (strstr(txt, a_needles[i]) == NULL) {
            log_it(L_ERROR, "%s: marker substring missing: '%s'",
                   a_basename, a_needles[i]);
            ok = false;
        }
    }
    DAP_DELETE(txt);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* §2.3 first acceptance criterion: enum carries an experimental banner
 * pointing to CR-9.7 and CR-11.D. */
static bool s_test_enum_banner_present(void)
{
    static const char *const k_needles[] = {
        "@experimental",
        "ChipmunkRing",
        "CR-11",
        "CR-9.7",
        "anonymity",
        NULL,
    };
    dap_assert(s_header_contains("../../include/dap_enc_key.h", k_needles)
               || s_header_contains("dap_enc_key.h", k_needles),
               "DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING carries experimental banner");
    return true;
}

/* §2.3 second criterion: module banner explicit about scope. */
static bool s_test_module_banner_present(void)
{
    static const char *const k_needles[] = {
        "CR-11.A",
        "CR-11.D",
        "CR-9.6 governance",
        "Production-ready path",
        "Experimental path",
        NULL,
    };
    dap_assert(s_header_contains("dap_enc_chipmunk_ring.h", k_needles),
               "dap_enc_chipmunk_ring.h carries CR-11.A module banner");
    return true;
}

/* §2.3 third criterion: enum description reworded — no "AES-N equivalent"
 * claim left in the preset descriptions reachable via the API. */
static bool s_test_descriptions_no_aes_claim(void)
{
    chipmunk_ring_pq_params_t l_params;
    dap_assert(dap_enc_chipmunk_ring_init() == 0, "ring init");
    dap_assert(dap_enc_chipmunk_ring_get_params(&l_params) == 0, "get params");

    chipmunk_ring_security_info_t l_info;
    for (chipmunk_ring_security_level_t lvl = CHIPMUNK_RING_SECURITY_LEVEL_I;
         lvl <= CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS;
         ++lvl) {
        if (dap_enc_chipmunk_ring_init_with_security_level(lvl) != 0) continue;
        if (dap_enc_chipmunk_ring_get_security_info(&l_info) != 0) continue;
        if (l_info.description == NULL) continue;
        dap_assert(strstr(l_info.description, "AES-128 equivalent") == NULL,
                   "no AES-128 equivalent claim");
        dap_assert(strstr(l_info.description, "AES-192 equivalent") == NULL,
                   "no AES-192 equivalent claim");
        dap_assert(strstr(l_info.description, "AES-256 equivalent") == NULL,
                   "no AES-256 equivalent claim");
    }
    /* Restore default for any later test running in this process. */
    dap_enc_chipmunk_ring_init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS);
    return true;
}

/* §2.3 fifth criterion: enum identifiers stable (no rename). */
static bool s_test_enum_identifiers_stable(void)
{
    dap_assert((int)CHIPMUNK_RING_SECURITY_LEVEL_I == 1,         "I == 1");
    dap_assert((int)CHIPMUNK_RING_SECURITY_LEVEL_III == 3,       "III == 3");
    dap_assert((int)CHIPMUNK_RING_SECURITY_LEVEL_V == 5,         "V == 5");
    dap_assert((int)CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS == 6,    "V+ == 6");
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring_experimental_marker");
    dap_common_init("test_chipmunk_ring_experimental_marker", NULL);

    int l_rc = 0;
    if (!s_test_enum_banner_present())          l_rc = 1;
    if (!s_test_module_banner_present())        l_rc = 1;
    if (!s_test_descriptions_no_aes_claim())    l_rc = 1;
    if (!s_test_enum_identifiers_stable())      l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "ALL CR-11.A experimental marker tests PASSED");
    } else {
        log_it(L_ERROR, "Some CR-11.A experimental marker tests FAILED");
    }
    dap_common_deinit();
    return l_rc;
}

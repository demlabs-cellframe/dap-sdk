#pragma once
#include "internal/dap_json_stage1.h" // For dap_json_stage1_t and error codes

#if defined(__AVX512F__) && defined(__AVX512DQ__) && defined(__AVX512BW__)
extern int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1);
#else
// Fallback if AVX-512 is not available
// Forward declare the fallback function
extern int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1);

static inline int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1) {
    // Fallback to AVX2 if available
    return dap_json_stage1_run_avx2(a_stage1);
}
#endif


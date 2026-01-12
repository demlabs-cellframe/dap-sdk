/**
 * @brief x86 movemask - native instruction
 * @details x86/x64 has native _mm_movemask_epi8 / _mm256_movemask_epi8
 * 
 * No helper needed - use direct SIMD intrinsic:
 *   SSE2:   {{MOVEMASK_EPI8}}(vector) → uint16_t
 *   AVX2:   {{MOVEMASK_EPI8}}(vector) → uint32_t
 *   AVX512: {{CMPEQ_EPI8_MASK}}(vector, target) → uint64_t (kmask)
 * 
 * Performance: Single instruction, 1 cycle latency
 */

// No helper needed for x86 - native instruction available
// This file exists for architectural symmetry with ARM


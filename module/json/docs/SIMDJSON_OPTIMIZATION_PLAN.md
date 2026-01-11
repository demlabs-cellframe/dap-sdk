# SimdJSON Stage 1 Optimization Plan

## Current State
- **Performance**: 0.9-1.2 GB/s
- **Target**: 4-5 GB/s (4x improvement needed!)
- **Problem**: Sequential processing despite having SIMD classification

## Root Causes

1. **Not using bitmaps**: Code calls `s_classify_chunk_avx2()` but doesn't use the result
2. **Sequential byte processing**: `while (pos < input_len)` processes one byte at a time
3. **No chunk batching**: Should process 32 bytes per SIMD instruction

## Optimization Strategy

### Phase 1: Use SIMD Classification Results (Expected: 2-3 GB/s)
```c
// BEFORE (current):
while (pos < input_len) {
    uint8_t c = input[pos];
    if (c == '{') { /* add token */ pos++; }
    // ... sequential check for each character
}

// AFTER (optimized):
while (pos + 32 <= input_len) {
    dap_json_bitmaps_t bitmaps = s_classify_chunk_avx2(input + pos);
    
    // Process all structural chars from bitmap in parallel
    uint32_t mask = bitmaps.structural;
    while (mask) {
        int bit_pos = __builtin_ctz(mask);  // Count trailing zeros
        size_t abs_pos = pos + bit_pos;
        // Add token at abs_pos
        mask &= (mask - 1);  // Clear lowest bit
    }
    
    pos += 32;  // Jump by chunk size!
}
```

### Phase 2: Optimize String Handling (Expected: 3-4 GB/s)
- Current: Uses `dap_json_stage1_scan_string_ref()` which is sequential
- Target: SIMD string scanning using quote bitmap and backslash tracking

### Phase 3: Prefetching & Cache Optimization (Expected: 4-5 GB/s)
- Add `__builtin_prefetch()` for next chunks
- Align data structures to cache lines
- Process multiple chunks ahead

## Implementation Steps

1. ✅ Create bitmap classification (`s_classify_chunk_avx2`)
2. ✅ Fix debug logging (log_it -> debug_if)
3. ✅ Fix benchmark timing (dap_time_now -> dap_nanotime_now)
4. 🔄 **[IN PROGRESS]** Rewrite main loop to use bitmaps
5. ⏳ SIMD string scanning
6. ⏳ Prefetching & cache optimization

## Expected Results Timeline
- After Step 4: **2-3 GB/s** (2-3x improvement)
- After Step 5: **3-4 GB/s** (3-4x improvement)
- After Step 6: **4-5 GB/s** (TARGET ACHIEVED! 🎯)


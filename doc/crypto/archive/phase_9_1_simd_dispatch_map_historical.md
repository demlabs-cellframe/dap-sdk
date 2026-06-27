# Phase 9.1 — Карта SIMD-точек входа и dispatch (PQC & Symmetric)

Документ фиксирует текущую карту SIMD-ландшафта для `task_5828bd04` (Phase 9.1), чтобы быстро перейти к 9.2 — базовому замеру и сравнительному бенчмарку с liboqs.

## 1) ML-KEM (FIPS 203): hot-path и runtime-dispatch

### 1.1. Полиномы (`src/kem/mlkem/dap_mlkem_poly.c`)
- Регистрируются через `DAP_DISPATCH_LOCAL`:
  - `s_compress_d4`, `s_compress_d5`, `s_decompress_d4`, `s_decompress_d5`
  - `s_tobytes`, `s_frombytes`, `s_frommsg`, `s_tomsg`
  - `s_mulcache_compute`
  - `s_poly_basemul`
- Инициализация `s_mlkem_poly_dispatch_init`:
  - базовый путь (`_ref`) через `DAP_DISPATCH_DEFAULT`
  - `DAP_DISPATCH_ARCH_SELECT_FOR` по классу `MLKEM`-алг.
  - AVX2: `dap_mlkem_poly_*_avx2`
  - NEON: `dap_mlkem_poly_*_neon`
- Точки входа:
  - публичные API в `dap_mlkem_poly.h` (обёртки через `MLKEM_NAMESPACE(...)`) и использование из `poly.c`, `polyvec.c`, `indcpa.c`

### 1.2. Поливекторы (`src/kem/mlkem/dap_mlkem_polyvec.c`)
- Регистрируемые `DAP_DISPATCH_LOCAL`:
  - `s_pv_compress_d10`, `s_pv_compress_d11`
  - `s_pv_decompress_d10`, `s_pv_decompress_d11`
  - `s_pv_basemul_acc`
- Инициализация: `s_mlkem_polyvec_dispatch_init`
  - AVX2: `dap_mlkem_polyvec_*_avx2`
  - NEON: `dap_mlkem_polyvec_*_neon`
- `PV_ENSURE()` на каждом публичном вызове/обёртке — lazy one-shot инициализация.

### 1.3. NTT (16-bit) и pack/unpack (`src/kem/mlkem/dap_mlkem_ntt.c`)
- Локальные точки `DAP_DISPATCH_LOCAL`:
  - `s_mlkem_ntt_fwd`, `s_mlkem_ntt_inv`
  - `s_mlkem_ntt_pack`, `s_mlkem_ntt_unpack`
- Инициализация `s_mlkem_ntt_dispatch_init`:
  - базовый референс:
    - `s_ntt_forward_generic`, `s_ntt_inverse_generic`, `s_nttpack_scalar`, `s_nttunpack_scalar`
  - выбор класса `MLKEM_NTT16`
  - x86 AVX2: `s_ntt_forward_avx2_packed`, `s_ntt_inverse_avx2_packed`, `dap_mlkem_ntt_nttpack_avx2`, `dap_mlkem_ntt_nttunpack_avx2`
  - x86 AVX-512: `dap_mlkem_ntt_forward_asm`, `dap_mlkem_ntt_inverse_asm`, `dap_mlkem_ntt_nttpack_asm`, `dap_mlkem_ntt_nttunpack_asm`
  - ARM NEON: `s_ntt_forward_neon_packed`, `s_ntt_inverse_neon_packed`, `dap_mlkem_ntt_nttpack_neon`, `dap_mlkem_ntt_nttunpack_neon`
- Внутри `MLKEM_NTT` также применяется логика `s_mlkem_ntt_ensure_*` с `DAP_DISPATCH_ENSURE`.

### 1.4. Rejection sampling (`src/kem/mlkem/dap_mlkem_indcpa.c`)
- `s_rej_uniform` через `DAP_DISPATCH_LOCAL`
- `s_rej_uniform_dispatch_init`:
  - базовый `dap_mlkem_rej_uniform_generic`
  - AVX2: `dap_mlkem_rej_uniform_avx2`
  - x86 AVX-512 + `has_avx512_vbmi2` — ручная подстановка `dap_mlkem_rej_uniform_avx512_vbmi2`
- Сюда же относятся x4-рабочие нагрузки:
  - `dap_mlkem_xof_absorb_squeeze_x4`, `dap_mlkem_prf_x4` в `dap_mlkem_symmetric.h`
  - в `indcpa.c` используется `_poly_getnoise_eta1_x4`/`_poly_getnoise_eta2_x4` и x4-KECCAK workflow.

### 1.5. Симд-карта KEM-модуля (`module/crypto/src/kem/mlkem/CMakeLists.txt`)
- Генерация SIMD-кода (`dap_arch_generate`, `dap_arch_generate_variant`, `dap_tpl_generate`) по шаблонам:
  - `dap_mlkem_poly_simd.c.tpl`, `dap_mlkem_ntt_simd.c.tpl`, `dap_mlkem_poly_ops_simd.c.tpl`, `dap_mlkem_polyvec_simd.c.tpl`, `dap_mlkem_rej_uniform_simd.c.tpl`
  - `ARCH` варианты: `avx2`, `neon`, `avx512_vbmi2` (для rej_uniform), `avx512` assembly-контур
- Итого в сборку добавляются:
  - `dap_mlkem_poly_avx2.c` / `dap_mlkem_poly_neon.c`
  - `dap_mlkem_poly_ops_avx2.c` / `dap_mlkem_poly_ops_neon.c`
  - `dap_mlkem_polyvec_avx2.c` / `dap_mlkem_polyvec_neon.c`
  - `dap_mlkem_ntt_avx2.c` / `dap_mlkem_ntt_neon.c` + сгенерированная ASM `dap_mlkem_ntt_asm.S`
  - `dap_mlkem_rej_uniform_generic.c` / `avx2` / `avx512_vbmi2`.

## 2) Dilithium / ML-DSA (PQC) SIMD

### 2.1. Полиномный модуль (`src/sig/dilithium/dilithium_poly.c`)
- Много точек `DAP_DISPATCH_LOCAL` для критичных операций:
  - `s_dil_ntt_fwd`, `s_dil_ntt_inv`, `s_dil_nttunpack`
  - `s_dil_pw_mont`, `s_dil_reduce`, `s_dil_csubq`, `s_dil_freeze`
  - `s_dil_add`, `s_dil_sub`, `s_dil_neg`, `s_dil_shiftl`
  - `s_dil_decompose`, `s_dil_p2r`, `s_dil_make_hint`, `s_dil_use_hint`
  - `s_dil_chknorm`, `s_dil_rej_uniform`
  - семейство *_gXX:
    - `s_dil_use_hint_g32`, `s_dil_use_hint_g88`
    - `s_dil_decompose_g32`, `s_dil_decompose_g88`
    - `s_dil_zunpack_g17`, `s_dil_zunpack_g19`
    - `s_dil_w1pack_g32`, `s_dil_w1pack_g88`
    - `s_dil_make_hint_g32`, `s_dil_make_hint_g88`
- Инициализация `s_dil_dispatch_init`:
  - AVX-512 приоритет, затем AVX2, затем NEON.
  - Подключаются:
    - AVX2-реализации (`dap_dilithium_*_avx2`)
    - AVX-512-реализации (`dap_dilithium_*_avx512`)
    - NEON (`dap_dilithium_*_neon`)
  - есть точки с потенциально неактивированными ASM вариантами (закомментированные, FMA/V BMI2-спецусловия оставлены в коде).

### 2.2. Polyvec (`src/sig/dilithium/dilithium_polyvec.c`)
- `s_pw_acc` (`polyveck_pointwise_acc_invmontgomery`) через `DAP_DISPATCH_LOCAL`
- AVX2 и AVX-512 варианты:
  - `s_polyvecl_pw_acc_avx2`, `s_polyvecl_pw_acc_avx512`

### 2.3. Карта генерации (`module/crypto/CMakeLists.txt`)
- Порты/темплейты:
  - NTT: `dap_dilithium_ntt_simd.c.tpl` с `NTT_INNER_FILE` для `avx2`, `avx2_512vl`, `avx512`, `neon`
  - Poly SIMD: `dap_dilithium_poly_simd.c.tpl` с `avx2`, `avx512`, `neon`
  - Sample/rejection: `dap_dilithium_sample_simd.c.tpl` (`avx2`, `neon`)
- Ассемблерные точки (System V ABI, x86):
  - `dap_dilithium_poly_ops_avx2.S`, `..._avx512.S`
  - `dap_dilithium_ntt_avx2_asm.S`, fused NTT variants
  - `dap_dilithium_fips204_avx2.S`, `..._avx512.S`
- В сборку добавлены TU для `ntt_avx2`, `ntt_avx2_512vl`, `ntt_avx512`, `sample_avx2`, плюс соответствующие NEON/ASM TU на доступной платформе.

## 3) ChaCha20 / Poly1305 SIMD

### 3.1. ChaCha20 (`src/sym/chacha20/dap_chacha20_poly1305.c`, `dap_chacha20_internal.h`)
- `s_chacha20_simd_fn` + `pthread_once`:
  - x86:
    - (не-Windows) `dap_chacha20_encrypt_asm` при `AVX-512` (`has_asm` + IFMA/VL маркеры)
    - затем AVX2, затем SSE2
  - ARM:
    - NEON (`dap_chacha20_encrypt_neon`)
- API `dap_chacha20_encrypt` переключает:
  - если length >= 256 и выбран SIMD — прямой SIMD вызов
  - иначе fallback в чистый scalar.

### 3.2. Poly1305 (`src/sym/chacha20/dap_chacha20_poly1305.c`, `dap_poly1305_internal.h`)
- `s_poly1305_update` внутри `s_poly1305_init`:
  - x86:
    - при `nblocks>=16` и `s_has_avx512_ifma` → `dap_poly1305_blocks_avx512_ifma`
    - иначе при `nblocks>=8` → `dap_poly1305_blocks_avx2`
  - ARM:
    - AArch64 currently исключён для multi-block NEON из-за issues, fallback scalar в некоторых сборках
    - otherwise `dap_poly1305_blocks_neon`
- Нет `DAP_DISPATCH_*`-макросов на уровне полигенера; выбор сделан вручную в коде.

### 3.3. Генерация (`module/crypto/CMakeLists.txt`)
- ChaCha20 через `dap_arch_generate_variant`:
  - SSE2, AVX2, AVX2_512VL, AVX512, NEON
  - шаблон `dap_chacha20_simd.c.tpl`
- Poly1305 через `dap_arch_generate_variant`:
  - AVX2, AVX512_IFMA, NEON
  - шаблон `dap_poly1305_simd.c.tpl`
- x86 ASM:
  - `dap_chacha20_asm.S.tpl` → `dap_chacha20_avx512vl.S` (`-mavx512f -mavx512vl`)

## 4) Keccak / SHAKE stack и SIMD/SVE/AVX-x4

### 4.1. Keccak single-state (`module/hash/include/dap_hash_keccak.h`)
- Dispatch через `DAP_DISPATCH_DECLARE_RESOLVE` + `DAP_DISPATCH_RESOLVE_*`:
  - `dap_hash_keccak_permute`:
    - x86: `AVX-512VL ASM` > `AVX2 scalar BMI2` > `SSE2` > `ref`
    - ARM:
      - при `aarch64`: `SVE2`, `SVE`, `NEON SHA3 ASM` (или fallback на NEON на Apple из-за известных несовместимостей)
- `DAP_DISPATCH_INLINE_CALL` делает one-time cached вызов.
- Также:
  - fused API для absorb/squeeze с выбором операций по rate:
    - `dap_keccak_sponge_resolve` и глобальные ops
    - AVX2/AVX-512VL `absorb_136`, `absorb_168`, `absorb_72`

### 4.2. Keccak x4 (`module/hash/include/dap_hash_keccak_x4.h`)
- Не классический `DAP_DISPATCH_*`, но есть внутренний runtime cache:
  - `dap_keccak_x4_resolve_permute`:
    - x86: `avx512vl_asm` -> `avx2`
    - ARM: `neon`
    - else: `_opt` (де-инация/ре-интерлив)
- Вызов через `dap_keccak_x4_permute`.

### 4.3. SHAKE256/SHAKE128
- `dap_hash_shake128.h` и `dap_hash_shake256.h` используют `dap_keccak_sponge_get_ops()` (fused ops).
- `dap_hash_shake_x4.h` строит x4-Absorb/Squeeze на базе `dap_keccak_x4_xor/extract` и `dap_keccak_x4_permute`:
  - прямые SIMD/оптимизированные пути плюс fallback `_opt` внутри x4 слоя.

### 4.4. Генерация (`module/hash/CMakeLists.txt`)
- Lane SIMD:
  - avx2/sse2/neon/sve через `dap_keccak_simd_lane.c.tpl`
- Plane SIMD:
  - avx512/sve2 через `dap_keccak_simd_plane.c.tpl`
- Keccak x4:
  - avx512/avx2/neon через `dap_keccak_x4.c.tpl`
- ASM:
  - AVX-512VL фреймворк и PT-вариант
  - BMI2 scalar (asm + C wrapper)
  - AArch64 NEON SHA3 ASM

## 5) Generic NTT dispatcher (`module/math/bignum/src/dap_ntt_dispatch.c`, `include/dap_ntt.h`)
- `DAP_DISPATCH_DEFINE` на внешних API:
  - `dap_ntt_forward`, `dap_ntt_inverse`, `*_mont`, `*_pointwise`
  - `dap_ntt16_forward`, `dap_ntt16_inverse`, `dap_ntt16_basemul`
- `dap_ntt_dispatch_init`:
  - регистрирует классы и правила:
    - Wine workaround (ограничение AVX-512 для Win32 + AVX512F/BW)
    - AMD правила для Zen4/Zen5: cap на AVX-512 -> AVX2
  - маппинг по ISA:
    - 16-bit: SSE2/AVX2/AVX512/NEON
    - 32-bit Montgomery: AVX2/AVX512/NEON
- `dap_ntt.h` делает `DAP_DISPATCH_ENSURE` в каждую публичную inline-обёртку.

## 6) Performance entry points (переход в Phase 9.2)

- `tests/performance/crypto/bench_mlkem_components.c`
  - прямые вызовы:
    - ML-KEM internals (ntt/poly/polyvec/noise/packing)
    - Keccak: `dap_hash_keccak_permute_*` и `dap_keccak_x4_permute_*`
    - ML-KEM SIMD helpers: `dap_mlkem_prf_x4`, `_poly_getnoise_eta1_x4`, т.д.
- `tests/performance/crypto/benchmark_crypto.c`
  - общий режим с опцией `BENCH_KECCAK`
  - сравнение через liboqs/OpenSSL/libsodium если включены.

## 7) Что уже валидируется перед оптимизацией (рекомендованный вход в 9.2)

1. Зафиксировать baseline:
   - CPU + selected arch (из вывода `dap_cpu_arch_get()`, `bench_crypto` + `benchmark` режимы),
   - время по каждому hot-path в `bench_mlkem_components.c`.
2. Сверить что dispatch реально выбирает ожидаемые пути:
   - проверить `dap_hash_keccak_get_impl_name`,
   - добавить/использовать временный диагностический вывод в debug-only (если нужно),
   - зафиксировать поведение на x86/arm в отчёте.
3. Сравнить с liboqs по тем же набором параметров (KEM/Dilithium, fixed seeds), затем уже оптимизировать точки, где отставание > X%.

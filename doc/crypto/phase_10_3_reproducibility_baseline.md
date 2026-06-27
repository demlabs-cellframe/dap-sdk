# Phase 10.3 — Reproducibility baseline (build + tests + benchmarks)

Этот документ фиксирует единый baseline-процесс для проверки выполненных SIMD/архитектурных правок.

## Цель baseline

- Зафиксировать последовательность команд, которая всегда выполняется одинаково на Linux runner.
- Разделить валидацию корректности и performance-срез отдельно.
- Добавить стабильный parser для метрик, который можно запускать в CI/локально.

## 10.3.1. Build + full tests (Linux)

```bash
ROOT_DIR=/mnt/store/work/dap-sdk
BUILD_DIR="$ROOT_DIR/build-phase10"
mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_DAP_SDK_TESTS=ON
cmake --build "$BUILD_DIR" -j$(nproc)
ctest --test-dir "$BUILD_DIR" --output-on-failure
```

## 10.3.2. Performance baseline (Linux)

```bash
ctest --test-dir "$BUILD_DIR" -L performance --output-on-failure
```

## 10.3.3. arm64 runner (native или cross)

- Для arm64 runner используется тот же набор команд с отдельным `BUILD_DIR`, например `"$ROOT_DIR/build-phase10-arm64"`.
- Для cross-инфраструктуры можно переиспользовать существующий CI workflow со стандартными `-S ... -B build-phase10-arm64`.

```bash
cmake -S "$ROOT_DIR" -B "$BUILD_DIR_ARM64" -DCMAKE_BUILD_TYPE=Release -DBUILD_DAP_SDK_TESTS=ON
cmake --build "$BUILD_DIR_ARM64" -j$(nproc)
ctest --test-dir "$BUILD_DIR_ARM64" -L performance --output-on-failure
```

## Parser for ctest result logs

После прогонов складываются стандартные логи ctest (`LastTest.log`), которые парсятся утилитой:

```bash
scripts/phase10_parse_ctest_performance.sh "$BUILD_DIR/Testing/Temporary/LastTest.log"
```

Ожидаемый фрагмент вывода:

```text
OVERALL_RESULT=100% tests passed, 0 tests failed out of 7
METRIC_LABEL=benchmark, TIME=1.15, PROC=2
METRIC_LABEL=crypto, TIME=5.77, PROC=3
METRIC_LABEL=json, TIME=9.12, PROC=3
METRIC_LABEL=performance, TIME=15.11, PROC=7
TOTAL_TEST_TIME=15.12
```

На этом же репозитории зафиксированы снимки:
- `build/phase10_perf_last.log` (Linux x86_64)
- `build-cross/arm64/phase10_arm64_perf_full.log` (arm64 QEMU runner)

## Baseline checklist (must pass before Phase 10.4)

- [x] Linux x86_64: clean configure/build/test suite success
- [x] Linux x86_64: performance labels executed and parsed
- [x] arm64 runner: full build/test/performance success
- [x] arm64 runner: cross smoke (`scripts/test-arm64-cross.sh arm64`) success (json + pqc + crypto)
- [x] Парсинг метрик сохранён (минимум `OVERALL_RESULT`, `METRIC_LABEL*`, `TOTAL_TEST_TIME`)
- [x] Снимок baseline прикреплён к ветке (файл отчёта/артефакт CI)

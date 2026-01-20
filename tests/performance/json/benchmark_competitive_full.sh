#!/bin/bash
# Run competitive benchmark with clean output
export DAP_LOG_LEVEL=WARNING
cd /home/naeper/work/dap-sdk/build/benchmarks
./benchmark_competitive_full "$@"

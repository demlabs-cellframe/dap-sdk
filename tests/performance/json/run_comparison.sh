#!/bin/bash
###############################################################################
# Competitive Benchmark Automation Script
# Runs comprehensive comparison between dap_json and competitors
###############################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
RESULTS_DIR="${SCRIPT_DIR}/results"
BENCHMARK_BIN="${BUILD_DIR}/benchmarks/benchmark_competitive_full"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default options
PHASE="2"
OUTPUT_FORMAT="terminal"
OUTPUT_FILE=""
RUN_PERF=false
RUN_VALGRIND=false

###############################################################################
# Helper Functions
###############################################################################

print_header() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║   DAP JSON COMPETITIVE BENCHMARK - AUTOMATION SUITE            ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_section() {
    echo -e "${BLUE}▶ $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Automated competitive benchmark runner for dap_json

OPTIONS:
    --phase <1|2>           Run Phase 1 or Phase 2 benchmarks (default: 2)
    --output <format>       Output format: terminal|json|html (default: terminal)
    --file <path>           Output file path (optional)
    --perf                  Run with perf profiling
    --valgrind              Run with valgrind memory check
    --help                  Show this help message

EXAMPLES:
    # Run Phase 2 benchmarks with terminal output
    $0 --phase 2

    # Run with JSON output
    $0 --phase 2 --output json --file results.json

    # Run with perf profiling
    $0 --phase 2 --perf

    # Run with valgrind
    $0 --phase 2 --valgrind

EOF
    exit 0
}

###############################################################################
# Parse Arguments
###############################################################################

while [[ $# -gt 0 ]]; do
    case $1 in
        --phase)
            PHASE="$2"
            shift 2
            ;;
        --output)
            OUTPUT_FORMAT="$2"
            shift 2
            ;;
        --file)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --perf)
            RUN_PERF=true
            shift
            ;;
        --valgrind)
            RUN_VALGRIND=true
            shift
            ;;
        --help)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

###############################################################################
# Main Execution
###############################################################################

print_header

# Check if benchmark binary exists
if [ ! -f "$BENCHMARK_BIN" ]; then
    print_error "Benchmark binary not found: $BENCHMARK_BIN"
    print_section "Building benchmark..."
    cd "$BUILD_DIR"
    cmake --build . --target benchmark_competitive_full
    if [ ! -f "$BENCHMARK_BIN" ]; then
        print_error "Failed to build benchmark"
        exit 1
    fi
fi

print_success "Benchmark binary found: $BENCHMARK_BIN"

# Create results directory
mkdir -p "$RESULTS_DIR"

# Run benchmark
print_section "Running Phase $PHASE competitive benchmark..."
echo ""

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="${RESULTS_DIR}/benchmark_phase${PHASE}_${TIMESTAMP}.log"

if [ "$RUN_PERF" = true ]; then
    print_section "Running with perf profiling..."
    PERF_DATA="${RESULTS_DIR}/perf_phase${PHASE}_${TIMESTAMP}.data"
    perf record -o "$PERF_DATA" --call-graph dwarf -- "$BENCHMARK_BIN" | tee "$LOG_FILE"
    print_success "Perf data saved: $PERF_DATA"
    
elif [ "$RUN_VALGRIND" = true ]; then
    print_section "Running with valgrind..."
    VALGRIND_LOG="${RESULTS_DIR}/valgrind_phase${PHASE}_${TIMESTAMP}.log"
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
        --log-file="$VALGRIND_LOG" "$BENCHMARK_BIN" | tee "$LOG_FILE"
    print_success "Valgrind log saved: $VALGRIND_LOG"
    
else
    # Normal run
    "$BENCHMARK_BIN" | tee "$LOG_FILE"
fi

print_success "Results saved: $LOG_FILE"

# Parse results and generate output
if [ "$OUTPUT_FORMAT" = "json" ]; then
    print_section "Generating JSON output..."
    JSON_FILE="${OUTPUT_FILE:-${RESULTS_DIR}/results_phase${PHASE}_${TIMESTAMP}.json}"
    
    # Extract results from log (simple parsing)
    cat > "$JSON_FILE" << EOF
{
    "timestamp": "$(date --iso-8601=seconds)",
    "phase": $PHASE,
    "log_file": "$LOG_FILE",
    "status": "See log file for detailed results",
    "note": "Full JSON parsing to be implemented in Phase 2.6 completion"
}
EOF
    print_success "JSON saved: $JSON_FILE"
    
elif [ "$OUTPUT_FORMAT" = "html" ]; then
    print_warning "HTML output not yet implemented (Phase 2.6 TODO)"
fi

echo ""
print_success "Competitive benchmark completed!"
echo ""
echo "Results location: $RESULTS_DIR"
echo "Latest log: $LOG_FILE"
echo ""

# Generate quick summary
print_section "Quick Summary:"
echo ""
if grep -q "TARGET NOT MET" "$LOG_FILE"; then
    print_warning "Performance target not met - optimization needed"
else
    print_success "Performance targets achieved!"
fi

# Extract win rate
WIN_RATE=$(grep "dap_json.*wins" "$LOG_FILE" | head -1 | grep -oP '\d+\.\d+%' || echo "N/A")
echo "dap_json win rate: $WIN_RATE"

echo ""
print_section "Next steps:"
echo "  1. Review results: less $LOG_FILE"
if [ "$RUN_PERF" = true ]; then
    echo "  2. Analyze perf: perf report -i $PERF_DATA"
fi
echo "  3. Continue Phase 2 optimization for better performance"
echo ""

#!/usr/bin/env bash
# Universal architecture-specific code generator for DAP SDK.
# Processes ONE template into ONE output file via dap_tpl,
# then wraps the result with an architecture preprocessor guard.
#
# All parameters come through environment variables (set by CMake)
# and positional arguments (KEY=VALUE pairs for dap_tpl).
#
# Environment:
#   DAP_TPL_DIR          — path to dap_tpl template engine
#   DAP_ARCH_TEMPLATE    — path to .c.tpl / .h.tpl template
#   DAP_ARCH_OUTPUT      — path to output file
#   DAP_ARCH_GUARD       — guard type: "x86" | "arm" | "sve" | "none" | custom string
#
# Positional args:  KEY=VALUE pairs forwarded to replace_template_placeholders
#   e.g.  ARCH_NAME=AVX2  ARCH_LOWER=avx2  "PRIMITIVES=@/path/to/prim.tpl"
#
# Usage (called by CMake via execute_process):
#   env DAP_TPL_DIR=... DAP_ARCH_TEMPLATE=... DAP_ARCH_OUTPUT=... DAP_ARCH_GUARD=x86 \
#       bash dap_arch_codegen.sh "ARCH_NAME=AVX2" "PRIMITIVES=@file.tpl" ...

set -euo pipefail

: "${DAP_TPL_DIR:?DAP_TPL_DIR not set}"
: "${DAP_ARCH_TEMPLATE:?DAP_ARCH_TEMPLATE not set}"
: "${DAP_ARCH_OUTPUT:?DAP_ARCH_OUTPUT not set}"

GUARD_TYPE="${DAP_ARCH_GUARD:-none}"

if [[ ! -f "${DAP_TPL_DIR}/dap_tpl.sh" ]]; then
    echo "Error: dap_tpl not found at ${DAP_TPL_DIR}/dap_tpl.sh" >&2
    exit 1
fi
if [[ ! -f "${DAP_ARCH_TEMPLATE}" ]]; then
    echo "Error: template not found: ${DAP_ARCH_TEMPLATE}" >&2
    exit 1
fi

source "${DAP_TPL_DIR}/dap_tpl.sh"
export TEMPLATES_DIR="$(cd "$(dirname "${DAP_ARCH_TEMPLATE}")" && pwd)"

mkdir -p "$(dirname "${DAP_ARCH_OUTPUT}")"

replace_template_placeholders "${DAP_ARCH_TEMPLATE}" "${DAP_ARCH_OUTPUT}" "$@"

# Wrap with preprocessor architecture guard
case "${GUARD_TYPE}" in
    x86)
        GUARD='defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)'
        ;;
    arm)
        GUARD='defined(__aarch64__) || defined(__arm__)'
        ;;
    sve)
        GUARD='defined(__aarch64__) && !defined(__APPLE__)'
        ;;
    none)
        exit 0
        ;;
    *)
        GUARD="${GUARD_TYPE}"
        ;;
esac

tmp="${DAP_ARCH_OUTPUT}.tmp"
{ echo "#if ${GUARD}"; cat "${DAP_ARCH_OUTPUT}"; echo "#endif"; } > "${tmp}"
mv "${tmp}" "${DAP_ARCH_OUTPUT}"

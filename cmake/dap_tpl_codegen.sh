#!/usr/bin/env bash
# General-purpose template code generator for DAP SDK.
# Processes ONE template into ONE output file via dap_tpl.
# No architecture guards — pure template substitution.
#
# Environment:
#   DAP_TPL_DIR       — path to dap_tpl template engine
#   DAP_TPL_TEMPLATE  — path to input .tpl file
#   DAP_TPL_OUTPUT    — path to output file
#
# Positional args: KEY=VALUE pairs forwarded to replace_template_placeholders
#   e.g.  BITS=32  COEFF_T=int32_t  "PREFIX=dap_ntt32"

set -euo pipefail

: "${DAP_TPL_DIR:?DAP_TPL_DIR not set}"
: "${DAP_TPL_TEMPLATE:?DAP_TPL_TEMPLATE not set}"
: "${DAP_TPL_OUTPUT:?DAP_TPL_OUTPUT not set}"

if [[ ! -f "${DAP_TPL_DIR}/dap_tpl.sh" ]]; then
    echo "Error: dap_tpl not found at ${DAP_TPL_DIR}/dap_tpl.sh" >&2
    exit 1
fi
if [[ ! -f "${DAP_TPL_TEMPLATE}" ]]; then
    echo "Error: template not found: ${DAP_TPL_TEMPLATE}" >&2
    exit 1
fi

source "${DAP_TPL_DIR}/dap_tpl.sh"
export TEMPLATES_DIR="$(cd "$(dirname "${DAP_TPL_TEMPLATE}")" && pwd)"

mkdir -p "$(dirname "${DAP_TPL_OUTPUT}")"

replace_template_placeholders "${DAP_TPL_TEMPLATE}" "${DAP_TPL_OUTPUT}" "$@"

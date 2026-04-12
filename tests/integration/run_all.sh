#!/usr/bin/env bash
# Run all integration test suites (or specific ones).
# Usage:
#   ./run_all.sh              # run all suites
#   ./run_all.sh 08_mouse     # run only 08_mouse.sh
#   ./run_all.sh 05 08 10     # run specific suites

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SUITE_DIR="$SCRIPT_DIR/suites"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TOTAL_PASS=0
TOTAL_FAIL=0
SUITE_RESULTS=()

if [[ $# -gt 0 ]]; then
    SUITES=()
    for arg in "$@"; do
        found=$(ls "$SUITE_DIR"/${arg}*.sh 2>/dev/null | head -1)
        if [[ -n "$found" ]]; then
            SUITES+=("$found")
        else
            echo -e "${RED}ERROR${NC} no suite matching: $arg"
            exit 1
        fi
    done
else
    mapfile -t SUITES < <(ls "$SUITE_DIR"/*.sh 2>/dev/null | sort)
fi

if (( ${#SUITES[@]} == 0 )); then
    echo -e "${RED}ERROR${NC} no test suites found in $SUITE_DIR"
    exit 1
fi

echo -e "${YELLOW}Running ${#SUITES[@]} integration test suite(s)${NC}"
echo ""

FAILED_SUITES=()

for suite in "${SUITES[@]}"; do
    name=$(basename "$suite" .sh)
    echo -e "${YELLOW}=== $name ===${NC}"

    set +e
    bash "$suite"
    rc=$?
    set -e

    if (( rc == 0 )); then
        SUITE_RESULTS+=("${GREEN}PASS${NC} $name")
    else
        SUITE_RESULTS+=("${RED}FAIL${NC} $name")
        FAILED_SUITES+=("$name")
    fi
    echo ""
done

echo -e "${YELLOW}=== Summary ===${NC}"
for r in "${SUITE_RESULTS[@]}"; do
    echo -e "  $r"
done

echo ""
if (( ${#FAILED_SUITES[@]} == 0 )); then
    echo -e "${GREEN}All ${#SUITES[@]} suites passed.${NC}"
else
    echo -e "${RED}${#FAILED_SUITES[@]} suite(s) failed: ${FAILED_SUITES[*]}${NC}"
fi

(( ${#FAILED_SUITES[@]} == 0 ))

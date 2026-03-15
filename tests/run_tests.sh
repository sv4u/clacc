#!/usr/bin/env bash
#
# clacc test harness
#
# Usage:
#   ./tests/run_tests.sh [--runner <path>] [--filter <pattern>] [--verbose]
#
# Options:
#   --runner <path>   Path to the bytecode runner (default: tools/c0vm-lite/c0vm-lite)
#   --filter <pat>    Only run tests whose path matches <pat>
#   --verbose         Print details for passing tests too
#
# Test file format:
#   Each .clac test file has a companion .expected file OR embedded directives:
#     // TEST: expect_output
#     // TEST: expect_success
#     // TEST: expect_error
#     // TEST: expect_compile_error
#
#   If a .expected file exists, the test is expect_output and stdout is compared.
#   Otherwise, the directive in the .clac file is used.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CLACC="$ROOT_DIR/clacc"
RUNNER="${ROOT_DIR}/tools/c0vm-lite/c0vm-lite"
FILTER=""
VERBOSE=false
TMPDIR_BASE=$(mktemp -d)

trap 'rm -rf "$TMPDIR_BASE"' EXIT

while [[ $# -gt 0 ]]; do
    case "$1" in
        --runner)  RUNNER="$2"; shift 2 ;;
        --filter)  FILTER="$2"; shift 2 ;;
        --verbose) VERBOSE=true; shift ;;
        *)         echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$CLACC" ]]; then
    echo "ERROR: clacc binary not found at $CLACC (run 'make' first)"
    exit 1
fi

if [[ ! -x "$RUNNER" ]]; then
    echo "ERROR: runner not found at $RUNNER"
    echo "  Build c0vm-lite:  make -C tools/c0vm-lite"
    echo "  Or specify:       --runner /path/to/c0vm"
    exit 1
fi

PASS=0
FAIL=0
SKIP=0
FAILURES=""

run_test() {
    local clac_file="$1"
    local test_name="${clac_file#$SCRIPT_DIR/}"
    local test_dir="$TMPDIR_BASE/$(echo "$test_name" | tr '/' '_')"
    mkdir -p "$test_dir"

    if [[ -n "$FILTER" ]] && [[ "$test_name" != *"$FILTER"* ]]; then
        SKIP=$((SKIP + 1))
        return
    fi

    local expect_type=""
    local expected_file="${clac_file%.clac}.expected"

    if [[ -f "$expected_file" ]]; then
        expect_type="expect_output"
    else
        local directive
        directive=$(grep -m1 '// TEST:' "$clac_file" 2>/dev/null || true)
        if [[ "$directive" == *"expect_compile_error"* ]]; then
            expect_type="expect_compile_error"
        elif [[ "$directive" == *"expect_error"* ]]; then
            expect_type="expect_error"
        elif [[ "$directive" == *"expect_success"* ]]; then
            expect_type="expect_success"
        elif [[ "$directive" == *"expect_output"* ]]; then
            expect_type="expect_output"
        else
            echo "  SKIP  $test_name (no test directive or .expected file)"
            SKIP=$((SKIP + 1))
            return
        fi
    fi

    local bc0_file="$test_dir/out.bc0"
    local actual_stdout="$test_dir/stdout.txt"
    local actual_stderr="$test_dir/stderr.txt"

    # --- Compilation phase ---
    local compile_rc=0
    "$CLACC" "$clac_file" -o "$bc0_file" >"$test_dir/compile_stdout.txt" 2>"$test_dir/compile_stderr.txt" || compile_rc=$?

    if [[ "$expect_type" == "expect_compile_error" ]]; then
        if [[ $compile_rc -ne 0 ]]; then
            PASS=$((PASS + 1))
            if $VERBOSE; then echo "  PASS  $test_name (compile error as expected)"; fi
            return
        else
            FAIL=$((FAIL + 1))
            FAILURES="${FAILURES}\n  FAIL  $test_name: expected compile error but compilation succeeded"
            echo "  FAIL  $test_name: expected compile error but compilation succeeded"
            return
        fi
    fi

    if [[ $compile_rc -ne 0 ]]; then
        FAIL=$((FAIL + 1))
        local errmsg
        errmsg=$(cat "$test_dir/compile_stderr.txt")
        FAILURES="${FAILURES}\n  FAIL  $test_name: compilation failed (rc=$compile_rc): $errmsg"
        echo "  FAIL  $test_name: compilation failed unexpectedly"
        return
    fi

    # --- Execution phase ---
    local run_rc=0
    if command -v timeout >/dev/null 2>&1; then
        timeout 10 "$RUNNER" "$bc0_file" >"$actual_stdout" 2>"$actual_stderr" || run_rc=$?
    elif command -v gtimeout >/dev/null 2>&1; then
        gtimeout 10 "$RUNNER" "$bc0_file" >"$actual_stdout" 2>"$actual_stderr" || run_rc=$?
    else
        "$RUNNER" "$bc0_file" >"$actual_stdout" 2>"$actual_stderr" || run_rc=$?
    fi

    case "$expect_type" in
        expect_output)
            if [[ $run_rc -ne 0 ]]; then
                FAIL=$((FAIL + 1))
                FAILURES="${FAILURES}\n  FAIL  $test_name: runner exited $run_rc"
                echo "  FAIL  $test_name: runner exited $run_rc"
                return
            fi
            if diff -q "$expected_file" "$actual_stdout" >/dev/null 2>&1; then
                PASS=$((PASS + 1))
                if $VERBOSE; then echo "  PASS  $test_name"; fi
            else
                FAIL=$((FAIL + 1))
                local diffout
                diffout=$(diff "$expected_file" "$actual_stdout" || true)
                FAILURES="${FAILURES}\n  FAIL  $test_name: output mismatch\n$diffout"
                echo "  FAIL  $test_name: output mismatch"
            fi
            ;;
        expect_success)
            if [[ $run_rc -eq 0 ]]; then
                PASS=$((PASS + 1))
                if $VERBOSE; then echo "  PASS  $test_name"; fi
            else
                FAIL=$((FAIL + 1))
                FAILURES="${FAILURES}\n  FAIL  $test_name: expected success but exited $run_rc"
                echo "  FAIL  $test_name: expected exit 0 but got $run_rc"
            fi
            ;;
        expect_error)
            if [[ $run_rc -ne 0 ]]; then
                PASS=$((PASS + 1))
                if $VERBOSE; then echo "  PASS  $test_name"; fi
            else
                FAIL=$((FAIL + 1))
                FAILURES="${FAILURES}\n  FAIL  $test_name: expected error but exited 0"
                echo "  FAIL  $test_name: expected nonzero exit but got 0"
            fi
            ;;
    esac
}

echo "clacc test suite"
echo "  compiler: $CLACC"
echo "  runner:   $RUNNER"
echo ""

# Discover and run all tests
for clac_file in $(find "$SCRIPT_DIR" -name '*.clac' -not -path '*/regression/*' | sort); do
    run_test "$clac_file"
done

# Regression tests (existing example programs)
for clac_file in $(find "$SCRIPT_DIR/regression" -name '*.clac' 2>/dev/null | sort); do
    run_test "$clac_file"
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "Failures:"
    echo -e "$FAILURES"
    exit 1
fi

exit 0

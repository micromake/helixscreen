#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for helix-launcher.sh binary selection logic

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
CYAN='\033[36m'
BOLD='\033[1m'
RESET='\033[0m'

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
declare -a FAILED_TESTS

print_test() {
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "\n${CYAN}[TEST $TESTS_RUN]${RESET} $1"
}

assert() {
    local condition="$1"
    local message="$2"
    if eval "$condition"; then
        echo -e "${GREEN}  ✓${RESET} $message"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}  ✗${RESET} $message"
        FAILED_TESTS+=("TEST $TESTS_RUN: $message")
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Create temp dir with mock binaries for testing
setup_mock_bindir() {
    local tmpdir
    tmpdir=$(mktemp -d)
    echo "$tmpdir"
}

cleanup_mock_bindir() {
    rm -rf "$1"
}

# Extract select_binary function from launcher for testing
extract_select_binary() {
    # Source the function definition by extracting it
    local launcher="${PROJECT_ROOT}/scripts/helix-launcher.sh"
    # Extract the select_binary function
    sed -n '/^select_binary()/,/^}/p' "$launcher"
}

# Create a self-contained test script that includes select_binary
create_test_harness() {
    local bindir="$1"
    local harness="$bindir/_test_harness.sh"
    cat > "$harness" << 'HARNESS_EOF'
#!/bin/sh
# Extract select_binary from launcher
HARNESS_EOF
    extract_select_binary >> "$harness"
    echo 'select_binary "$1"' >> "$harness"
    chmod +x "$harness"
    echo "$harness"
}

# Test: no fallback binary → selects primary
test_no_fallback_selects_primary() {
    print_test "No fallback binary → selects primary"
    local bindir
    bindir=$(setup_mock_bindir)

    # Create only primary binary
    echo '#!/bin/sh' > "$bindir/helix-screen"
    chmod +x "$bindir/helix-screen"

    local harness
    harness=$(create_test_harness "$bindir")

    local result
    result=$(sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen' ]" \
        "Selected primary when no fallback exists"

    cleanup_mock_bindir "$bindir"
}

# Test: primary has missing libs → selects fbdev
test_missing_libs_selects_fbdev() {
    print_test "Primary has missing libs → selects fbdev"
    local bindir
    bindir=$(setup_mock_bindir)

    # Create both binaries
    echo '#!/bin/sh' > "$bindir/helix-screen"
    echo '#!/bin/sh' > "$bindir/helix-screen-fbdev"
    chmod +x "$bindir/helix-screen" "$bindir/helix-screen-fbdev"

    # Create a fake ldd that reports missing libs for primary
    local fake_ldd="$bindir/ldd"
    cat > "$fake_ldd" << 'EOF'
#!/bin/sh
case "$1" in
    *helix-screen-fbdev)
        echo "	linux-vdso.so.1 => (0x00007ffd)"
        echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
        ;;
    *helix-screen)
        echo "	linux-vdso.so.1 => (0x00007ffd)"
        echo "	libEGL.so.1 => not found"
        echo "	libGLESv2.so.2 => not found"
        echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
        ;;
esac
EOF
    chmod +x "$fake_ldd"

    local harness
    harness=$(create_test_harness "$bindir")
    local result
    result=$(PATH="$bindir:$PATH" sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen-fbdev' ]" \
        "Selected fbdev when primary has missing GL libs"

    cleanup_mock_bindir "$bindir"
}

# Test: primary libs all satisfied → selects primary (DRM)
test_libs_satisfied_selects_primary() {
    print_test "Primary libs all satisfied → selects primary"
    local bindir
    bindir=$(setup_mock_bindir)

    echo '#!/bin/sh' > "$bindir/helix-screen"
    echo '#!/bin/sh' > "$bindir/helix-screen-fbdev"
    chmod +x "$bindir/helix-screen" "$bindir/helix-screen-fbdev"

    # Create a fake ldd that reports all libs found
    local fake_ldd="$bindir/ldd"
    cat > "$fake_ldd" << 'EOF'
#!/bin/sh
echo "	linux-vdso.so.1 => (0x00007ffd)"
echo "	libEGL.so.1 => /usr/lib/aarch64-linux-gnu/libEGL.so.1 (0x00007f)"
echo "	libGLESv2.so.2 => /usr/lib/aarch64-linux-gnu/libGLESv2.so.2 (0x00007f)"
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
EOF
    chmod +x "$fake_ldd"

    local harness
    harness=$(create_test_harness "$bindir")
    local result
    result=$(PATH="$bindir:$PATH" sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen' ]" \
        "Selected primary when all libs satisfied"

    cleanup_mock_bindir "$bindir"
}

# Test: HELIX_DISPLAY_BACKEND forced to fbdev → skips DRM
test_env_forced_fbdev() {
    print_test "HELIX_DISPLAY_BACKEND=fbdev → selects fbdev"
    local bindir
    bindir=$(setup_mock_bindir)

    echo '#!/bin/sh' > "$bindir/helix-screen"
    echo '#!/bin/sh' > "$bindir/helix-screen-fbdev"
    chmod +x "$bindir/helix-screen" "$bindir/helix-screen-fbdev"

    local harness
    harness=$(create_test_harness "$bindir")
    local result
    result=$(HELIX_DISPLAY_BACKEND=fbdev sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen-fbdev' ]" \
        "Selected fbdev when HELIX_DISPLAY_BACKEND=fbdev"

    cleanup_mock_bindir "$bindir"
}

# Test: ldd not available → tries primary (best effort)
test_no_ldd_selects_primary() {
    print_test "ldd not available → selects primary (best effort)"
    local bindir
    bindir=$(setup_mock_bindir)

    echo '#!/bin/sh' > "$bindir/helix-screen"
    echo '#!/bin/sh' > "$bindir/helix-screen-fbdev"
    chmod +x "$bindir/helix-screen" "$bindir/helix-screen-fbdev"

    local harness
    harness=$(create_test_harness "$bindir")
    # Create a wrapper dir with symlinks to essentials but NOT ldd
    local wrapper_dir="$bindir/_no_ldd"
    mkdir -p "$wrapper_dir"
    for cmd in sh grep command env; do
        local cmd_path
        cmd_path=$(command -v "$cmd" 2>/dev/null || true)
        [ -n "$cmd_path" ] && ln -sf "$cmd_path" "$wrapper_dir/$cmd"
    done
    local result
    result=$(PATH="$wrapper_dir" sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen' ]" \
        "Selected primary when ldd not available"

    cleanup_mock_bindir "$bindir"
}

# Test: both binaries present, no env override, ldd says both fine → primary
test_both_fine_selects_primary() {
    print_test "Both binaries fine → selects primary"
    local bindir
    bindir=$(setup_mock_bindir)

    echo '#!/bin/sh' > "$bindir/helix-screen"
    echo '#!/bin/sh' > "$bindir/helix-screen-fbdev"
    chmod +x "$bindir/helix-screen" "$bindir/helix-screen-fbdev"

    local fake_ldd="$bindir/ldd"
    cat > "$fake_ldd" << 'EOF'
#!/bin/sh
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
EOF
    chmod +x "$fake_ldd"

    local harness
    harness=$(create_test_harness "$bindir")
    local result
    result=$(PATH="$bindir:$PATH" sh "$harness" "$bindir")

    assert "[ '$result' = '$bindir/helix-screen' ]" \
        "Selected primary when both binaries have satisfied deps"

    cleanup_mock_bindir "$bindir"
}

# Run all tests
main() {
    echo -e "${BOLD}${CYAN}Launcher Binary Selection Test Harness${RESET}"
    echo -e "${CYAN}Project:${RESET} $PROJECT_ROOT"
    echo ""

    test_no_fallback_selects_primary
    test_missing_libs_selects_fbdev
    test_libs_satisfied_selects_primary
    test_env_forced_fbdev
    test_no_ldd_selects_primary
    test_both_fine_selects_primary

    # Print summary
    echo ""
    echo -e "${BOLD}${CYAN}Test Summary${RESET}"
    echo -e "${CYAN}────────────────────────────────────────${RESET}"
    echo -e "Total tests: $TESTS_RUN"
    echo -e "${GREEN}Passed: $TESTS_PASSED${RESET}"

    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}Failed: $TESTS_FAILED${RESET}"
        echo ""
        echo -e "${RED}${BOLD}Failed tests:${RESET}"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗${RESET} $test"
        done
        exit 1
    else
        echo -e "${GREEN}${BOLD}✓ All tests passed!${RESET}"
        exit 0
    fi
}

main

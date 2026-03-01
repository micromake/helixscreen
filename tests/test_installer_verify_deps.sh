#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Tests for installer verify_binary_deps() fbdev fallback behavior

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

# Helper: Create mock install dir with mock binaries and fake ldd
setup_mock_install() {
    local tmpdir
    tmpdir=$(mktemp -d)
    mkdir -p "$tmpdir/bin"
    echo '#!/bin/sh' > "$tmpdir/bin/helix-screen"
    chmod +x "$tmpdir/bin/helix-screen"
    echo "$tmpdir"
}

cleanup_mock() {
    rm -rf "$1"
}

# Create a test script that sources requirements.sh with stubs
create_verify_harness() {
    local install_dir="$1"
    local harness="$install_dir/_verify_harness.sh"

    cat > "$harness" << HEOF
#!/bin/sh
# Stub logging functions
log_success() { echo "[SUCCESS] \$*"; }
log_warn() { echo "[WARN] \$*"; }
log_error() { echo "[ERROR] \$*"; }
log_info() { echo "[INFO] \$*"; }

INSTALL_DIR="$install_dir"
SUDO=""
PLATFORM="\$1"

# Source the real verify_binary_deps function
_HELIX_REQUIREMENTS_SOURCED=""
HEOF

    # Extract just verify_binary_deps from requirements.sh
    sed -n '/^verify_binary_deps()/,/^}/p' "$PROJECT_ROOT/scripts/lib/installer/requirements.sh" >> "$harness"

    echo 'verify_binary_deps "$PLATFORM"' >> "$harness"

    chmod +x "$harness"
    echo "$harness"
}

# Test: primary binary deps satisfied → success
test_primary_deps_satisfied() {
    print_test "Primary binary deps satisfied → success, no warning"
    local install_dir
    install_dir=$(setup_mock_install)

    # Create fake ldd that reports all deps found
    local fake_ldd="$install_dir/ldd"
    cat > "$fake_ldd" << 'EOF'
#!/bin/sh
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
echo "	libEGL.so.1 => /usr/lib/aarch64-linux-gnu/libEGL.so.1 (0x00007f)"
EOF
    chmod +x "$fake_ldd"

    local harness
    harness=$(create_verify_harness "$install_dir")

    local output exit_code
    output=$(PATH="$install_dir:$PATH" sh "$harness" "pi" 2>&1) && exit_code=0 || exit_code=$?

    assert "[ $exit_code -eq 0 ]" "Exit code 0"
    assert "[[ '$output' == *'[SUCCESS]'* ]]" "Shows success message"
    assert "[[ '$output' != *'[WARN]'* ]]" "No warnings"

    cleanup_mock "$install_dir"
}

# Test: primary missing libs, fbdev exists and satisfied → warn, return 0
test_primary_missing_fbdev_ok() {
    print_test "Primary missing libs, fbdev satisfied → warn, return 0"
    local install_dir
    install_dir=$(setup_mock_install)

    # Create fbdev fallback binary
    echo '#!/bin/sh' > "$install_dir/bin/helix-screen-fbdev"
    chmod +x "$install_dir/bin/helix-screen-fbdev"

    # Create fake ldd: primary missing GL, fbdev fine
    local fake_ldd="$install_dir/ldd"
    cat > "$fake_ldd" << 'LDDEOF'
#!/bin/sh
case "$1" in
    *helix-screen-fbdev)
        echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
        ;;
    *helix-screen)
        echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
        echo "	libEGL.so.1 => not found"
        echo "	libGLESv2.so.2 => not found"
        ;;
esac
LDDEOF
    chmod +x "$fake_ldd"

    # Stub apt-cache to say libssl1.1 is not available (so it doesn't try to install)
    local fake_apt="$install_dir/apt-cache"
    echo '#!/bin/sh
exit 1' > "$fake_apt"
    chmod +x "$fake_apt"

    local harness
    harness=$(create_verify_harness "$install_dir")

    local output exit_code
    output=$(PATH="$install_dir:$PATH" sh "$harness" "pi" 2>&1) && exit_code=0 || exit_code=$?

    assert "[ $exit_code -eq 0 ]" "Exit code 0 (not fatal)"
    assert "[[ '$output' == *'fbdev fallback'* ]]" "Mentions fbdev fallback"
    assert "[[ '$output' == *'[WARN]'* ]]" "Shows warning"

    cleanup_mock "$install_dir"
}

# Test: primary missing libs, no fbdev binary → error exit 1
test_primary_missing_no_fbdev() {
    print_test "Primary missing libs, no fbdev → error exit 1"
    local install_dir
    install_dir=$(setup_mock_install)

    # Create fake ldd: primary missing GL
    local fake_ldd="$install_dir/ldd"
    cat > "$fake_ldd" << 'LDDEOF'
#!/bin/sh
echo "	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x00007f)"
echo "	libEGL.so.1 => not found"
LDDEOF
    chmod +x "$fake_ldd"

    local fake_apt="$install_dir/apt-cache"
    echo '#!/bin/sh
exit 1' > "$fake_apt"
    chmod +x "$fake_apt"

    local harness
    harness=$(create_verify_harness "$install_dir")

    local output exit_code
    output=$(PATH="$install_dir:$PATH" sh "$harness" "pi" 2>&1) && exit_code=0 || exit_code=$?

    assert "[ $exit_code -ne 0 ]" "Exit code non-zero"
    assert "[[ '$output' == *'[ERROR]'* ]]" "Shows error message"

    cleanup_mock "$install_dir"
}

# Test: both binaries missing libs → error exit 1
test_both_missing() {
    print_test "Both binaries missing libs → error exit 1"
    local install_dir
    install_dir=$(setup_mock_install)

    echo '#!/bin/sh' > "$install_dir/bin/helix-screen-fbdev"
    chmod +x "$install_dir/bin/helix-screen-fbdev"

    # Both missing
    local fake_ldd="$install_dir/ldd"
    cat > "$fake_ldd" << 'LDDEOF'
#!/bin/sh
case "$1" in
    *helix-screen-fbdev)
        echo "	libsomething.so => not found"
        ;;
    *helix-screen)
        echo "	libEGL.so.1 => not found"
        ;;
esac
LDDEOF
    chmod +x "$fake_ldd"

    local fake_apt="$install_dir/apt-cache"
    echo '#!/bin/sh
exit 1' > "$fake_apt"
    chmod +x "$fake_apt"

    local harness
    harness=$(create_verify_harness "$install_dir")

    local output exit_code
    output=$(PATH="$install_dir:$PATH" sh "$harness" "pi" 2>&1) && exit_code=0 || exit_code=$?

    assert "[ $exit_code -ne 0 ]" "Exit code non-zero"
    assert "[[ '$output' == *'[ERROR]'* ]]" "Shows error message"

    cleanup_mock "$install_dir"
}

main() {
    echo -e "${BOLD}${CYAN}Installer verify_binary_deps() Test Harness${RESET}"
    echo -e "${CYAN}Project:${RESET} $PROJECT_ROOT"
    echo ""

    test_primary_deps_satisfied
    test_primary_missing_fbdev_ok
    test_primary_missing_no_fbdev
    test_both_missing

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

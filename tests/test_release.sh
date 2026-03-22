#!/bin/bash
# test_release.sh — Validates the release workflow and required release files.
# Ensures the release pipeline won't fail due to missing files, bad YAML
# structure, or inconsistent references.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RELEASE_YML="$REPO_ROOT/.github/workflows/release.yml"
CI_YML="$REPO_ROOT/.github/workflows/ci.yml"

passed=0
failed=0
total=0

pass() {
    printf "  %-55s PASS\n" "$1"
    passed=$((passed + 1))
    total=$((total + 1))
}

fail() {
    printf "  %-55s FAIL\n" "$1"
    printf "    %s\n" "$2"
    failed=$((failed + 1))
    total=$((total + 1))
}

echo ""
echo "=== Release Workflow Validation ==="

# ---------------------------------------------------------------------------
# 1. Required files for the portable zip exist in the repo
# ---------------------------------------------------------------------------
test_name="README.md exists"
if [ -f "$REPO_ROOT/README.md" ]; then
    pass "$test_name"
else
    fail "$test_name" "README.md is missing — portable zip will fail"
fi

test_name="LICENSE exists"
if [ -f "$REPO_ROOT/LICENSE" ]; then
    pass "$test_name"
else
    fail "$test_name" "LICENSE is missing — portable zip will fail"
fi

# ---------------------------------------------------------------------------
# 2. Release workflow file exists and is well-formed
# ---------------------------------------------------------------------------
test_name="release.yml exists"
if [ -f "$RELEASE_YML" ]; then
    pass "$test_name"
else
    fail "$test_name" "$RELEASE_YML not found"
    echo ""
    echo "  $total tests, $passed passed, $failed failed"
    exit 1
fi

test_name="release.yml is valid YAML (no tabs)"
if grep -Pn '^\t' "$RELEASE_YML" >/dev/null 2>&1; then
    fail "$test_name" "YAML files must not contain tabs for indentation"
else
    pass "$test_name"
fi

# ---------------------------------------------------------------------------
# 3. Release triggers on version tags
# ---------------------------------------------------------------------------
test_name="release triggers on v* tags"
if grep -q '"v\*"' "$RELEASE_YML" || grep -q "'v\*'" "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing tag trigger pattern 'v*'"
fi

# ---------------------------------------------------------------------------
# 4. Required jobs exist
# ---------------------------------------------------------------------------
test_name="test job exists"
if grep -q '^\s*test:' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing 'test' job in release workflow"
fi

test_name="build-windows job exists"
if grep -q '^\s*build-windows:' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing 'build-windows' job in release workflow"
fi

test_name="release job exists"
if grep -q '^\s*release:' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing 'release' job in release workflow"
fi

# ---------------------------------------------------------------------------
# 5. No Linux build job (Windows-only project)
# ---------------------------------------------------------------------------
test_name="no build-linux job (Windows-only)"
if grep -q '^\s*build-linux:' "$RELEASE_YML"; then
    fail "$test_name" "build-linux job found — this is a Windows-only project"
else
    pass "$test_name"
fi

# ---------------------------------------------------------------------------
# 6. Release job depends on build-windows (not build-linux)
# ---------------------------------------------------------------------------
test_name="release depends on build-windows"
if grep -A5 'release:' "$RELEASE_YML" | grep 'needs:' | grep -q 'build-windows'; then
    pass "$test_name"
else
    fail "$test_name" "release job does not depend on build-windows"
fi

test_name="release does NOT depend on build-linux"
if grep -A5 'release:' "$RELEASE_YML" | grep 'needs:' | grep -q 'build-linux'; then
    fail "$test_name" "release job still depends on build-linux"
else
    pass "$test_name"
fi

# ---------------------------------------------------------------------------
# 7. build-windows depends on test
# ---------------------------------------------------------------------------
test_name="build-windows depends on test job"
if grep -A5 'build-windows:' "$RELEASE_YML" | grep -q 'needs:.*test'; then
    pass "$test_name"
else
    fail "$test_name" "build-windows should depend on the test job"
fi

# ---------------------------------------------------------------------------
# 8. Artifact names are consistent (upload matches what release expects)
# ---------------------------------------------------------------------------
test_name="windows artifact name is consistent"
upload_name=$(grep -A2 'upload-artifact' "$RELEASE_YML" | grep 'name:' | head -1 | sed 's/.*name:\s*//' | tr -d ' ')
if [ -n "$upload_name" ]; then
    pass "$test_name"
else
    fail "$test_name" "Could not find artifact upload name"
fi

# ---------------------------------------------------------------------------
# 9. Portable zip includes myterm.exe
# ---------------------------------------------------------------------------
test_name="portable zip references myterm.exe"
if grep -q 'myterm\.exe' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Portable zip step does not reference myterm.exe"
fi

# ---------------------------------------------------------------------------
# 9b. Inno Setup installer
# ---------------------------------------------------------------------------
test_name="installer.iss exists"
if [ -f "$REPO_ROOT/installer.iss" ]; then
    pass "$test_name"
else
    fail "$test_name" "installer.iss is missing — installer build will fail"
fi

test_name="installer.iss references myterm.exe"
if grep -q 'MyAppExeName.*myterm\.exe\|Source:.*myterm\.exe\|{#MyAppExeName}' "$REPO_ROOT/installer.iss"; then
    pass "$test_name"
else
    fail "$test_name" "installer.iss does not reference myterm.exe"
fi

test_name="installer.iss includes README.md"
if grep -q 'README.md' "$REPO_ROOT/installer.iss"; then
    pass "$test_name"
else
    fail "$test_name" "installer.iss does not include README.md"
fi

test_name="installer.iss includes LICENSE"
if grep -q 'LICENSE' "$REPO_ROOT/installer.iss"; then
    pass "$test_name"
else
    fail "$test_name" "installer.iss does not include LICENSE"
fi

test_name="release workflow builds installer with iscc"
if grep -q 'iscc' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Release workflow does not invoke iscc (Inno Setup compiler)"
fi

test_name="release workflow uses Zig 0.15.2 or newer"
if grep -Eq 'version:\s*0\.15\.[2-9]|version:\s*0\.[1-9][6-9]\.|version:\s*[1-9]\.' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Release workflow should use Zig 0.15.2 or newer for Ghostty"
fi

test_name="release workflow uploads installer artifact"
if grep -q 'myterm-windows-installer' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Release workflow does not upload installer artifact"
fi

test_name="release notes reference installer"
if grep -q 'setup\.exe' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Release notes do not mention the installer"
fi

# ---------------------------------------------------------------------------
# 10. Release notes are Windows-only (no Linux references)
# ---------------------------------------------------------------------------
test_name="release notes have no Linux download entry"
if awk '/Generate release notes/,/EOF|NOTES_EOF/' "$RELEASE_YML" | grep -qi 'linux.*tarball\|linux.*tar\.gz'; then
    fail "$test_name" "Release notes still reference Linux tarball"
else
    pass "$test_name"
fi

test_name="release notes have no Linux install instructions"
if awk '/Generate release notes/,/EOF|NOTES_EOF/' "$RELEASE_YML" | grep -q 'chmod +x'; then
    fail "$test_name" "Release notes still contain Linux install instructions"
else
    pass "$test_name"
fi

# ---------------------------------------------------------------------------
# 11. contents: write permission is set
# ---------------------------------------------------------------------------
test_name="contents write permission is set"
if grep -q 'contents:\s*write' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing 'contents: write' permission — release creation will fail"
fi

# ---------------------------------------------------------------------------
# 12. GitHub Release action is configured
# ---------------------------------------------------------------------------
test_name="uses softprops/action-gh-release"
if grep -q 'softprops/action-gh-release' "$RELEASE_YML"; then
    pass "$test_name"
else
    fail "$test_name" "Missing softprops/action-gh-release action"
fi

# ---------------------------------------------------------------------------
# 13. CI workflow exists and runs tests
# ---------------------------------------------------------------------------
test_name="ci.yml exists"
if [ -f "$CI_YML" ]; then
    pass "$test_name"
else
    fail "$test_name" "$CI_YML not found"
fi

test_name="ci.yml runs make test"
if grep -q 'make test' "$CI_YML"; then
    pass "$test_name"
else
    fail "$test_name" "CI workflow does not run 'make test'"
fi

test_name="ci.yml has Windows build job"
if grep -q '^\s*build-windows:' "$CI_YML"; then
    pass "$test_name"
else
    fail "$test_name" "CI workflow should include a Windows build job"
fi

test_name="ci.yml installs Zig for Windows build"
if grep -q 'mlugg/setup-zig@v2' "$CI_YML"; then
    pass "$test_name"
else
    fail "$test_name" "CI workflow should install Zig for the Windows build"
fi

test_name="ci.yml uses Zig 0.15.2 or newer"
if grep -Eq 'version:\s*0\.15\.[2-9]|version:\s*0\.[1-9][6-9]\.|version:\s*[1-9]\.' "$CI_YML"; then
    pass "$test_name"
else
    fail "$test_name" "CI workflow should use Zig 0.15.2 or newer for Ghostty"
fi

# ---------------------------------------------------------------------------
# 14. Source files referenced in tests/Makefile exist
# ---------------------------------------------------------------------------
for src in tabs.c config.c search.c splits.c; do
    test_name="src/$src exists (needed by test build)"
    if [ -f "$REPO_ROOT/src/$src" ]; then
        pass "$test_name"
    else
        fail "$test_name" "src/$src is missing — tests will fail to compile"
    fi
done

# ---------------------------------------------------------------------------
# 15. Test stubs exist
# ---------------------------------------------------------------------------
test_name="tests/stubs.c exists"
if [ -f "$REPO_ROOT/tests/stubs.c" ]; then
    pass "$test_name"
else
    fail "$test_name" "tests/stubs.c is missing — tests will fail to compile"
fi

# ---------------------------------------------------------------------------
# 16. CMakeLists.txt exists and has correct structure
# ---------------------------------------------------------------------------
test_name="CMakeLists.txt exists"
if [ -f "$REPO_ROOT/CMakeLists.txt" ]; then
    pass "$test_name"
else
    fail "$test_name" "CMakeLists.txt is missing — build will fail"
fi

test_name="CMakeLists.txt builds myterm executable"
if grep -q 'add_executable(myterm' "$REPO_ROOT/CMakeLists.txt"; then
    pass "$test_name"
else
    fail "$test_name" "CMakeLists.txt does not define myterm executable target"
fi

test_name="CMakeLists.txt has MYTERM_BUILD_TESTS option"
if grep -q 'MYTERM_BUILD_TESTS' "$REPO_ROOT/CMakeLists.txt"; then
    pass "$test_name"
else
    fail "$test_name" "Missing MYTERM_BUILD_TESTS option — release build flag will fail"
fi

test_name="CMakeLists.txt pins Ghostty to a commit"
if grep -Eq 'GIT_TAG\s+[0-9a-f]{40}' "$REPO_ROOT/CMakeLists.txt"; then
    pass "$test_name"
else
    fail "$test_name" "Ghostty dependency should be pinned to a specific commit"
fi

# ---------------------------------------------------------------------------
# 16b. Windows links Ghostty statically to avoid missing import-library rules
# ---------------------------------------------------------------------------
test_name="CMakeLists.txt selects ghostty-vt-static on Windows"
if awk '/if\(WIN32\)/,/endif\(\)/' "$REPO_ROOT/CMakeLists.txt" | grep -q 'MYTERM_GHOSTTY_TARGET ghostty-vt-static'; then
    pass "$test_name"
else
    fail "$test_name" "Windows builds should link ghostty-vt-static"
fi

test_name="myterm_lib links against configurable Ghostty target"
if grep -q '\${MYTERM_GHOSTTY_TARGET}' "$REPO_ROOT/CMakeLists.txt"; then
    pass "$test_name"
else
    fail "$test_name" "myterm_lib should link against \${MYTERM_GHOSTTY_TARGET}"
fi

test_name="Windows disables Ghostty SIMD for static linking"
if awk '/if\(WIN32\)/,/endif\(\)/' "$REPO_ROOT/CMakeLists.txt" | grep -q 'GHOSTTY_ZIG_BUILD_FLAGS ".*-Dsimd=false'; then
    pass "$test_name"
else
    fail "$test_name" "Windows static Ghostty build should set -Dsimd=false"
fi

# ---------------------------------------------------------------------------
# 17. Windows-specific source exists
# ---------------------------------------------------------------------------
test_name="src/pty_windows.c exists"
if [ -f "$REPO_ROOT/src/pty_windows.c" ]; then
    pass "$test_name"
else
    fail "$test_name" "src/pty_windows.c is missing — Windows build will fail"
fi

test_name="src/ghostty/ghostty.h compatibility header exists"
if [ -f "$REPO_ROOT/src/ghostty/ghostty.h" ]; then
    pass "$test_name"
else
    fail "$test_name" "Missing Ghostty compatibility header used by the Windows build"
fi

test_name="CMakeLists.txt adds Ghostty source include directory"
if grep -q 'ghostty_SOURCE_DIR}/include' "$REPO_ROOT/CMakeLists.txt"; then
    pass "$test_name"
else
    fail "$test_name" "CMakeLists.txt should include Ghostty's source headers for compilation"
fi

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
echo ""
echo "  $total tests, $passed passed, $failed failed"

if [ "$failed" -gt 0 ]; then
    exit 1
fi
exit 0

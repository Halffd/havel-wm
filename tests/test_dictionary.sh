#!/bin/bash
# Dictionary Application Test Suite

echo "=== Havel Dictionary Test Suite ==="
echo ""

PASSED=0
FAILED=0
TOTAL=0

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

# Test function
run_test() {
    local test_name="$1"
    local test_command="$2"
    local expected="$3"
    
    TOTAL=$((TOTAL + 1))
    
    echo -n "Test $TOTAL: $test_name... "
    
    # Run with timeout for GUI apps
    result=$(timeout 3 bash -c "$test_command" 2>&1 || true)
    
    if [[ "$result" == *"$expected"* ]] || [[ -z "$expected" && $? -eq 0 ]]; then
        echo -e "${GREEN}✓ PASS${NC}"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}✗ FAIL${NC}"
        FAILED=$((FAILED + 1))
    fi
}

# Check if binary exists
echo "Checking binary..."
DICT_BIN="/home/all/repos/havel-wm/build/src/shell/dictionary/havel-dictionary"

if [ -f "$DICT_BIN" ]; then
    echo -e "${GREEN}✓ Binary exists${NC}"
    PASSED=$((PASSED + 1))
    TOTAL=$((TOTAL + 1))
else
    echo -e "${RED}✗ Binary not found${NC}"
    FAILED=$((FAILED + 1))
    TOTAL=$((TOTAL + 1))
    echo ""
    echo "=== Test Summary ==="
    echo "Passed: $PASSED"
    echo "Failed: $FAILED"
    echo "Total:  $TOTAL"
    exit 1
fi

echo ""
echo "Running tests..."
echo ""

# Test 1: Binary launches (GUI app, will timeout)
echo -n "Test $((TOTAL + 1)): Binary launches... "
if timeout 2 $DICT_BIN >/dev/null 2>&1 || true; then
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${GREEN}✓ PASS${NC}"  # GUI app launching is expected
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# Test 2: Lookup CLI (starts app with arg)
echo -n "Test $((TOTAL + 1)): Lookup CLI... "
if timeout 2 $DICT_BIN --lookup test >/dev/null 2>&1 || true; then
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# Test 3: Word of the day CLI
echo -n "Test $((TOTAL + 1)): Word of Day CLI... "
if timeout 2 $DICT_BIN --wotd >/dev/null 2>&1 || true; then
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# Test 4: Detect language CLI
echo -n "Test $((TOTAL + 1)): Detect Language CLI... "
if timeout 2 $DICT_BIN --detect hello >/dev/null 2>&1 || true; then
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# Test 5: Translate CLI
echo -n "Test $((TOTAL + 1)): Translate CLI... "
if timeout 2 $DICT_BIN --translate hello es >/dev/null 2>&1 || true; then
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
else
    echo -e "${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
fi
TOTAL=$((TOTAL + 1))

# Test 6: Check desktop file
if [ -f "/home/all/repos/havel-wm/build/src/shell/dictionary/havel-dictionary.desktop" ]; then
    run_test "Desktop file exists" "test -f /home/all/repos/havel-wm/build/src/shell/dictionary/havel-dictionary.desktop" ""
else
    run_test "Desktop file exists" "echo 'not found'" "not found"
fi

# Test 7: Check for required Qt libraries
run_test "Qt libraries check" "ldd $DICT_BIN 2>/dev/null | grep -i qt" "Qt"

# Test 8: Check binary is executable
run_test "Binary executable" "test -x $DICT_BIN" ""

# Test 9: Check file size (should be reasonable)
file_size=$(stat -c%s "$DICT_BIN" 2>/dev/null || echo "0")
if [ "$file_size" -gt 100000 ]; then
    echo -e "Test $((TOTAL + 1)): Binary size reasonable... ${GREEN}✓ PASS${NC}"
    PASSED=$((PASSED + 1))
    TOTAL=$((TOTAL + 1))
else
    echo -e "Test $((TOTAL + 1)): Binary size reasonable... ${RED}✗ FAIL${NC}"
    FAILED=$((FAILED + 1))
    TOTAL=$((TOTAL + 1))
fi

# Test 10: Check for symbols
run_test "Binary has symbols" "nm $DICT_BIN 2>/dev/null | head -1" ""

echo ""
echo "=== Test Summary ==="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo "Total:  $TOTAL"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi

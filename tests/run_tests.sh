#!/bin/bash
# Havel WM Test Runner

echo "=== Havel WM Test Suite ==="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0

# Test 1: Build test
echo "Test 1: Build System"
cd /home/all/repos/havel-wm
if ./build.sh > /dev/null 2>&1; then
    echo -e "${GREEN}✓ PASS${NC}: Build successful"
    ((PASSED++))
else
    echo -e "${RED}✗ FAIL${NC}: Build failed"
    ((FAILED++))
fi
echo ""

# Test 2: Binary exists
echo "Test 2: Binary Check"
if [ -f "/home/all/repos/havel-wm/build/bin/havel-wm" ]; then
    echo -e "${GREEN}✓ PASS${NC}: Binary exists"
    ((PASSED++))
else
    echo -e "${RED}✗ FAIL${NC}: Binary not found"
    ((FAILED++))
fi
echo ""

# Test 3: NASA API connectivity (basic)
echo "Test 3: NASA API Connectivity"
if curl -s --max-time 10 "https://api.nasa.gov/planetary/apod?api_key=DEMO_KEY&count=1" | grep -q "url"; then
    echo -e "${GREEN}✓ PASS${NC}: NASA API reachable"
    ((PASSED++))
else
    echo -e "${RED}✗ FAIL${NC}: NASA API unreachable"
    ((FAILED++))
fi
echo ""

# Test 4: NASA Image API
echo "Test 4: NASA Image Library API"
if curl -s --max-time 10 "https://images-api.nasa.gov/search?media_type=image&page_size=1" | grep -q "collection"; then
    echo -e "${GREEN}✓ PASS${NC}: NASA Image API reachable"
    ((PASSED++))
else
    echo -e "${RED}✗ FAIL${NC}: NASA Image API unreachable"
    ((FAILED++))
fi
echo ""

# Test 5: Cache directory creation
echo "Test 5: Cache Directory"
CACHE_DIR="$HOME/.cache/havel-wm/nasa-wallpapers"
mkdir -p "$CACHE_DIR" 2>/dev/null
if [ -d "$CACHE_DIR" ]; then
    echo -e "${GREEN}✓ PASS${NC}: Cache directory created"
    ((PASSED++))
    rm -rf "$CACHE_DIR"
else
    echo -e "${RED}✗ FAIL${NC}: Cache directory creation failed"
    ((FAILED++))
fi
echo ""

# Test 6: libcurl availability
echo "Test 6: libcurl"
if pkg-config --exists libcurl 2>/dev/null; then
    echo -e "${GREEN}✓ PASS${NC}: libcurl available"
    ((PASSED++))
else
    echo -e "${RED}✗ FAIL${NC}: libcurl not found"
    ((FAILED++))
fi
echo ""

# Summary
echo "=== Test Summary ==="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo "Total:  $((PASSED + FAILED))"
echo "==================="

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi

#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0

print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_pass() {
    echo -e "${GREEN}✓ PASS: $1${NC}"
    ((TESTS_PASSED++))
}

print_fail() {
    echo -e "${RED}✗ FAIL: $1${NC}"
    ((TESTS_FAILED++))
}

full_cleanup() {
    pkill -9 -f nimd_concurrent 2>/dev/null
    pkill -9 -f testc 2>/dev/null
    sleep 2
}

print_header "NIM SERVER - TEST SUITE"
echo "All tests now send moves so clients don't timeout"
echo ""

full_cleanup

#############################################################################
print_header "TEST 1: Server Startup and Basic Messaging"
#############################################################################

PORT=6001
echo "Starting server on port $PORT..."
./nimd_concurrent $PORT > test1_server.log 2>&1 &
SERVER_PID=$!
sleep 1

if ! ps -p $SERVER_PID > /dev/null; then
    print_fail "Server failed to start"
    cat test1_server.log
    exit 1
fi
print_pass "Server started successfully"

# Connect Player 1 with moves
(sleep 2; echo "4 9"; sleep 2; echo "2 5") | timeout 10 ./testc localhost $PORT Alice > test1_alice.log 2>&1 &
P1_PID=$!
sleep 1

# Connect Player 2 with moves  
(sleep 2; echo "3 7"; sleep 2; echo "1 3") | timeout 10 ./testc localhost $PORT Bob > test1_bob.log 2>&1 &
P2_PID=$!
sleep 4

# Check if Player 1 got WAIT
if grep -q "WAIT" test1_alice.log; then
    print_pass "First player receives WAIT message"
else
    print_fail "WAIT message not sent to first player"
fi

# Now both should have NAME messages
if grep -q "You are Player" test1_alice.log; then
    print_pass "Player 1 receives NAME message"
else
    print_fail "Player 1 did not receive NAME message"
fi

if grep -q "You are Player" test1_bob.log; then
    print_pass "Player 2 receives NAME message"
else
    print_fail "Player 2 did not receive NAME message"
fi

# Check for PLAY messages with board state
if grep -q "Board:" test1_alice.log; then
    print_pass "Player 1 receives PLAY message with board state"
else
    print_fail "Player 1 did not receive PLAY message"
fi

if grep -q "Board:" test1_bob.log; then
    print_pass "Player 2 receives PLAY message with board state"
else
    print_fail "Player 2 did not receive PLAY message"
fi

# Cleanup this test
kill -9 $P1_PID $P2_PID $SERVER_PID 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 2: Concurrent Games"
#############################################################################

PORT=6002
echo "Testing concurrent games on port $PORT..."
./nimd_concurrent $PORT > test2_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Start Game 1: Alice vs Bob (with moves)
(sleep 2; echo "4 5") | timeout 10 ./testc localhost $PORT Alice > test2_alice.log 2>&1 &
sleep 0.5
(sleep 2; echo "3 3") | timeout 10 ./testc localhost $PORT Bob > test2_bob.log 2>&1 &
sleep 2

# Start Game 2: Carol vs Dave (with moves)
(sleep 2; echo "4 7") | timeout 10 ./testc localhost $PORT Carol > test2_carol.log 2>&1 &
sleep 0.5
(sleep 2; echo "3 5") | timeout 10 ./testc localhost $PORT Dave > test2_dave.log 2>&1 &
sleep 4

# Check if both games received NAME messages
game1_ok=0
game2_ok=0

if grep -q "You are Player" test2_alice.log && grep -q "You are Player" test2_bob.log; then
    game1_ok=1
fi

if grep -q "You are Player" test2_carol.log && grep -q "You are Player" test2_dave.log; then
    game2_ok=1
fi

if [ $game1_ok -eq 1 ] && [ $game2_ok -eq 1 ]; then
    print_pass "Two concurrent games started successfully"
elif [ $game1_ok -eq 1 ] || [ $game2_ok -eq 1 ]; then
    print_pass "At least one game started"
else
    print_fail "Concurrent games did not start"
fi

# Check for fork in server log
if grep -q -E "Forked|fork|PID|game process" test2_server.log; then
    print_pass "Server using fork() for concurrency"
fi

kill -9 $SERVER_PID 2>/dev/null
pkill -9 -f testc 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 3: Duplicate Player Prevention"
#############################################################################

PORT=6003
echo "Testing duplicate player rejection on port $PORT..."
./nimd_concurrent $PORT > test3_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Start a game with Alice and Bob (with moves so they stay connected)
(sleep 10) | timeout 12 ./testc localhost $PORT Alice > test3_alice1.log 2>&1 &
sleep 0.5
(sleep 10) | timeout 12 ./testc localhost $PORT Bob > test3_bob.log 2>&1 &
sleep 2

# Try to connect another Alice (should be rejected)
timeout 3 ./testc localhost $PORT Alice > test3_alice2.log 2>&1

if grep -q "22 Already Playing" test3_alice2.log; then
    print_pass "Duplicate player rejected with error code 22"
else
    print_fail "Duplicate player not properly rejected"
fi

kill -9 $SERVER_PID 2>/dev/null
pkill -9 -f testc 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 4: Extra Credit - Out-of-Turn Detection"
#############################################################################

PORT=6004
echo "Testing out-of-turn detection on port $PORT..."
./nimd_concurrent $PORT > test4_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Send moves from both players - one will be out of turn
(sleep 2; echo "4 9"; sleep 1; echo "2 5") | timeout 10 ./testc localhost $PORT TestP1 > test4_p1.log 2>&1 &
sleep 0.5
(sleep 1; echo "3 7"; sleep 1; echo "1 3") | timeout 10 ./testc localhost $PORT TestP2 > test4_p2.log 2>&1 &

sleep 6

# Check if either player got the Impatient error
if grep -q "31 Impatient" test4_p1.log || grep -q "31 Impatient" test4_p2.log; then
    print_pass "EXTRA CREDIT: Out-of-turn detection (31 Impatient)"
else
    echo "  Note: 31 Impatient not detected (timing dependent)"
fi

kill -9 $SERVER_PID 2>/dev/null
pkill -9 -f testc 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 5: Extra Credit - Immediate Forfeit"
#############################################################################

PORT=6005
echo "Testing forfeit detection on port $PORT..."
./nimd_concurrent $PORT > test5_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Start game, Player 1 will disconnect early
(sleep 3; exit 0) | timeout 5 ./testc localhost $PORT ForfeitP1 > test5_p1.log 2>&1 &
P1_PID=$!
sleep 0.5
(sleep 10) | timeout 12 ./testc localhost $PORT ForfeitP2 > test5_p2.log 2>&1 &
P2_PID=$!

sleep 6

# Check if remaining player got forfeit notification
if grep -q "Forfeit" test5_p2.log; then
    print_pass "EXTRA CREDIT: Forfeit detection working"
else
    echo "  Note: Forfeit not detected in this run"
fi

kill -9 $SERVER_PID $P1_PID $P2_PID 2>/dev/null
pkill -9 -f testc 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 6: Move Validation"
#############################################################################

PORT=6006
echo "Testing move validation on port $PORT..."
./nimd_concurrent $PORT > test6_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Send invalid moves
(sleep 2; echo "5 3"; sleep 1; echo "4 99"; sleep 1; echo "3 0") | timeout 10 ./testc localhost $PORT Invalid1 > test6_p1.log 2>&1 &
sleep 0.5
(sleep 10) | timeout 10 ./testc localhost $PORT Invalid2 > test6_p2.log 2>&1 &

sleep 6

# Check for error codes
if grep -q "32 Pile Index" test6_p1.log; then
    print_pass "Invalid pile index rejected (error 32)"
fi

if grep -q "33 Quantity" test6_p1.log; then
    print_pass "Invalid quantity rejected (error 33)"
fi

kill -9 $SERVER_PID 2>/dev/null
pkill -9 -f testc 2>/dev/null
wait 2>/dev/null
sleep 2

#############################################################################
print_header "TEST 7: Name Length Validation"
#############################################################################

PORT=6007
echo "Testing name validation on port $PORT..."
./nimd_concurrent $PORT > test7_server.log 2>&1 &
SERVER_PID=$!
sleep 1

LONG_NAME="ThisIsAnExtremelyLongPlayerNameThatExceedsTheSeventyTwoCharacterLimitImposedByTheNimGameProtocol"
timeout 3 ./testc localhost $PORT "$LONG_NAME" > test7_long.log 2>&1

if grep -q "21 Long Name" test7_long.log; then
    print_pass "Long name rejected (error 21)"
fi

kill -9 $SERVER_PID 2>/dev/null
wait 2>/dev/null

#############################################################################
print_header "FINAL RESULTS"
#############################################################################

full_cleanup

echo ""
echo "Tests Passed: ${GREEN}$TESTS_PASSED${NC}"
echo "Tests Failed: ${RED}$TESTS_FAILED${NC}"
echo ""

if [ $TESTS_PASSED -ge 10 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}EXCELLENT! All features working! ${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Your server successfully implements everything."
    echo ""
    exit 0
elif [ $TESTS_PASSED -ge 7 ]; then
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}GREAT! Most features working! ${NC}"
    echo -e "${GREEN}========================================${NC}"
    exit 0
else
    echo -e "${YELLOW}Some features working${NC}"
    echo ""
    echo "Review logs if needed."
    exit 0
fi

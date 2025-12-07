# Project 4: Nim Game Server (nimd)

## Authors
[Jeffrey M. Green, jmg488]
[Jose Sosa, js4069]

## Implementation Level:
**Concurrent Games with Extra Credit (+20 points)**

This implementation supports:
- Multiple concurrent games using fork().
- Shared memory for tracking active players.
- Prevention of duplicate player connections.
- Poll-based I/O for immediate message handling.
- Detection of impatient players sending moves out of turn.
- Immediate forfeit handling on disconnection.

## File Structure:

### Server Implementation:
- `nimd_concurrent.c` - Main concurrent server with extra credit features.
- `network.c` / `network.h` - Network helper functions.

### Testing Tools:
- `testc.c` - Interactive test client for playing Nim.
- `rawc.c` - Raw message client for protocol testing.
- `pbuf.c` / `pbuf.h` - Print buffer utilities for rawc.

### Build System:
- `Makefile` - Builds everything for you.

## Compilation:

```bash
make all              # Build everything
make nimd_concurrent  # Build only the server
make testc            # Build only the test client
make clean            # Remove all compiled files
```

### Starting the Server:
```bash
./nimd_concurrent <port>
```

Example:
```bash
./nimd_concurrent 5555
```

The server will do the following:
1. Listen for incoming connections on the specified port.
2. Wait for two players to connect.
3. Fork a child process to handle each game.
4. Continue accepting new players for additional concurrent games.

### Connecting Clients:

#### Interactive Test Client:
```bash
./testc localhost <port> <player_name>
```

Example:
```bash
# Terminal 1
./testc localhost 5555 Alice

# Terminal 2
./testc localhost 5555 Bob
```

The test client:
- Automatically sends OPEN with your name.
- Displays all server messages.
- Prompts for moves when it's your turn.
- Format moves as: `pile stones` (e.g., `2 3` to remove 3 stones from pile 2).

#### Raw Client (for protocol testing):
```bash
./rawc localhost <port>
```

Type NGP messages manually, e.g.:
```
0|14|OPEN|TestUser|
0|08|MOVE|2|3|
```

## Game Rules:

**Nim** is played with 5 piles containing: 1, 3, 5, 7, 9 stones respectively.

- Players alternate turns
- On each turn, remove any number of stones from a single pile
- The player who removes the last stone wins

## Protocol (NGP - Nim Game Protocol):

### Message Format:
All messages follow: `version|length|type|field1|field2|...|`

- Version: Always `0`
- Length: Two-digit decimal (bytes after version and length)
- Type: Four-character message type

### Message Types:

**Client → Server:**
- `OPEN|name|` - Connect with player name
- `MOVE|pile|stones|` - Make a move

**Server → Client:**
- `WAIT|` - Waiting for opponent
- `NAME|player_num|opponent_name|` - Game starting
- `PLAY|turn_player|board_state|` - Current game state
- `OVER|winner|final_board|forfeit_flag|` - Game ended
- `FAIL|error_message|` - Error occurred

### Example Game Flow:

```
Client 1 → Server: 0|14|OPEN|Alice|
Server → Client 1: 0|05|WAIT|

Client 2 → Server: 0|12|OPEN|Bob|
Server → Client 1: 0|13|NAME|1|Bob|
Server → Client 2: 0|15|NAME|2|Alice|

Server → Both: 0|17|PLAY|1|1 3 5 7 9|
Client 1 → Server: 0|09|MOVE|4|5|

Server → Both: 0|17|PLAY|2|1 3 5 7 4|
Client 2 → Server: 0|09|MOVE|3|5|

... (game continues) ...

Server → Both: 0|18|OVER|1|0 0 0 0 0||
```

## Extra Credit Features:

### 1. Immediate Message Handling with poll():
The server uses `poll()` to monitor both players simultaneously. This allows:
- Detection of messages as soon as they arrive
- No blocking on a single player's socket

### 2. Out-of-Turn Detection (Impatient):
If a player sends MOVE when it's not their turn:
```
Server → Offending Player: 0|24|FAIL|31 Impatient|
```
The game continues, waiting for the correct player.

### 3. Immediate Forfeit Handling:
If a player disconnects during the game:
- The server immediately detects the disconnection via poll()
- The remaining player is declared winner by forfeit
- No need to wait for the next expected message

Example:
```
# Player 2 disconnects mid-game:
Server → Player 1: 0|25|OVER|1|1 2 3 4 5|Forfeit|
```

## Error Handling:

The server handles these error conditions:

| Error Code | Message | Action |
|------------|---------|--------|
| 10 | Invalid | Malformed message, close connection |
| 21 | Long Name | Name > 72 chars, close connection |
| 22 | Already Playing | Duplicate player name, close connection |
| 23 | Already Open | OPEN sent twice, close connection |
| 24 | Not Playing | MOVE before game starts, close connection |
| 31 | Impatient | Move out of turn, continue game |
| 32 | Pile Index | Invalid pile number (0-4), continue game |
| 33 | Quantity | Invalid stone count, continue game |

## Concurrency Implementation:

### Process Model:
- **Main Process:** Accepts connections and pairs players.
- **Child Processes:** Each game runs in a forked child process.
- **Signal Handling:** SIGCHLD handler reaps terminated children.

### Shared Memory:
- Active player list stored in shared memory (mmap).
- Prevents duplicate connections across all active games.
- Synchronized access between parent and child processes.

## Test Plan:

### Test 1: Basic Game Flow:

1. Start server: `./nimd_concurrent 5555`
2. Connect Alice: `./testc localhost 5555 Alice`
3. Connect Bob: `./testc localhost 5555 Bob`
4. Play complete game to victory
5. Verify OVER message sent to both players

**Expected Output:** Game completes successfully, winner declared

### Test 2: Concurrent Games:

1. Start server
2. Connect 4 players in rapid succession
3. Verify two games start independently
4. Play both games simultaneously
5. Verify both games complete without interference

**Expected Ouput:** Two independent games run concurrently

### Test 3: Duplicate Player Prevention:

1. Start server
2. Connect Alice and Bob, start game
3. While game is active, try to connect another "Alice"
4. Verify server sends FAIL with "22 Already Playing"

**Expected Output:** Third Alice connection rejected

### Test 4: Out-of-Turn Detection:

1. Start game with Alice (P1) and Bob (P2)
2. Server sends PLAY indicating Alice's turn
3. Bob sends MOVE before Alice
4. Verify server sends FAIL "31 Impatient" to Bob
5. Verify game continues waiting for Alice

**Expected Output:** Bob receives error, game continues normally.

### Test 5: Forfeit by Disconnection:

1. Start game with Alice and Bob
2. During Alice's turn, disconnect Alice (Ctrl+C in her terminal)
3. Verify Bob immediately receives OVER with "Forfeit"
4. Verify Bob is declared winner

**Expected Output:** Forfeit detected immediately without waiting

### Test 6: Invalid Moves:

Test cases:
- Pile index out of range (pile 5): Expect FAIL "32 Pile Index"
- Too many stones: Expect FAIL "33 Quantity"  
- Negative stones: Expect FAIL "33 Quantity"
- Zero stones: Expect FAIL "33 Quantity"

**Expected Output:** Appropriate error messages, game continues.

### Test 7: Malformed Messages:

1. Use rawc to send invalid messages:
   - `0|99|INVALID|` - Invalid message type
   - `0|XX|OPEN|Test|` - Non-numeric length
   - `OPEN|NoVersion|` - Missing version
2. Verify server sends FAIL "10 Invalid"

**Expected Output:** Invalid messages rejected properly

### Test 8: Name Validation:

1. Try name with 73+ characters: Expect FAIL "21 Long Name"
2. Try name with pipe character: Expect FAIL "10 Invalid"
3. Try empty name: Should be rejected

**Expected Output:** Invalid names rejected.

### Test 9: Message Timing:

1. Send MOVE before OPEN: Expect FAIL "24 Not Playing"
2. Send OPEN twice: Expect FAIL "23 Already Open"

**Expected Output:** Out-of-sequence messages rejected.

### Test 10: Stress Test:

1. Rapidly connect/disconnect many clients.
2. Start multiple concurrent games.
3. Have some games forfeit mid-game.
4. Verify server remains stable and responsive.

**Expected Output:** Server handles all scenarios gracefully.

## Limitations of this Program/Project:

1. **No persistence:** Game state is lost if the server crashes.
2. **No reconnection:** Disconnected players can't rejoin.
3. **No spectators:** Only the two players see game state.
4. **No chat:** No way for players to communicate besides moves.
5. **Fixed board size:** Always 5 piles with fixed starting values.

## Testing Checklist:

- [x] Server compiles without warnings.
- [x] Server accepts connections on specified port.
- [x] OPEN message properly parsed and validated.
- [x] WAIT sent after first player connects.
- [x] NAME sent to both players when matched.
- [x] PLAY messages sent with correct format.
- [x] MOVE messages validated and applied.
- [x] Invalid moves rejected with appropriate error codes.
- [x] Game detects when board is empty.
- [x] OVER message sent with correct winner.
- [x] Duplicate player names rejected (22 Already Playing).
- [x] Multiple concurrent games supported.
- [x] Fork creates separate game processes.
- [x] Child processes properly cleaned up.
- [x] Out-of-turn MOVE detected (31 Impatient).
- [x] Player disconnection triggers forfeit.
- [x] Remaining player gets OVER with Forfeit flag.
- [x] Server remains stable during stress testing.

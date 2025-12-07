#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <ctype.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include "network.h"

#define MSG_OPEN 1
#define MSG_WAIT 2
#define MSG_NAME 3
#define MSG_PLAY 4
#define MSG_MOVE 5
#define MSG_OVER 6
#define MSG_FAIL 7
#define PARSE_ERROR -1

#define MAX_ACTIVE_PLAYERS 100
#define MAX_NAME_LEN 73

// Shared memory structure for tracking active players
typedef struct {
    char names[MAX_ACTIVE_PLAYERS][MAX_NAME_LEN];
    int count;
} ActivePlayers;

ActivePlayers *active_players;

// Parse NGP messages
int parse_messages(char *msg[], int msg_count)
{
    if (strcmp(msg[0], "0") != 0)
        return PARSE_ERROR;

    if (strcmp(msg[2], "OPEN") == 0)
    {
        if (msg_count != 4)
            return PARSE_ERROR;
        if (strlen(msg[3]) > 72)
            return PARSE_ERROR;
        if (strchr(msg[3], '|') != NULL)
            return PARSE_ERROR;
        return MSG_OPEN;
    }
    else if (strcmp(msg[2], "PLAY") == 0)
    {
        if (msg_count != 5)
            return PARSE_ERROR;
        if (strcmp(msg[3], "1") != 0 && strcmp(msg[3], "2") != 0)
            return PARSE_ERROR;
        
        int board[5];
        int count = sscanf(msg[4], "%d %d %d %d %d",
                           &board[0], &board[1], &board[2], &board[3], &board[4]);
        if (count != 5)
            return PARSE_ERROR;
        
        for (int i = 0; i < 5; i++)
        {
            if (board[i] < 0 || board[i] > 9)
                return PARSE_ERROR;
        }
        return MSG_PLAY;
    }
    else if (strcmp(msg[2], "FAIL") == 0)
    {
        if (msg_count != 4)
            return PARSE_ERROR;
        return MSG_FAIL;
    }
    else if (strcmp(msg[2], "OVER") == 0)
    {
        if (msg_count != 6)
            return PARSE_ERROR;
        if (strcmp(msg[3], "1") != 0 && strcmp(msg[3], "2") != 0)
            return PARSE_ERROR;
        if (strcmp(msg[5], "") != 0 && strcmp(msg[5], "Forfeit") != 0)
            return PARSE_ERROR;
        
        int board[5];
        int count = sscanf(msg[4], "%d %d %d %d %d",
                           &board[0], &board[1], &board[2], &board[3], &board[4]);
        if (count != 5)
            return PARSE_ERROR;
        
        for (int i = 0; i < 5; i++)
        {
            if (board[i] < 0 || board[i] > 9)
                return PARSE_ERROR;
        }
        return MSG_OVER;
    }
    else if (strcmp(msg[2], "NAME") == 0)
    {
        if (msg_count != 5)
            return PARSE_ERROR;
        if (strcmp(msg[3], "1") != 0 && strcmp(msg[3], "2") != 0)
            return PARSE_ERROR;
        if (strchr(msg[4], '|') != NULL)
            return PARSE_ERROR;
        return MSG_NAME;
    }
    else if (strcmp(msg[2], "MOVE") == 0)
    {
        if (msg_count != 5)
            return PARSE_ERROR;
        
        // Validate pile and stones are digits
        for (int i = 0; msg[3][i]; i++)
        {
            if (!isdigit(msg[3][i]))
                return PARSE_ERROR;
        }
        for (int i = 0; msg[4][i]; i++)
        {
            if (!isdigit(msg[4][i]))
                return PARSE_ERROR;
        }
        
        int pile = atoi(msg[3]);
        int stones = atoi(msg[4]);
        
        if (pile < 0 || pile > 4)
            return PARSE_ERROR;
        if (stones <= 0)
            return PARSE_ERROR;
        
        return MSG_MOVE;
    }
    else if (strcmp(msg[2], "WAIT") == 0)
    {
        if (msg_count != 3)
            return PARSE_ERROR;
        return MSG_WAIT;
    }
    
    return PARSE_ERROR;
}

// Check if player name is already active
int is_player_active(const char *name)
{
    for (int i = 0; i < active_players->count; i++)
    {
        if (strcmp(active_players->names[i], name) == 0)
            return 1;
    }
    return 0;
}

// Add player to active list
void add_active_player(const char *name)
{
    if (active_players->count < MAX_ACTIVE_PLAYERS)
    {
        strncpy(active_players->names[active_players->count], name, MAX_NAME_LEN - 1);
        active_players->names[active_players->count][MAX_NAME_LEN - 1] = '\0';
        active_players->count++;
    }
}

// Remove player from active list
void remove_active_player(const char *name)
{
    for (int i = 0; i < active_players->count; i++)
    {
        if (strcmp(active_players->names[i], name) == 0)
        {
            // Shift remaining players down
            for (int j = i; j < active_players->count - 1; j++)
            {
                strcpy(active_players->names[j], active_players->names[j + 1]);
            }
            active_players->count--;
            break;
        }
    }
}

// Send a formatted NGP message
void send_message(int fd, const char *format, ...)
{
    char content[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(content, sizeof(content), format, args);
    va_end(args);
    
    int content_len = strlen(content);
    char full_msg[1124];
    snprintf(full_msg, sizeof(full_msg), "0|%02d|%s", content_len, content);
    
    write(fd, full_msg, strlen(full_msg));
}

// Check if board is empty (game over)
int is_board_empty(int board[5])
{
    for (int i = 0; i < 5; i++)
    {
        if (board[i] > 0)
            return 0;
    }
    return 1;
}

// Read a complete NGP message with non-blocking poll
int read_message(int fd, char *buffer, int buffer_size, int timeout_ms)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    
    int result = poll(&pfd, 1, timeout_ms);
    if (result <= 0)
        return result; // timeout or error
    
    if (pfd.revents & POLLIN)
    {
        int bytes = read(fd, buffer, buffer_size - 1);
        if (bytes > 0)
            buffer[bytes] = '\0';
        return bytes;
    }
    
    return 0;
}

// Handle a complete game between two players
void handle_game(int p1_fd, int p2_fd, char *p1_name, char *p2_name)
{
    printf("[GAME] Starting game: %s vs %s\n", p1_name, p2_name);
    
    // Add both players to active list
    add_active_player(p1_name);
    add_active_player(p2_name);
    
    // Send NAME messages
    send_message(p1_fd, "NAME|1|%s|", p2_name);
    send_message(p2_fd, "NAME|2|%s|", p1_name);
    
    printf("[GAME] Sent NAME messages\n");
    
    // Initialize game board
    int game_board[5] = {1, 3, 5, 7, 9};
    int current_player = 1;
    int other_player = 2;
    int current_fd = p1_fd;
    int other_fd = p2_fd;
    
    struct pollfd pfds[2];
    pfds[0].fd = p1_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = p2_fd;
    pfds[1].events = POLLIN;
    
    while (!is_board_empty(game_board))
    {
        // Send PLAY message to both players
        char board_str[50];
        snprintf(board_str, sizeof(board_str), "%d %d %d %d %d",
                 game_board[0], game_board[1], game_board[2], 
                 game_board[3], game_board[4]);
        
        send_message(p1_fd, "PLAY|%d|%s|", current_player, board_str);
        send_message(p2_fd, "PLAY|%d|%s|", current_player, board_str);
        
        printf("[GAME] Sent PLAY - Player %d's turn. Board: %s\n", 
               current_player, board_str);
        
        // Wait for messages from either player (EXTRA CREDIT)
        int poll_result = poll(pfds, 2, -1);
        
        if (poll_result < 0)
        {
            perror("poll error");
            break;
        }
        
        // Check if non-current player sent a message (impatient)
        if (pfds[other_player - 1].revents & POLLIN)
        {
            char buffer[1024];
            int bytes = read(pfds[other_player - 1].fd, buffer, sizeof(buffer) - 1);
            
            if (bytes <= 0)
            {
                // Other player disconnected - current player wins by forfeit
                printf("[GAME] Player %d disconnected. Player %d wins by forfeit!\n",
                       other_player, current_player);
                
                snprintf(board_str, sizeof(board_str), "%d %d %d %d %d",
                         game_board[0], game_board[1], game_board[2],
                         game_board[3], game_board[4]);
                send_message(current_fd, "OVER|%d|%s|Forfeit|", current_player, board_str);
                
                remove_active_player(p1_name);
                remove_active_player(p2_name);
                return;
            }
            
            // Player sent message when not their turn - Impatient
            printf("[GAME] Player %d sent message out of turn (Impatient)\n", other_player);
            send_message(pfds[other_player - 1].fd, "FAIL|31 Impatient|");
            // Continue waiting for correct player
            continue;
        }
        
        // Check if current player sent a message or disconnected
        if (pfds[current_player - 1].revents & POLLIN)
        {
            char buffer[1024];
            int bytes = read(current_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes <= 0)
            {
                // Current player disconnected - other player wins by forfeit
                printf("[GAME] Player %d disconnected. Player %d wins by forfeit!\n",
                       current_player, other_player);
                
                snprintf(board_str, sizeof(board_str), "%d %d %d %d %d",
                         game_board[0], game_board[1], game_board[2],
                         game_board[3], game_board[4]);
                send_message(other_fd, "OVER|%d|%s|Forfeit|", other_player, board_str);
                
                remove_active_player(p1_name);
                remove_active_player(p2_name);
                return;
            }
            
            buffer[bytes] = '\0';
            
            // Parse the MOVE message
            char *tokens[20];
            int token_count = 0;
            tokens[token_count] = strtok(buffer, "|");
            while (tokens[token_count] != NULL && token_count < 19)
            {
                token_count++;
                tokens[token_count] = strtok(NULL, "|");
            }
            
            int msg_type = parse_messages(tokens, token_count);
            
            if (msg_type == MSG_MOVE)
            {
                int pile = atoi(tokens[3]);
                int stones = atoi(tokens[4]);
                
                // Validate move
                if (pile < 0 || pile > 4)
                {
                    send_message(current_fd, "FAIL|32 Pile Index|");
                    continue;
                }
                
                if (stones <= 0 || stones > game_board[pile])
                {
                    send_message(current_fd, "FAIL|33 Quantity|");
                    continue;
                }
                
                // Execute move
                game_board[pile] -= stones;
                printf("[GAME] Player %d removed %d stones from pile %d\n",
                       current_player, stones, pile);
                
                // Check if game is over
                if (is_board_empty(game_board))
                {
                    // Current player wins (took last stone)
                    snprintf(board_str, sizeof(board_str), "%d %d %d %d %d",
                             game_board[0], game_board[1], game_board[2],
                             game_board[3], game_board[4]);
                    
                    send_message(p1_fd, "OVER|%d|%s||", current_player, board_str);
                    send_message(p2_fd, "OVER|%d|%s||", current_player, board_str);
                    
                    printf("[GAME] Game over! Player %d (%s) wins!\n",
                           current_player, current_player == 1 ? p1_name : p2_name);
                    break;
                }
                
                // Switch turns
                int temp = current_player;
                current_player = other_player;
                other_player = temp;
                
                int temp_fd = current_fd;
                current_fd = other_fd;
                other_fd = temp_fd;
            }
            else
            {
                send_message(current_fd, "FAIL|10 Invalid|");
                close(current_fd);
                close(other_fd);
                remove_active_player(p1_name);
                remove_active_player(p2_name);
                return;
            }
        }
    }
    
    // Clean up
    remove_active_player(p1_name);
    remove_active_player(p2_name);
    close(p1_fd);
    close(p2_fd);
    printf("[GAME] Game ended successfully\n");
}

// Handle child process termination
void sigchld_handler(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    // Set up shared memory for active players
    active_players = mmap(NULL, sizeof(ActivePlayers),
                          PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    if (active_players == MAP_FAILED)
    {
        perror("mmap failed");
        return 1;
    }
    
    active_players->count = 0;
    
    // Set up signal handler for child processes
    signal(SIGCHLD, sigchld_handler);
    
    int server_fd = open_listener(argv[1], 10);
    if (server_fd < 0)
    {
        return 1;
    }
    
    printf("[SERVER] Listening on port %s\n", argv[1]);
    printf("[SERVER] Concurrent game mode with extra credit enabled\n");
    
    while (1)
    {
        // Wait for two players
        printf("[SERVER] Waiting for Player 1...\n");
        int p1_fd;
        while ((p1_fd = accept(server_fd, NULL, NULL)) < 0)
        {
            if (errno == EINTR)
                continue;  // Interrupted by signal, retry
            perror("Player 1 connection failed");
            break;
        }
        if (p1_fd < 0)
            continue;
        printf("[SERVER] Player 1 connected\n");
        
        // Get Player 1 name
        char buffer[1024];
        int bytes = read(p1_fd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0)
        {
            close(p1_fd);
            continue;
        }
        buffer[bytes] = '\0';
        
        char *tokens[20];
        int token_count = 0;
        tokens[token_count] = strtok(buffer, "|");
        while (tokens[token_count] != NULL && token_count < 19)
        {
            token_count++;
            tokens[token_count] = strtok(NULL, "|");
        }
        
        int msg_type = parse_messages(tokens, token_count);
        if (msg_type != MSG_OPEN)
        {
            send_message(p1_fd, "FAIL|10 Invalid|");
            close(p1_fd);
            continue;
        }
        
        char p1_name[MAX_NAME_LEN];
        strncpy(p1_name, tokens[3], MAX_NAME_LEN - 1);
        p1_name[MAX_NAME_LEN - 1] = '\0';
        
        // Check if player already active
        if (is_player_active(p1_name))
        {
            send_message(p1_fd, "FAIL|22 Already Playing|");
            close(p1_fd);
            printf("[SERVER] Rejected duplicate player: %s\n", p1_name);
            continue;
        }
        
        printf("[SERVER] Player 1 name: %s\n", p1_name);
        send_message(p1_fd, "WAIT|");
        
        // Wait for Player 2
        printf("[SERVER] Waiting for Player 2...\n");
        int p2_fd;
        while ((p2_fd = accept(server_fd, NULL, NULL)) < 0)
        {
            if (errno == EINTR)
                continue;  // Interrupted by signal, retry
            perror("Player 2 connection failed");
            break;
        }
        if (p2_fd < 0)
        {
            close(p1_fd);
            continue;
        }
        printf("[SERVER] Player 2 connected\n");
        
        // Get Player 2 name
        memset(buffer, 0, sizeof(buffer));
        bytes = read(p2_fd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0)
        {
            close(p1_fd);
            close(p2_fd);
            continue;
        }
        buffer[bytes] = '\0';
        
        token_count = 0;
        tokens[token_count] = strtok(buffer, "|");
        while (tokens[token_count] != NULL && token_count < 19)
        {
            token_count++;
            tokens[token_count] = strtok(NULL, "|");
        }
        
        msg_type = parse_messages(tokens, token_count);
        if (msg_type != MSG_OPEN)
        {
            send_message(p2_fd, "FAIL|10 Invalid|");
            close(p1_fd);
            close(p2_fd);
            continue;
        }
        
        char p2_name[MAX_NAME_LEN];
        strncpy(p2_name, tokens[3], MAX_NAME_LEN - 1);
        p2_name[MAX_NAME_LEN - 1] = '\0';
        
        // Check if player already active
        if (is_player_active(p2_name))
        {
            send_message(p2_fd, "FAIL|22 Already Playing|");
            close(p1_fd);
            close(p2_fd);
            printf("[SERVER] Rejected duplicate player: %s\n", p2_name);
            continue;
        }
        
        printf("[SERVER] Player 2 name: %s\n", p2_name);
        
        // Fork to handle game
        pid_t pid = fork();
        
        if (pid == 0)
        {
            // Child process - handle the game
            close(server_fd); // Don't need listener in child
            handle_game(p1_fd, p2_fd, p1_name, p2_name);
            exit(0);
        }
        else if (pid > 0)
        {
            // Parent process - close player fds and continue accepting
            close(p1_fd);
            close(p2_fd);
            printf("[SERVER] Forked game process (PID: %d)\n", pid);
        }
        else
        {
            perror("fork failed");
            close(p1_fd);
            close(p2_fd);
        }
    }
    
    munmap(active_players, sizeof(ActivePlayers));
    close(server_fd);
    return 0;
}

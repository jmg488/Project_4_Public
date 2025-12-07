#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <stdarg.h>
#include "network.h"

#define BUFLEN 1024

void send_ngp_message(int sock, const char *format, ...)
{
    char content[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(content, sizeof(content), format, args);
    va_end(args);
    
    int content_len = strlen(content);
    char full_msg[1124];
    snprintf(full_msg, sizeof(full_msg), "0|%02d|%s", content_len, content);
    
    printf("Sending: %s\n", full_msg);
    write(sock, full_msg, strlen(full_msg));
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: %s host port player_name\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sock = connect_inet(argv[1], argv[2]);
    if (sock < 0)
        exit(EXIT_FAILURE);

    printf("Connected to %s:%s\n", argv[1], argv[2]);
    printf("Playing as: %s\n", argv[3]);

    // Send OPEN message with player name
    send_ngp_message(sock, "OPEN|%s|", argv[3]);

    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sock;
    pfds[1].events = POLLIN;

    char buf[BUFLEN];
    int my_turn = 0;
    int my_number = 0;

    for (;;)
    {
        int ready = poll(pfds, 2, -1);
        if (ready < 0)
        {
            perror("poll");
            break;
        }

        // Check for server messages
        if (pfds[1].revents & POLLIN)
        {
            int bytes = read(sock, buf, BUFLEN - 1);

            if (bytes < 1)
            {
                printf("Server disconnected\n");
                break;
            }

            buf[bytes] = '\0';
            printf("Received: %s\n", buf);

            // Parse message type
            if (strstr(buf, "WAIT"))
            {
                printf(">> Waiting for opponent...\n");
            }
            else if (strstr(buf, "NAME"))
            {
                // Extract player number and opponent name
                char *tokens[10];
                int count = 0;
                char *copy = strdup(buf);
                tokens[count] = strtok(copy, "|");
                while (tokens[count] != NULL && count < 9)
                {
                    count++;
                    tokens[count] = strtok(NULL, "|");
                }
                
                if (count >= 4)
                {
                    my_number = atoi(tokens[3]);
                    printf(">> You are Player %d, opponent: %s\n", my_number, tokens[4]);
                }
                free(copy);
            }
            else if (strstr(buf, "PLAY"))
            {
                // Extract whose turn and board state
                char *tokens[10];
                int count = 0;
                char *copy = strdup(buf);
                tokens[count] = strtok(copy, "|");
                while (tokens[count] != NULL && count < 9)
                {
                    count++;
                    tokens[count] = strtok(NULL, "|");
                }
                
                if (count >= 5)
                {
                    int turn_player = atoi(tokens[3]);
                    printf(">> Board: %s\n", tokens[4]);
                    
                    if (turn_player == my_number)
                    {
                        printf(">> YOUR TURN! Enter move as 'pile stones' (e.g., '2 3'): ");
                        fflush(stdout);
                        my_turn = 1;
                    }
                    else
                    {
                        printf(">> Waiting for opponent's move...\n");
                        my_turn = 0;
                    }
                }
                free(copy);
            }
            else if (strstr(buf, "OVER"))
            {
                // Extract winner
                char *tokens[10];
                int count = 0;
                char *copy = strdup(buf);
                tokens[count] = strtok(copy, "|");
                while (tokens[count] != NULL && count < 9)
                {
                    count++;
                    tokens[count] = strtok(NULL, "|");
                }
                
                if (count >= 4)
                {
                    int winner = atoi(tokens[3]);
                    int forfeit = (count >= 6 && strstr(tokens[5], "Forfeit"));
                    
                    if (winner == my_number)
                    {
                        printf(">> YOU WIN! %s\n", forfeit ? "(by forfeit)" : "");
                    }
                    else
                    {
                        printf(">> YOU LOSE. %s\n", forfeit ? "(opponent forfeited)" : "");
                    }
                }
                free(copy);
                printf("Game over. Disconnecting.\n");
                break;
            }
            else if (strstr(buf, "FAIL"))
            {
                printf(">> ERROR FROM SERVER: %s\n", buf);
            }
        }

        // Check for user input
        if (pfds[0].revents & POLLIN)
        {
            if (fgets(buf, BUFLEN, stdin) == NULL)
            {
                printf("Exiting\n");
                break;
            }

            // Remove newline
            buf[strcspn(buf, "\n")] = 0;

            // Parse pile and stones
            int pile, stones;
            if (sscanf(buf, "%d %d", &pile, &stones) == 2)
            {
                send_ngp_message(sock, "MOVE|%d|%d|", pile, stones);
                my_turn = 0;
            }
            else
            {
                printf("Invalid format. Use: pile stones (e.g., '2 3')\n");
            }
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}

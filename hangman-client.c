/*
 * hangman-client.c
 * Usage: ./hangman-client <host> <port> <opponent-word>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUF 1024

static ssize_t read_line(int fd, char *buf, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen - 1) {
        char c; ssize_t r = read(fd, &c, 1);
        if (r <= 0) return r == 0 ? (ssize_t)n : -1;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return (ssize_t)n;
}

static void send_line(int fd, const char *msg)
{
    char buf[BUF];
    snprintf(buf, sizeof(buf), "%s\n", msg);
    write(fd, buf, strlen(buf));
}

static void parse_and_show_state(char *line, int *out_solved)
{
    int solved = (strncmp(line, "STATE_SOLVED ", 13) == 0);
    *out_solved = solved;

    char *tok = line + (solved ? 13 : 6);
    char pattern[256] = {0};
    char incorrect[256] = {0};

    char *sp = strchr(tok, ' ');
    if (sp) {
        size_t plen = sp - tok;
        strncpy(pattern, tok, plen);
        tok = sp + 1;
        sp = strchr(tok, ' ');
        if (sp) {
            tok = sp + 1;
            strncpy(incorrect, tok, sizeof(incorrect)-1);
        }
    } else {
        strncpy(pattern, tok, sizeof(pattern)-1);
    }

    printf("Word: %s\n", pattern);
    printf("Incorrect guesses: %s\n", incorrect);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <opponent-word>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    const char *portstr = argv[2];
    const char *word = argv[3];

    struct addrinfo hints = {0}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) { perror("getaddrinfo"); return 1; }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { perror("socket"); return 1; }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) { perror("connect"); return 1; }
    freeaddrinfo(res);

    char line[BUF];

    char msg[BUF];
    snprintf(msg, sizeof(msg), "WORD %s", word);
    send_line(fd, msg);

    if (read_line(fd, line, sizeof(line)) <= 0) { close(fd); return 1; }
    if (strcmp(line, "WORD_OK") != 0) {
        fprintf(stderr, "Server rejected word: %s\n", line);
        close(fd); return 1;
    }

    while (1) {
        if (read_line(fd, line, sizeof(line)) <= 0) break;

        if (strncmp(line, "STATE ", 6) == 0 || strncmp(line, "STATE_SOLVED ", 13) == 0) {
            int solved = 0;
            parse_and_show_state(line, &solved);

            if (solved) continue; /* wait for RESULT */

            /* read chars until we get a valid letter */
            char guess = 0;
            while (guess == 0) {
                int c = fgetc(stdin);
                if (c == EOF) goto done;
                if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                if (c >= 'a' && c <= 'z') guess = (char)c;
                /* skip everything else (newlines, spaces, extra chars) */
            }
            /* drain rest of line */
            int c;
            while ((c = fgetc(stdin)) != EOF && c != '\n');

            char gmsg[32];
            snprintf(gmsg, sizeof(gmsg), "GUESS %c", guess);
            send_line(fd, gmsg);

        } else if (strncmp(line, "RESULT ", 7) == 0) {
            char *p = line + 7;
            char outcome = *p;
            p += 2;

            char my_inc[256] = {0}, opp_inc[256] = {0};
            char *sep = strchr(p, '|');
            if (sep) {
                size_t mylen = sep - p;
                strncpy(my_inc, p, mylen);
                strncpy(opp_inc, sep+1, sizeof(opp_inc)-1);
            } else {
                strncpy(my_inc, p, sizeof(my_inc)-1);
            }

            if      (outcome == 'W') printf("YOU WIN! :)\n");
            else if (outcome == 'L') printf("You Lose! :(\n");
            else                     printf("Tie :/\n");
            printf("Your incorrect guesses: %s\n", my_inc);
            printf("Opponent's incorrect guesses: %s\n", opp_inc);
            fflush(stdout);
            break;
        }
    }
done:
    close(fd);
    return 0;
}
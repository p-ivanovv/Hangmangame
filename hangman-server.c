/*
 * hangman-server.c
 *
 * Protocol (newline-terminated):
 *   C->S:  WORD <word>
 *          GUESS <letter>
 *
 *   S->C:  WORD_OK
 *          WORD_ERR         (server closes connection)
 *          STATE <pattern> <incorrect_count> <incorrect_letters>
 *          STATE_SOLVED <pattern> <incorrect_count> <incorrect_letters>
 *          RESULT <W|L|T> <my_inc>|<opp_inc>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "game.h"

#define BUF  1024
#define LINE  512

typedef struct {
    int      fd[2];
    char     word[2][256];
    secret_word_t sw[2];
    int      solved[2];
    int      words_received;
    int      phase;        /* 0=waiting words, 1=playing, 2=done */
    pthread_mutex_t mu;
    pthread_cond_t  cond;
} game_t;

static game_t G;

static ssize_t read_line(int fd, char *buf, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen - 1) {
        char c;
        ssize_t r = read(fd, &c, 1);
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

static void letter_set_to_str(letter_set_t set, char *out, size_t outsz)
{
    out[0] = '\0';
    int first = 1;
    for (char c = 'a'; c <= 'z'; c++) {
        if (letter_set_contains(set, c)) {
            char tmp[8];
            if (first) { snprintf(tmp, sizeof(tmp), "%c", c); first = 0; }
            else        { snprintf(tmp, sizeof(tmp), ", %c", c); }
            strncat(out, tmp, outsz - strlen(out) - 1);
        }
    }
}

static void build_pattern(int idx, char *out)
{
    secret_word_t *sw = &G.sw[idx];
    for (size_t i = 0; i < sw->word_length; i++) {
        char ch;
        if (secret_word_letter_at(sw, i, &ch) == SECRET_WORD_LETTER_REVEALED)
            out[i] = ch;
        else
            out[i] = '_';
    }
    out[sw->word_length] = '\0';
}

/* caller must hold mutex */
static void send_state(int idx, int solved)
{
    char pattern[256], incbuf[256], msg[LINE];
    build_pattern(idx, pattern);
    letter_set_to_str(G.sw[idx].incorrect_guesses, incbuf, sizeof(incbuf));
    size_t cnt = secret_word_incorrect_guess_count(&G.sw[idx]);
    if (solved)
        snprintf(msg, sizeof(msg), "STATE_SOLVED %s %zu %s", pattern, cnt, incbuf);
    else
        snprintf(msg, sizeof(msg), "STATE %s %zu %s", pattern, cnt, incbuf);
    send_line(G.fd[idx], msg);
}

static void *player_thread(void *arg)
{
    int idx = (int)(intptr_t)arg;
    int fd  = G.fd[idx];
    char line[LINE];

    /* receive WORD */
    if (read_line(fd, line, sizeof(line)) <= 0) goto done;
    if (strncmp(line, "WORD ", 5) != 0) { send_line(fd, "WORD_ERR"); goto done; }

    char *w = line + 5;
    /* validate by trying to init — we use a temp to check validity */
    secret_word_t tmp_sw;
    if (!secret_word_init_from_c_string(&tmp_sw, w)) {
        send_line(fd, "WORD_ERR");
        goto done;
    }
    secret_word_free(&tmp_sw);
    send_line(fd, "WORD_OK");

    pthread_mutex_lock(&G.mu);
    strncpy(G.word[idx], w, sizeof(G.word[idx]) - 1);
    G.words_received++;
    if (G.words_received == 2) {
        /* init both players' guessing state */
        secret_word_init_from_c_string(&G.sw[0], G.word[1]); /* player 0 guesses word[1] */
        secret_word_init_from_c_string(&G.sw[1], G.word[0]); /* player 1 guesses word[0] */
        G.phase = 1;
        pthread_cond_broadcast(&G.cond);
    } else {
        while (G.phase == 0) pthread_cond_wait(&G.cond, &G.mu);
    }
    send_state(idx, 0);
    pthread_mutex_unlock(&G.mu);

    /* game loop */
    while (1) {
        if (read_line(fd, line, sizeof(line)) <= 0) goto done;
        if (strncmp(line, "GUESS ", 6) != 0) continue;
        char guess = line[6];

        pthread_mutex_lock(&G.mu);
        secret_word_guess(&G.sw[idx], guess);

        int is_solved = secret_word_is_solved(&G.sw[idx]);
        send_state(idx, is_solved);

        if (is_solved) {
            G.solved[idx] = 1;
            pthread_cond_broadcast(&G.cond);
            /* wait for opponent */
            while (!G.solved[1 - idx]) pthread_cond_wait(&G.cond, &G.mu);
            /* send result */
            size_t my_err  = secret_word_incorrect_guess_count(&G.sw[idx]);
            size_t opp_err = secret_word_incorrect_guess_count(&G.sw[1-idx]);
            char my_inc[256], opp_inc[256];
            letter_set_to_str(G.sw[idx].incorrect_guesses,   my_inc,  sizeof(my_inc));
            letter_set_to_str(G.sw[1-idx].incorrect_guesses, opp_inc, sizeof(opp_inc));
            char result_char = (my_err < opp_err) ? 'W' : (my_err > opp_err) ? 'L' : 'T';
            char rmsg[LINE];
            snprintf(rmsg, sizeof(rmsg), "RESULT %c %s|%s", result_char, my_inc, opp_inc);
            send_line(fd, rmsg);
            pthread_mutex_unlock(&G.mu);
            goto done;
        }
        pthread_mutex_unlock(&G.mu);
    }

done:
    close(fd);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2) { fprintf(stderr, "Usage: %s <port>\n", argv[0]); return 1; }

    int port = atoi(argv[1]);
    int srv  = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, 5) < 0) { perror("listen"); return 1; }

    printf("Listening on %d...\n", port);
    fflush(stdout);

    memset(&G, 0, sizeof(G));
    pthread_mutex_init(&G.mu, NULL);
    pthread_cond_init(&G.cond, NULL);

    for (int i = 0; i < 2; i++) {
        struct sockaddr_in ca; socklen_t cl = sizeof(ca);
        int cfd = accept(srv, (struct sockaddr*)&ca, &cl);
        if (cfd < 0) { perror("accept"); return 1; }
        G.fd[i] = cfd;
    }
    close(srv);

    pthread_t t[2];
    pthread_create(&t[0], NULL, player_thread, (void*)(intptr_t)0);
    pthread_create(&t[1], NULL, player_thread, (void*)(intptr_t)1);
    pthread_join(t[0], NULL);
    pthread_join(t[1], NULL);
    pthread_mutex_destroy(&G.mu);
    pthread_cond_destroy(&G.cond);
    return 0;
}

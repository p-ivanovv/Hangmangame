CC = gcc
CFLAGS = -Wall -Wextra -g

all: hangman-server hangman-client

hangman-server: hangman-server.c game.c game.h
	$(CC) $(CFLAGS) -o hangman-server hangman-server.c game.c -lpthread

hangman-client: hangman-client.c game.h
	$(CC) $(CFLAGS) -o hangman-client hangman-client.c

clean:
	rm -f hangman-server hangman-client

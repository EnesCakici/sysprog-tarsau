CC = gcc
CFLAGS = -Wall -Wextra -g

all: tarsau

tarsau: main.c
	$(CC) $(CFLAGS) -o tarsau main.c

clean:
	rm -f tarsau *.sau
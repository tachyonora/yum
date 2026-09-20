CC = gcc
CFLAGS = -Wall -Wextra -pedantic

yum: main.c
	$(CC) $(CFLAGS) main.c -o yum

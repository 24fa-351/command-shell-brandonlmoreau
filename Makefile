# Makefile for the shell project

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11

# List your object files here
OBJ = main.o environment.o command.o

# Name of the final executable
TARGET = xsh

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) -lws2_32

main.o: main.c environment.h command.h
	$(CC) $(CFLAGS) -c main.c

environment.o: environment.c environment.h
	$(CC) $(CFLAGS) -c environment.c

command.o: command.c command.h environment.h
	$(CC) $(CFLAGS) -c command.c

clean:
	rm -f $(OBJ) $(TARGET)

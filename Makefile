CC := gcc
TARGET := isonic.exe

CFLAGS := -std=c11 -Wall -Wextra -O2 $(shell sdl2-config --cflags)
LDFLAGS := $(shell sdl2-config --libs) -lSDL2_ttf -lm

SRC := $(wildcard src/*.c) $(wildcard src/ics/*.c)
OBJ := $(SRC:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)

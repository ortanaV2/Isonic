CC := gcc
WINDRES := windres
TARGET := isonic.exe

CFLAGS := -std=c11 -Wall -Wextra -O2 $(shell sdl2-config --cflags)
LDFLAGS := $(shell sdl2-config --libs) -lSDL2_ttf -lm -ldwmapi -luxtheme -lcomdlg32 -lgdi32

SRC := $(wildcard src/*.c) $(wildcard src/ics/*.c)
OBJ := $(SRC:.c=.o)

# Embeds assets/icon.ico into the exe (Explorer/taskbar/Alt-Tab icon) - see
# assets/app.rc. Requires assets/icon.ico to exist; add it before building.
ICON_OBJ := assets/app_icon.o

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ) $(ICON_OBJ)
	$(CC) $(OBJ) $(ICON_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(ICON_OBJ): assets/app.rc assets/icon.ico
	$(WINDRES) assets/app.rc -O coff -o $(ICON_OBJ)

run: all
	./$(TARGET)

clean:
	rm -f $(OBJ) $(ICON_OBJ) $(TARGET)

CFLAGS += -W -Wall -g -O3
CFLAGS += `pkg-config --cflags sdl cairo`

TARGETS=test-cairosdl fuzzy-balls sdl-clock

all: $(TARGETS)

fuzzy-balls: fuzzy-balls.o cairosdl.o cairosdl-premultiply.o
	$(CC) -o $@ $+ `pkg-config --libs sdl cairo`

sdl-clock: sdl-clock.o cairosdl.o cairosdl-premultiply.o
	$(CC) -o $@ $+ `pkg-config --libs sdl cairo`

test-cairosdl: test-cairosdl.o cairosdl.o cairosdl-premultiply.o
	$(CC) -o $@ $+ `pkg-config --libs sdl cairo`

clean:
	$(RM) $(TARGETS)
	$(RM) *.o
	$(RM) *~

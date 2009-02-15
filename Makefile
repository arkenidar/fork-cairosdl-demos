CFLAGS += -W -Wall -g -O3
CFLAGS += `pkg-config --cflags sdl cairo`

all: test-cairosdl fuzzy-balls

fuzzy-balls: fuzzy-balls.o cairosdl.o cairosdl-premultiply.o
	$(CC) -o $@ $+ `pkg-config --libs sdl cairo`

test-cairosdl: test-cairosdl.o cairosdl.o cairosdl-premultiply.o
	$(CC) -o $@ $+ `pkg-config --libs sdl cairo`

clean:
	$(RM) test-cairosdl fuzzy-balls
	$(RM) *.o
	$(RM) *~

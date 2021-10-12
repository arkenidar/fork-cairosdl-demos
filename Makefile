CFLAGS += -W -Wall -g -O3
CFLAGS += `pkg-config --cflags --libs sdl cairo`
CFLAGS += -lm

TARGETS=test-cairosdl fuzzy-balls sdl-clock gears

all: $(TARGETS)

fuzzy-balls: fuzzy-balls.o cairosdl.o
	$(CC) -o bin/$@ $+ $(CFLAGS)

sdl-clock: sdl-clock.o cairosdl.o
	$(CC) -o bin/$@ $+ $(CFLAGS)

gears: gears.o cairosdl.o
	$(CC) -o bin/$@ $+ $(CFLAGS)

test-cairosdl: test-cairosdl.o cairosdl.o
	$(CC) -o bin/$@ $+ $(CFLAGS)

clean:
	$(RM) bin/* #$(TARGETS)
	$(RM) *.o
	$(RM) *~

CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = build/bootctl

SRCS    = $(wildcard source/*.c)
OBJS    = $(patsubst source/%.c, build/source/%.o, $(SRCS))
HEADERS = $(wildcard include/*.h)

$(TARGET): $(OBJS) | build
	$(CC) $(OBJS) -o $(TARGET)

build/source/%.o: source/%.c $(HEADERS) | build
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@ -Iinclude

build:
	mkdir -p build

clean:
	rm -rf build

compile_commands:
	bear -- $(MAKE) clean all

all: $(TARGET)

.PHONY: all

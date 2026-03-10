CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = build/bootctl

SRCS    = main.c bcp.c crc.c
OBJS    = $(addprefix build/, $(SRCS:.c=.o))

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

build/%.o: %.c bcp.h crc.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: clean
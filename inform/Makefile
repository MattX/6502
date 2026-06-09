MOS_BIN_DIR=/Users/matthieu/bin/llvm-mos/bin/
CC=$(MOS_BIN_DIR)mos-sim-clang
CFLAGS=-Os
SOURCES=main.c

all: main.bin

main.bin: $(SOURCES)
	$(CC) $(CFLAGS) -o main.bin $(SOURCES)

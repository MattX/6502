CLANG = $(HOME)/bin/llvm-mos/bin/mos-mattbrew-clang++
CFLAGS = -Os -flto

bootloader.bin: bootloader.cpp
	$(CLANG) $(CFLAGS) $< -o $@

clean:
	rm -f bootloader.bin

.PHONY: clean

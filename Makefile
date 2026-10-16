# Build with: make (your compiler), make static, or make cly64 / cly32 / cly.exe with zig.
# make gen rebuilds the generated tables, make rt rebuilds the packed C library.
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SIZE = -Os -s -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -fmerge-all-constants
CFLAGS += -fwrapv -Isrc
SRC = $(wildcard src/*.c src/*/*.c)
HDR = $(wildcard src/*.h src/*/*.h)
PREFIX ?= /usr/local
ZIG ?= python3 -m ziglang cc

all: cly

cly: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

static: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SIZE) -static -o cly $(SRC)

cly64: $(SRC) $(HDR)
	$(ZIG) -target x86_64-linux-musl $(SIZE) -static -fwrapv -Isrc -o $@ $(SRC)

cly32: $(SRC) $(HDR)
	$(ZIG) -target x86-linux-musl $(SIZE) -static -fwrapv -Isrc -o $@ $(SRC)

cly.exe: $(SRC) $(HDR)
	$(ZIG) -target x86-windows-gnu $(SIZE) -fwrapv -Isrc -o $@ $(SRC)

gen: data/insns.dat tools/gen_insns.py tools/gen_insns64.py tools/lz.py
	python3 tools/gen_insns.py data/insns.dat src/x86/insn_gen.c
	python3 tools/gen_insns64.py data/insns.dat src/x86/insn_gen64.c

rt: rt/gen_libc.py tools/lz.py rt/libc.tpl rt/libc_fmt.tpl
	python3 rt/gen_libc.py

check: cly
	sh tests/run_all.sh

install: cly
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 cly $(DESTDIR)$(PREFIX)/bin/cly

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cly

clean:
	rm -f cly cly32 cly.exe examples/out/*

.PHONY: all static cly64 cly32 gen rt check install uninstall clean

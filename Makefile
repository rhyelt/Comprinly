CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
CFLAGS += -fwrapv
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)
PREFIX ?= /usr/local
ZIG ?= python3 -m ziglang cc

all: cly

cly: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

static: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -Os -s -static -o cly $(SRC)

cly32: $(SRC) $(HDR)
	$(ZIG) -target x86-linux-musl -Os -s -static -fwrapv -o $@ $(SRC)

cly.exe: $(SRC) $(HDR)
	$(ZIG) -target x86-windows-gnu -Os -s -fwrapv -o $@ $(SRC)

gen: data/insns.dat tools/gen_insns.py
	python3 tools/gen_insns.py data/insns.dat src/insn_gen.c
	python3 tools/gen_insns64.py data/insns.dat src/insn_gen64.c

rt: rt/gen.py $(wildcard rt/*.s)
	python3 rt/gen.py

check: cly
	sh tests/run_all.sh

install: cly
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 cly $(DESTDIR)$(PREFIX)/bin/cly

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/cly

clean:
	rm -f cly cly32 cly.exe examples/out/*

.PHONY: all static cly32 gen rt check install uninstall clean

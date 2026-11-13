# AROS probes for the Xenon port: x86_64 ABIv11 executables. x86_64-aros-gcc on PATH
# (AROS_SDK = the sysroot). Run them with ../../tests/aros/aros.sh (README.md). Plain GNU make.
CC	= x86_64-aros-gcc
CFLAGS	= -std=gnu99 -O2 -Wall -Wextra -Wno-unused-parameter $(if $(AROS_SDK),--sysroot=$(AROS_SDK))

PROGS	= arosprobe

all: $(PROGS)

arosprobe: arosprobe.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f $(PROGS)

.PHONY: all clean

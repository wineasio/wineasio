#!/usr/bin/make -f
# Makefile for MOD Desktop ASIO #
# ----------------------------- #
# Created by falkTX
#

### Tools

CC ?= gcc

### Common settings

CFLAGS += $(shell pkg-config --cflags jack)
CFLAGS += -std=gnu11
CFLAGS += -D_REENTRANT -DNATIVE_INT64
CFLAGS += -fPIC
CFLAGS += -Wall
CFLAGS += -pipe
CFLAGS += -fno-strict-aliasing -Wwrite-strings -Wpointer-arith
CFLAGS += -Werror=implicit-function-declaration
CFLAGS += -I. -Irtaudio/include

# Debug or Release
ifeq ($(DEBUG),true)
CFLAGS += -O0 -DDEBUG -g -D__WINESRC__
else
CFLAGS += -O2 -DNDEBUG -fvisibility=hidden
endif

# CFLAGS += -fdata-sections -ffunction-sections
# CFLAGS += -fno-common
# CFLAGS += -fvisibility=hidden
# CFLAGS += -fno-stack-protector -U_FORTIFY_SOURCE -Wp,-U_FORTIFY_SOURCE
# CFLAGS += -fno-gnu-unique
# CFLAGS += -ffast-math -fno-finite-math-only
# CFLAGS += -Os -DNDEBUG=1 -fomit-frame-pointer
# CFLAGS += -mtune=generic -msse -msse2 -mfpmath=sse
# CFLAGS +=  -D__STDC_FORMAT_MACROS=1
# CFLAGS +=  -D__USE_MINGW_ANSI_STDIO=1
# CFLAGS +=  -mstackrealign
# CFLAGS +=  -posix
# LDFLAGS += -Wl,--gc-sections,--no-undefined
# LDFLAGS += -Wl,-O1
# LDFLAGS += -Wl,--as-needed,--strip-all
# LDFLAGS += -static -static-libgcc -static-libstdc++ -Wl,-Bstatic

### Global source lists

SRCS = asio.c main.c regsvr.c JackBridge.c
OBJS = $(SRCS:%.c=build/%.c.o)

### Generic targets

all: mod-desktop-asio.dll

clean:
	rm -f $(OBJS) mod-desktop-asio.dll
	rm -rf build

### Build rules

.PHONY: all

# Implicit rules

build/%.c.o: %.c
	@$(shell mkdir -p build)
	$(CC) -c $(INCLUDE_PATH) $(CFLAGS) $(CEXTRA) -o $@ $<

### Target specific build rules

mod-desktop-asio.dll: $(OBJS)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -shared mod-desktop-asio.dll.def -lodbc32 -lole32 -luuid -lwinmm -o $@

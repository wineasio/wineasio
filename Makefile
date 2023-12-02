#!/usr/bin/make -f
# Makefile for MOD App ASIO #
# ------------------------- #
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
LDFLAGS += $(subst jackserver,jack,$(shell pkg-config --libs jack))

# Debug or Release
ifeq ($(DEBUG),true)
CFLAGS += -O0 -DDEBUG -g -D__WINESRC__
else
CFLAGS += -O2 -DNDEBUG -fvisibility=hidden
endif

### Global source lists

SRCS = asio.c main.c regsvr.c JackBridge.c
OBJS = $(SRCS:%.c=build/%.c.o)

### Generic targets

all: mod-app-asio.dll

### Build rules

.PHONY: all

# Implicit rules

build/%.c.o: %.c
	@$(shell mkdir -p build)
	$(CC) -c $(INCLUDE_PATH) $(CFLAGS) $(CEXTRA) -o $@ $<

### Target specific build rules

mod-app-asio.dll: $(OBJS)
	$(CC) $^ $(CFLAGS) $(LDFLAGS) -shared mod-app-asio.dll.def -lodbc32 -lole32 -luuid -lwinmm -o $@

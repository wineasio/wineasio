#!/usr/bin/make -f
# Makefile for WineASIO #
# --------------------- #
# Created by falkTX
# Initially generated by winemaker
#

ifeq ($(ARCH),)
$(error incorrect use of Makefile, ARCH var is missing)
endif
ifeq ($(M),)
$(error incorrect use of Makefile, M var is missing)
endif

PREFIX                = /usr
SRCDIR                = .
DLLS                  = wineasio.dll

### Tools

CC = gcc
WINECC = winegcc

### Common settings

CEXTRA                = -m$(M) -D_REENTRANT -fPIC -Wall -pipe
CEXTRA               += -fno-strict-aliasing -Wdeclaration-after-statement -Wwrite-strings -Wpointer-arith
CEXTRA               += -Werror=implicit-function-declaration
CEXTRA               += $(shell pkg-config --cflags jack)
RCEXTRA               =
INCLUDE_PATH          = -I. -Irtaudio/include
INCLUDE_PATH         += -I$(PREFIX)/include/wine
INCLUDE_PATH         += -I$(PREFIX)/include/wine/windows
INCLUDE_PATH         += -I$(PREFIX)/include/wine-development
INCLUDE_PATH         += -I$(PREFIX)/include/wine-development/wine/windows
DLL_PATH              =
LIBRARY_PATH          =
LIBRARIES             = $(shell pkg-config --libs jack)

# 64bit build needs an extra flag
ifeq ($(M),64)
CEXTRA               += -DNATIVE_INT64
endif

# Debug or Release
ifeq ($(DEBUG),true)
CEXTRA               += -O0 -DDEBUG -g -D__WINESRC__
LIBRARIES            +=
else
CEXTRA               += -O2 -DNDEBUG -fvisibility=hidden
endif

### wineasio.dll sources and settings

wineasio_dll_MODULE   = wineasio.dll
wineasio_dll_C_SRCS   = asio.c \
			main.c \
			regsvr.c
wineasio_dll_LDFLAGS  = -shared \
			-m$(M) \
			-mnocygwin \
			$(wineasio_dll_MODULE:%=%.spec) \
			-L/usr/lib$(M)/wine \
			-L/usr/lib/$(ARCH)-linux-gnu/wine \
			-L/usr/lib/$(ARCH)-linux-gnu/wine-development \
			-L/opt/wine-staging/lib \
			-L/opt/wine-staging/lib/wine \
			-L/opt/wine-staging/lib$(M) \
			-L/opt/wine-staging/lib$(M)/wine
wineasio_dll_DLL_PATH =
wineasio_dll_DLLS     = odbc32 \
			ole32 \
			winmm
wineasio_dll_LIBRARY_PATH=
wineasio_dll_LIBRARIES= uuid

wineasio_dll_OBJS     = $(wineasio_dll_C_SRCS:%.c=build$(M)/%.c.o)

### Global source lists

C_SRCS                = $(wineasio_dll_C_SRCS)

### Generic targets

all:
build: rtaudio/include/asio.h $(DLLS:%=build$(M)/%.so)

### Build rules

.PHONY: all

# Implicit rules

DEFINCL = $(INCLUDE_PATH) $(DEFINES) $(OPTIONS)

build$(M)/%.c.o: %.c
	@$(shell mkdir -p build$(M))
	$(CC) -c $(DEFINCL) $(CFLAGS) $(CEXTRA) -o $@ $<

### Target specific build rules
DEFLIB = $(LIBRARY_PATH) $(LIBRARIES) $(DLL_PATH)

build$(M)/$(wineasio_dll_MODULE).so: $(wineasio_dll_OBJS)
	$(WINECC) $(wineasio_dll_LDFLAGS) -o $@ $(wineasio_dll_OBJS) $(wineasio_dll_LIBRARY_PATH) $(DEFLIB) $(wineasio_dll_DLLS:%=-l%) $(wineasio_dll_LIBRARIES:%=-l%)

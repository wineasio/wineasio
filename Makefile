### Generated by Winemaker

PREFIX                = /usr
SRCDIR                = .
SUBDIRS               =
DLLS                  = wineasio.dll
EXES                  =



### Common settings

CEXTRA                = -m32 -g -O2 -D__WINESRC__ -D_REENTRANT -fPIC -Wall -pipe -fno-strict-aliasing -Wdeclaration-after-statement -Wwrite-strings -Wpointer-arith
CXXEXTRA              = -m32 -D__WINESRC__ -D_REENTRANT -fPIC -Wall -pipe -fno-strict-aliasing -Wdeclaration-after-statement -Wwrite-strings -Wpointer-arith
RCEXTRA               =
INCLUDE_PATH          = -I. -I/usr/include -I$(PREFIX)/include -I$(PREFIX)/include/wine -I$(PREFIX)/include/wine/windows
DLL_PATH              =
LIBRARY_PATH          =
LIBRARIES             = -ljack


### wineasio.dll sources and settings

wineasio_dll_MODULE   = wineasio.dll
wineasio_dll_C_SRCS   = asio.c \
			main.c \
			regsvr.c
wineasio_dll_CXX_SRCS =
wineasio_dll_RC_SRCS  =
wineasio_dll_LDFLAGS  = -shared \
			$(wineasio_dll_MODULE:%=%.spec) \
			-mnocygwin
wineasio_dll_DLL_PATH =
wineasio_dll_DLLS     = odbc32 \
			ole32 \
			oleaut32 \
			winspool \
			winmm \
			pthread
wineasio_dll_LIBRARY_PATH=
wineasio_dll_LIBRARIES= uuid

wineasio_dll_OBJS     = $(wineasio_dll_C_SRCS:.c=.o) \
			$(wineasio_dll_CXX_SRCS:.cpp=.o) \
			$(wineasio_dll_RC_SRCS:.rc=.res)



### Global source lists

C_SRCS                = $(wineasio_dll_C_SRCS)
CXX_SRCS              = $(wineasio_dll_CXX_SRCS)
RC_SRCS               = $(wineasio_dll_RC_SRCS)


### Tools

CC = gcc
CXX = g++
WINECC = winegcc
RC = wrc


### Generic targets

all: asio.h $(SUBDIRS) $(DLLS:%=%.so) $(EXES:%=%.so)

### Build rules

.PHONY: all clean dummy

$(SUBDIRS): dummy
	@cd $@ && $(MAKE)

# Implicit rules

.SUFFIXES: .cpp .rc .res
DEFINCL = $(INCLUDE_PATH) $(DEFINES) $(OPTIONS)

.c.o:
	$(CC) -c $(DEFINCL) $(CFLAGS) $(CEXTRA) -o $@ $<

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.cxx.o:
	$(CXX) -c $(CXXFLAGS) $(CXXEXTRA) $(DEFINCL) -o $@ $<

.rc.res:
	$(RC) $(RCFLAGS) $(RCEXTRA) $(DEFINCL) -fo$@ $<

# Rules for cleaning

CLEAN_FILES     = y.tab.c y.tab.h lex.yy.c core *.orig *.rej \
                  \\\#*\\\# *~ *% .\\\#*

clean:: $(SUBDIRS:%=%/__clean__) $(EXTRASUBDIRS:%=%/__clean__)
	$(RM) $(CLEAN_FILES) $(RC_SRCS:.rc=.res) $(C_SRCS:.c=.o) $(CXX_SRCS:.cpp=.o)
	$(RM) $(DLLS:%=%.so) $(EXES:%=%.so) $(EXES:%.exe=%)

$(SUBDIRS:%=%/__clean__): dummy
	cd `dirname $@` && $(MAKE) clean

$(EXTRASUBDIRS:%=%/__clean__): dummy
	-cd `dirname $@` && $(RM) $(CLEAN_FILES)

distclean:: clean
	$(RM) asio.h

### Target specific build rules
DEFLIB = $(LIBRARY_PATH) $(LIBRARIES) $(DLL_PATH)

$(wineasio_dll_MODULE).so: $(wineasio_dll_OBJS)
	$(WINECC) $(wineasio_dll_LDFLAGS) -o $@ $(wineasio_dll_OBJS) $(wineasio_dll_LIBRARY_PATH) $(DEFLIB) $(wineasio_dll_DLLS:%=-l%) $(wineasio_dll_LIBRARIES:%=-l%)

install:
	cp wineasio.dll.so $(PREFIX)/lib/wine
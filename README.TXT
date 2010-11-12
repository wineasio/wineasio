Before installation edit the prefix path in the Makefile
PREFIX = <root path you use>

usually this will either be

PREFIX = /usr
or
PREFIX = /usr/local

Copy the file asio.h from Steinberg's asio-sdk to
the wineasio directory

then execute: make
and as root:  make install

then, again as normal user: regsvr32 wineasio.dll

Notes: 
The asio.c file uses 32 bit integer buffers, wich is supported by 
most asio applications. The asio.c.float uses 32 bit float buffers and
thus avoids the format conversion necessary for jack.
You can set the number of ASIO inputs/outputs using evnironment variables

ASIO_INPUTS
ASIO_OUTPUTS

with bash e.g.:
export ASIO_INPUTS=4
export ASIO_OUTPUTS=8

original code: Robert Reif posted to the wine mailinglist
modified by: Ralf Beck (musical_snake@gmx.de)

todo: 
- make timecode sync to jack transport


changelog:
0.3:
30-APR-2007: corrected connection of in/outputs

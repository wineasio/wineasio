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


Runtime settings
----------------

You can set the number of ASIO inputs/outputs using environment variables
ASIO_INPUTS
ASIO_OUTPUTS

with bash e.g.:
export ASIO_INPUTS=4
export ASIO_OUTPUTS=8

You can set the connections for the ports using environment variables
INPORTn=client:port
OUTPORTn=client:port
where "n" is the wine port number, starting at zero and the "client:port"
is the JACK port you want to use.  These are used to override physical ports
that would otherwise be used.

Rather than setting these in your environment, you can set up a file called
.wineasiocfg in your home directory (or a site-wide /etc/default/wineasiocfg).

----------------

original code: Robert Reif posted to the wine mailinglist
modified by: Ralf Beck (musical_snake@gmx.de)
port mapping and config file: Peter L Jones (pljones@users.sf.net)

todo: 
- make timecode sync to jack transport


changelog:
0.5:
03-SEP-2007: port mapping and config file
0.3:
30-APR-2007: corrected connection of in/outputs

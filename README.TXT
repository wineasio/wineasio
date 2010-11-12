CONTENTS
========
1. Installation
2. User Instruction
3. Credits
4. Change Log

1. INSTALLATION
---------------

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
most asio applications.  The asio.c.float uses 32 bit float buffers and
thus avoids the format conversion necessary for jack.


2. USER INSTRUCTIONS
--------------------

The driver can be configured in two ways: either using environment variables
or using a configuration file.

The configuration file can be set per user in ".wineasiocfg".
As a fallback, a site file can be provided in "/etc/default/wineasiocfg"
if desired.

The format for the configuration file is simply "var=val".

If using the shell, either include the assignment on the command line:
    ASIO_INPUTS=0 ~/bin/reaper.exe
or ensure the variable has been exported:
    EXPORT ASIO_INPUTS=0
    ~/bin/reaper.exe

The available variables are as follows:
ASIO_INPUTS
ASIO_OUTPUTS
ASIO_INPORTNAME<n>
ASIO_OUTPORTNAME<n>
ASIO_INPORT<n>
ASIO_OUTPORT<n>
<clientname>

The last entry allows you to change the client name from the default, which
is constructed from the program name prefixed by "ASIO".  For example,
    ASIO_reaper.exe=REAPER
All of the entries beginning ASIO_ can also have entries specific to a client,
using the assigned client name.  For example,
    REAPER_INPUTS=0

INPUTS and OUTPUTS
------------------
These let you limit the number of JACK ports allocated to this client.

INPORTNAME and OUTPORTNAME
--------------------------
These allow you to rename the input and output ports for the client.
The default names are "input_<n>" and "output_<n>".  For example,
    REAPER_OUTPORTNAME0=left
    REAPER_OUTPORTNAME1=right

INPORT and OUTPORT
------------------
These allow you to connect the client to JACK ports of your choice.
The default is to connect JACK's "hardware" inputs to your client's inputs
and your client's outputs to JACK's "hardware" outputs.  You might be
running some other application, e.g. an icecast server, and want to
send output to that.  For example,
    ASIO_OUTPORT0=idjc-mx:aux_lt
    ASIO_OUTPORT1=idjc-mx:aux_rt


3. CREDITS
----------

original code: Robert Reif posted to the wine mailinglist
    see also http://bugs.winehq.org/show_bug.cgi?id=2161

modified by: Ralf Beck (musical_snake@gmx.de)

port mapping, config file, dynamic client naming, bringing
back in line with Robert Reif's code
	Peter L Jones (pljones@users.sf.net)

todo: 
- make timecode sync to jack transport


4. CHANGE LOG
-------------
0.7.1:
02-DEC-2007: Build against installed libjack.so rather than dlloading (RB)

0.7:
01-DEC-2007: In a fit of insanity, I merged JackLab and Robert Reif code bases. (PLJ)

0.6:
21-NOV-2007: add dynamic client naming (PLJ)

0.0.3:
17-NOV-2007: Unique port name code (RR)

0.5:
03-SEP-2007: port mapping and config file (PLJ)

0.3:
30-APR-2007: corrected connection of in/outputs (RB)

0.1:
???????????: Initial RB release (RB)

0.0.2:
12-SEP-2006: Fix thread bug, tidy up code (RR)

0.0.1:
31-AUG-2006: Initial version (RR)

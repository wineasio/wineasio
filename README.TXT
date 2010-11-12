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

wineasio is currently 32bit only and must live in the lib32 library on 
64bit systems.  The Makefile tries to autodetect this.  But if it fails,
set LIBDIR.

Copy the file asio.h from Steinberg's asio-sdk to the wineasio directory

then execute: make
and as root:  make install

then, again as normal user: regsvr32 wineasio.dll

Notes: 
The asio.c file uses 32 bit integer buffers, which is supported by most asio
applications.


2. USER INSTRUCTIONS
--------------------

The driver can be configured in two ways: either using environment variables or
using a configuration file.

The configuration file can be set per user in ".wineasiocfg".  As a fallback, a
site-wide file can be provided in "/etc/default/wineasiocfg" if desired.

The format for the configuration file is simply "var=val".

If using the shell, either include the assignment on the command line:
    ASIO_INPUTS=0 ~/bin/reaper.exe
or ensure the variable has been exported:
    export ASIO_INPUTS=0
    ~/bin/reaper.exe

The available variables are as follows:
ASIO_INPUTS
ASIO_OUTPUTS
ASIO_INPORTNAME<n>
ASIO_OUTPORTNAME<n>
ASIO_INPORT<n>
ASIO_OUTPORT<n>
ASIO_AUTOCONNECT
<clientname>

The last entry allows you to change the client name from the default, which is
constructed from the program name prefixed by "ASIO_".  For example,
    ASIO_reaper=REAPER
All of the entries beginning ASIO_ can also have entries specific to a client,
using the assigned client name.  For example,
    ASIO_reaper_INPUTS=0
or (if the client name has been re-assigned as above),
    REAPER_INPUTS=0

INPUTS and OUTPUTS
------------------
These let you limit the number of JACK ports allocated to this client.  The
default value for both is 2.

INPORTNAME and OUTPORTNAME
--------------------------
These allow you to rename the input and output ports for the client.  The
default names are "input_<n>" and "output_<n>".  For example,
    REAPER_OUTPORTNAME0=left
    REAPER_OUTPORTNAME1=right

INPORT and OUTPORT
------------------
These allow you to connect the client to JACK ports of your choice.  The
default is to connect JACK's "hardware" inputs to your client's inputs and your
client's outputs to JACK's "hardware" outputs.  You might be running some other
application, e.g. an icecast server, and want to send output to that.  For
example,
    ASIO_OUTPORT0=idjc-mx:aux_lt
    ASIO_OUTPORT1=idjc-mx:aux_rt

AUTOCONNECT
-----------
This enables you to control whether a client automatically connects to JACK or
whether you will do the wiring up yourself.  The default is to connect.  To turn
it off, use, for example,
    ASIO_reaper_AUTOCONNECT=false
(where "false" is anything other than "true" (case independent)).

3. CREDITS
----------

original code: Robert Reif posted to the wine mailinglist
    see also http://bugs.winehq.org/show_bug.cgi?id=2161

modified by: Ralf Beck (musical_snake@gmx.de)

port mapping, config file, dynamic client naming, bringing back in line with
Robert Reif's code, merging JackWASIO:
	Peter L Jones (pljones@users.sf.net)

fix for windows-style path handling: William Steidtmann

todo: 
- make timecode sync to jack transport
- are we leaving memory allocated?


4. CHANGE LOG
-------------
0.8:
08-AUG-2010: Forward port JackWASIO changes... needs testing hard. (PLJ)

0.7.6:
27-DEC-2009: Fixes for compilation on 64bit platform. (PLJ)

0.7.5:
29-Oct-2009: Added fork with call to qjackctl from ASIOControlPanel(). (JH)
29-Oct-2009: Changed the SCHED_FIFO priority of the win32 callback thread. (JH)
28-Oct-2009: Fixed wrongly reported output latency. (JH)

0.7.4:
08-APR-2008: Updates to the README.TXT (PLJ)
02-APR-2008: Move update to "toggle" to hopefully better place (PLJ)
24-MCH-2008: Don't trace in win32_callback.  Set patch-level to 4. (PLJ)
09-JAN-2008: Nedko Arnaudov supplied a fix for Nuendo under WINE.

0.7.3:
27-DEC-2007: Make slaving to jack transport work, correct port allocation bug. (RB)

0.7:
01-DEC-2007: In a fit of insanity, I merged JackLab and Robert Reif code bases. (PLJ)

0.6:
21-NOV-2007: add dynamic client naming (PLJ)

0.0.3:
17-NOV-2007: Unique port name code (RR)

JackWASIO (branched from 0.5-ish):
02-OCT-2007: Johnny Petrantoni - added more Linux compatibility code, it should compile now on a linux box (still need edit makefile)
30-SEP-2007: Johnny Petrantoni - added JackPilot preferences for virtual-ports and auto connect, added some macros for linux compilation, 
								 removed prepath from libjack.
29-SEP-2007: Johnny Petrantoni - added mutex and condition code to synch IT IS WORKING NOW!!! tested down to 256 buffersize using reaper.
28-SEP-2007: Johnny Petrantoni - Added pThreadUtilities.h code... but the cracks are still there..
28-SEP-2007: Johnny Petrantoni - Minor fixes, added TODO and this changes log.
28-SEP-2007: Johnny Petrantoni - Added Stephane GetEXEName() and code cleanup.
28-SEP-2007: Johnny Petrantoni - First Import.

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

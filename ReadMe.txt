This is the second release of the wine ASIO driver.

Changes:

1. The ASIO driver now requires that the Steinberg ASIO SDK be installed to get ASIO support.
   This appears to be how other open source projects deal with the ASIO technology licencing.

2. The first version was able to call Windows functions from the ASIO callback which was
   called from the Jack thread.  This only worked for programs that didn't call Windows
   functions.  The ASIO callback is now called from a Windows thread.

3. You should now be able to compile a dummy driver if Jack or the ASIO SDK are not installed.

4. A buffer swap bug was fixed.

Install:

1. Download the Steinberg ASIO SDK from: http://www.steinberg.net/324_1.html and
   install it somewhere like: /usr/local/asiosdk2.1.

2. Patch wine with the wineasio.diff.txt patch.

3. Run autoconf to generate a new configure.

4. Run ./configure --with-asio-sdk=/usr/local/asiosdk2.1 (or wherever you installed the SDK).

5. Do the normal make depend, make and make install

6. Register the wineasio.dll by doing:
   cd wine/dlls/wineasio
   regsvr32 wineasio.dll.so

To Do:
   ASIO control panel to set Jack parameters (devices, sample rate, buffer size, ...).
   Better integration with wine.
   You tell me.

Please send feeback to: reif@earthlink.net


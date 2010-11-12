Instalation instructions:

Copy the wineasio directory to wine/dlls.
Install the wineasio.diff.txt patch. 
Run autoconf to create a new configure.
Do a normal install.
Run regsvr32 on the wineasio.dll.so file in dlls/wineasio.
You may also need to symlink /usr/local/wine/lib/wineasio.dll.so to ~/.wine/drive_c/windows/system32.

Please send feedback to reif@earthlink.net


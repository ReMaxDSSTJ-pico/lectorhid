# lectorhid
This is a semi-port of the PCPROX reader for 125Khz RFID readers from PCPROX/WAVEID using GCC C 
It happens there is a repository and a library for Phyton that reads the PCPROX using Phyton and USB HID.
But I don't know to write on Phyton and I am lazy. So I ported the existing program from <micolous/pcpro>
to create my own program in C.

I use VS Code to edit, and compile with make, using a Makefile.
I really don't like VS Code but it's free and I have to use it. And I waste too much time finding and searching
how the VS Code works, with those entangled .json files.

It needs the hidapi from usblib. Get the hidapi library from https://github.com/libusb/hidapi/tree/master and
install to your system.

This program uses a Makefile, so get CMAKE for windows in order to compile with Maketools on VS Code.
Having trouble compiling this on VS Code? Run make from the command line! Really!
Or read this https://hackernoon.com/how-to-set-up-c-debugging-in-vscode-using-a-makefile
It helped me, and can help you. Just be ware the path to the compiles may be different on your system.

Pedro Farías Lozano
tjactivo@hotmail.com


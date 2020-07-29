## OSAB - the Open Source Audio Bible player
## [theaudiobible.org](http://theaudiobible.org/)
#### Copyright (C) 2011-2020 Theophilus

OSAB is a design for a portable audio Bible player released under the MIT Licence in the hope that it will be used by others worldwide to produce and distribute portable audio players that are specifically designed to share God's word with those who need to hear.

The intention is to share firmware, software, schematics, PCB layout and enclosure design along with instructions on how to put it all together.  The intention with the enclosure is that it be 3D-printable on a low cost FDM 3D printer, or mass-producible via injection moulding.

## osab-firmware
This is the repository for the firmware source files for OSAB.

### Licence
This is open source software/firmware released under the MIT Licence - please see the LICENCE file in this directory.

## Firmware build process
The firmware is for the VS1000 processor from VLSI - vlsi.fi

### Dependencies
Building needs to be done on a Linux machine at this stage.

Other dependencies include:
* glibc.i686
* GNU make
* gcc
* curl
* unzip
* tar
* patch
* sed

To build the EEPROM image file (eeprom.img) just change to the directory where this `README.md` file resides and run:
```shell
make
```

The appropriate tools and library should then be downloaded from vlsi.fi and the build process should run, producing eeprom.img.

A successful result should end something like this:
```shell
tools/vskit130/bin/coff2spiboot -x 0x50 osab.bin eeprom.img
I: 0x0050-0x0729 In: 7017, out: 7017
X: 0x1fa0-0x1ffe In:  193, out:  193
X: 0x210f-0x2112 In:   11, out:   11
In: 7221, out: 7226
```

## Uploading firmware to EEPROM on VS1000 board

### Hardware dependencies
* This firmware is designed to work on an **OSAB board** - see the **osab-electronics** project for the schematic and PCB (printed circuit board) design.  Note that the OSAB PCB design is still a work in progress.

* Until the OSAB board design is complete, firmware testing will have to be done on an existing VS1000 board such as the VS1000 developer board from vlsi.fi or on an [m7 audio Bible player](https://theaudiobible.org/).  The advantage of using the m7 is that the firmware will work "out of the box" on this hardware, whereas any other VS1000 board would need hardware modification.

* A 3.3V TTL USB-serial cable is needed to upload the firmware to the OSAB board.

* The OSAB board (and the m7) uses the USB port for two purposes:
  * Charging the lithium battery.
  * Serial port for firmware upload. The D+ USB pin is connected to TX and the D- USB pin to RX on the VS1000 chip. Note that these pins can only tolerate up to 3.3VDC - do not connect a serial cable that operates at standard RS-232 levels directly to this port.  Instead, use a serial cable that operates at 3.3V TTL levels, or use an RS-232 to 3.3V TTL level shifter.

### Linux machine modification
The Linux machine used to upload the firmware to the OSAB board needs the following:
* If you are not already a member of this group, then add your user account to the **dialout** group.  Log out and in again after doing this:
```shell
sudo usermod -aG dialout <your_username>
```

* Plug the USB-serial cable into the Linux machine and check what device name is assigned to it by running `dmesg`.  You should notice something like ttyUSB0 or ttyUSB1:
```shell
dmesg | tail
[161708.600058] usb 2-1.4: new full-speed USB device number 28 using ehci-pci
[161708.679604] usb 2-1.4: New USB device found, idVendor=067b, idProduct=2303
[161708.679607] usb 2-1.4: New USB device strings: Mfr=1, Product=2, SerialNumber=0
[161708.679609] usb 2-1.4: Product: USB-Serial Controller D
[161708.679611] usb 2-1.4: Manufacturer: Prolific Technology Inc.
[161708.681874] pl2303 2-1.4:1.0: pl2303 converter detected
[161708.684057] usb 2-1.4: pl2303 converter now attached to ttyUSB0
```

* Symlink the device file to `/dev/ttyS0`.  The vs3emu software from VLSI expects the serial port to be on `/dev/ttyS0` or `/dev/ttyS1`, but Linux creates a device file `/dev/ttyUSB0` (or similar) for USB serial devices.  So, the easiest solution is to remove the existing `/dev/ttyS0` file (assuming it is not needed) and make a symbolic link from `/dev/ttyUSB0` to `/dev/ttyS0`.
```shell
sudo rm /dev/ttyS0
sudo ln -s /dev/ttyUSB0 /dev/ttyS0
```

### Uploading the firmware
1. Connect the USB-serial cable to the Linux machine and to the OASB USB port.

2. Hold the CS pin on the EEPROM chip low before powering up the VS1000. On the OSAB or m7 board this can be done by pinching the outsides of pins 1 and 4 on the 25LC640A with a pair of metal tweezers (discharge any static electricity from your body before doing this).

3. While holding the CS pin on the EEPROM chip low, press the reset button, then press the Play button to power up the VS1000.  If there is existing firmware in the EEPROM and you hear audio playing, then you know that the CS pin was not held low properly as the VS1000 booted.  At this point, while the VS1000 is powered up, hold the CS pin low again and press the reset button to force a reboot of the VS1000.

4. To upload the firmware image file (eeprom.img), run `make upload` and you should see something like the following:
```shell
make upload
vs3emu -chip vs1000 -s 115200 -l prommer.bin e.cmd
VSEMU 2.2 Nov 12 2010 16:45:47(c)1995-2010 VLSI Solution Oy
Clock 11999 kHz
Using serial port 1, COM speed 115200
Waiting for a connection to the board...

Caused interrupt
Chip version "1000"
Stack pointer 0x19e0, bpTable 0x7d4d
User program entry address 0x7526
prommer.bin: includes optional header, 12 sections, 451 symbols
Section 1: SingleCycleCommand  page:0 start:83 size:23 relocs:3
Section 2: SpiWaitStatus  page:0 start:106 size:43 relocs:7
Section 3: SpiWriteBlock  page:0 start:149 size:54 relocs:7
Section 4: SpiReadBlock  page:0 start:203 size:37 relocs:5
Section 5: puthex      page:0 start:240 size:50 relocs:5
Section 6: const_x     page:1 start:8096 size:260 relocs:0
Section 7: WriteEEPROM  page:0 start:290 size:167 relocs:67
Section 8: main        page:0 start:457 size:37 relocs:3
Section 9: init_x      page:1 start:8356 size:17 relocs:0
Section 10: code        page:0 start:80 size:3 relocs:1 fixed
Section 11: VS_stdiolib  page:0 start:494 size:74 relocs:18
Section 12: VS_stdiolib$0  page:0 start:568 size:110 relocs:32
Entered main()
Trying to open eeprom.img
Erase first sector...
0000

Programming...
Sector 0000...
Sector 0001...
Sector 0002...
Sector 0003...
Sector 0004...
Sector 0005...
Sector 0006...
Sector 0007...
Sector 0008...
Sector 0009...
Sector 000A...
Sector 000B...
Sector 000C...
Sector 000D...
Sector 000E...
Reading first 2 words of EEPROM: 564C5349 ("VLSI"), which is a valid VLSI boot id.
Done.
```

5. At this point you can press ctrl-C to quit vs3emu and then press the reset button on the OSAB board.  Assuming there is a microSD card with .ogg files and a menu.mnu file (refer to the `README.md` in the **osab-tools** project for the process of uploading audio content to a microSD card) then you should hear the audio playing.  If there is no microSD card present, then the blue status LED should flash.

## Cleaning up
To clean up object files and build targets, just run:
```shell
make clean
```

To clean up the entire source tree, just run:
```shell
make very-clean
```

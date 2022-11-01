# WiRSa
This project is a Wifi - RS232 Serial adapter for getting vintage computers connected to the internet (telnet or http). Use it to connect your vintage computer to a telnet BBS or other internet resources. At it's heart is a Wemos ESP8266 wifi-enabled controller. This project differs from other Wifi Serial adapters in that it adds a micro-SD card module for reading and writing data. The intended use case is for people who have a vintage computer but have no ability (or desire) to create floppies, and doesn't want to modify the computer (by installing a permantent floppy emulation solution something like a Gotek). V2 also adds a 128x64 display and 4 navigation buttons to make setup easier.

## Some assembled units available for sale
If you prefer to save yourself some time, I have some units (fully assembled or kit) available for sale on my eBay store here: https://www.ebay.com/itm/175299622202 

## History
This began as a device I built for the filming of Apple TV's "The Shining Girls". There were a number of scenes involving computer terminals. They had the terminals but no idea how to display anything on them. I came up with this device as a means to display a static block of text at the terminal or "playback" a text file - ie: when the actor hits a key on the keyboard, regardless of what key is pressed - the next character from the text file gets printed. This creates the illusion that the actor is actually typing the document in real-time but they're really just mashing keys. I've included this text playback feature in the firmware. The text file is read directly from the SD card. I had previously used a Wemos to build a Wifi modem for my Commodore 64 user port. This project is basically the combination of those 2 use cases.

## Firmware
The firmware has 3 main functions:  
1. MODEM mode, for telnet/BBS use. The Modem Mode of the firmware is based on "WiFi SIXFOUR" (https://github.com/thErZAgH/c64modem), which in turn was based on the "Virtual modem for ESP8266" (https://github.com/jsalin/esp8266_modem).
2. Text Playback mode, for reading and printing text files from the SD card over the serial line.
3. File Transfer mode which offers the ability to send and receive data from the SD card with the host computer (so long as the host computer is running a terminal software which supports file transfer). Currently only the file transfer protocol YMODEM is supported. There are plans to also implement XMODEM,  YMODEM Batch, ZMODEM and KERMIT.

On a new build, the default serial settings will be 9600 baud, 8-N-1. You can change the baud rate while in Modem Mode, with the SET BAUD RATE (AT$SB=) command.

## Flashing the Firmware
If you have a WiRSa and want to upgrade the firware to the latest version found here, you have 2 options... 
1. Install the Arduino software and download the /Firmware/WiRSa/WiRSa.ino file. Load the .ino file and compile/upload to the device. You may need to resolve some library dependancies.
2. Much simpler way is to download the filecompiled binary at /Firmware/WiRSa/WiRSa.ino.d1_mini.bin. Download [NodeMCU-PyFlasher](https://github.com/marcelstoer/nodemcu-pyflasher/releases) and use it to upload the new version. 

## Board Design / PCB
The WiRSA is based on a few cheaply available components and a custom PCB which ties them all together. The components needed are: 
• WiRSa v2 PCB
• Lolin (Wemos) D1 mini (which is an ESP8266-based controller with a USB interface and is compatible with Arduino)
• RS232-to-TTL converter module
• 128x64 SPI OLED display
• SD card reader module & 6-pin header
• (4) right-angle momentary switches
• (4) 10k resistors
• (2) jumpers & (2) 2-pin jumper headers
Everything gets soldered to a custom PCB. The KiCAD schematic, PCB and Gerber files are all included in this repo. 

## Enclosure
The enclosure has been created in OpenSCAD and the source is included here as well as a resulting STL. It has a sliding top for easy access to the PCB, openings in the front for the Serial connector, opening in the rear for USB & SD card, and a small hole on the side to access the Wemos reset switch. The cover has an opening for the display.

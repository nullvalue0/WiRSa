# RetroDisks WiRSa v3 - WiFi RS232 Serial Modem Adapter

The WiRSa v3 is a WiFi-to-RS232 serial adapter designed to get vintage computers connected to the internet (telnet or http). Use it to connect your vintage computer to a telnet BBS or other internet resources.. It allows vintage computers to connect to modern networks via WiFi, supporting telnet BBS connectivity, file transfers, and network gateway functionality. This project differs from other Wifi Serial adapters in that it adds a micro-SD card module for reading and writing data. The intended use case is for people who have a vintage computer but have no ability (or desire) to create floppies, and doesn't want to modify the computer (by installing a permanent floppy emulation solution something like a Gotek). Includes a 128x64 display, 4 navigation buttons, ESP32 processor, battery charging circuit and power switch, and upgrades the RS232 level shifting to a MAX3242 device which is capable of driving all serial lines.

![WiRSa Menu Action](https://github.com/nullvalue0/WiRSa/blob/main/Pictures/2024-12-23T05_33_06.740Z-IMG_20241222_222852_2~2.jpg)

## Kits & assembled units available for sale
If you prefer to save yourself some time, I have some units (fully assembled or kit) available for sale. I had quite a few PCB's made up and I am trying to keep these as cheap as possible. You can purchase on [Tindie](https://www.tindie.com/products/retrodisks/wirsa-v3-wifi-rs232-serial-modem-adapter-with-sd).

## Features

### Hardware

- **Processor:** ESP-WROOM-32D with WiFi and Bluetooth 4.2 BLE
- **Display:** 128x64 two-color OLED display for settings, commands, and real-time data
- **Serial Port:** Female DB9 (DE9) connector
- **Full RS232 Breakout:**
  - DSR/DTR (Data Set Ready / Data Terminal Ready)
  - RTS/CTS (Request to Send / Clear to Send)
  - DCD (Data Carrier Detect)
  - RI (Ring Indicator)
- **Storage:** Built-in micro SD card reader for file transfers and text playback
- **Power:**
  - USB-C power connector
  - Optional 2000mAh 3.7V Li-Po battery (~14 hours runtime)
- **Navigation:** Four physical buttons for menu navigation

![WiRSa Unboxing Action](https://github.com/nullvalue0/WiRSa/blob/main/Pictures/readme_image2.jpg)

### Operating Modes

1. **MODEM Mode** - Hayes-compatible AT command modem emulation for telnet/BBS connectivity
2. **File Transfer** - Binary file transfer directly from SD card via XModem, YModem, and ZModem protocols
3. **Text Playback** - Display text files from SD card through the serial port
4. **PPP Gateway** - Point-to-Point Protocol gateway/server for TCP/IP networking
5. **SLIP Gateway** - Serial Line Internet Protocol gateway/server for TCP/IP networking
6. **Telnet Server** - Remote into your vintage computer or serial device over telnet
7. **Utilities** - Diagnostics and troubleshooting tools including signal monitoring and loopback testing
8. **Config** - Settings for baud rate, serial configuration, display orientation, and more

### Software Features

- **AT Command Set** - Full Hayes-compatible modem commands
- **Web Interface** - Built-in web server for status monitoring, configuration and file management
- **Flow Control** - Hardware (RTS/CTS) and software (XON/XOFF) flow control
- **Speed Dial** - 10 programmable speed dial entries
- **PETSCII Translation** - Support for Commodore computers
- **Telnet Protocol** - Automatic Telnet IAC handling
- **OTA Updates** - Over-the-air firmware updates via HTTP

![WiRSa BBS Action](https://github.com/nullvalue0/WiRSa/blob/main/Pictures/2024-12-24T02_40_23.319Z-PXL_20241222_223637513~2(1).jpg)

## Getting Started

### WiFi Configuration

1. Connect to your computer via serial port (default: 9600 baud, 8N1)
2. Enter your WiFi credentials using AT commands (2.4 GHz networks only):
   ```
   AT$SSID=YourNetworkName
   AT$PASS=YourPassword
   ```
3. Save settings: `AT&W`
4. Reboot or reconnect to apply

### Connecting to a BBS

Use the dial command with the host and port:
```
ATDT bbs.example.com:23
```

Or configure speed dial entries:
```
AT&Z0=bbs.example.com:23
ATDS0
```

### Useful AT Commands

| Command | Description |
|---------|-------------|
| `AT` | Test connection (returns OK) |
| `ATDT<host:port>` | Dial/connect to TCP host |
| `ATDS<0-9>` | Speed dial entry |
| `ATH` | Hang up connection |
| `ATA` | Answer incoming connection |
| `ATO` | Return online from command mode |
| `+++` | Escape to command mode (during connection) |
| `ATE0/1` | Echo off/on |
| `ATV0/1` | Verbose results off/on |
| `ATI` | Display network info |
| `AT&V` | View current settings |
| `AT&W` | Save settings to EEPROM |
| `ATZ` | Load settings from EEPROM |
| `AT&F` | Restore factory defaults |
| `AT&K<0/1/2>` | Flow control (0=none, 1=hardware, 2=software) |
| `ATNET<0/1>` | Telnet protocol handling off/on |
| `ATPET=<0/1>` | PETSCII translation off/on |
| `AT$SSID=` | Set WiFi SSID |
| `AT$PASS=` | Set WiFi password |
| `AT$SP=<port>` | Set listening port |
| `ATS0=<0/1>` | Auto-answer off/on |
| `ATFC` | Check for firmware updates |
| `ATFU` | Perform firmware update |
| `AT?` or `ATHELP` | Display help |
| `ATX` | Exit MODEM Mode, return to WiRSa main menu |

## File Transfers

The WiRSa supports binary file transfers directly from the SD card using industry-standard protocols:

- **XModem** - Basic 128-byte block transfer with checksum
- **YModem** - Batch file transfer with filename and size
- **ZModem** - Streaming protocol with CRC16/CRC32 and automatic resume

To transfer files:
1. Insert SD card with files
2. Select "File Transfer" from the main menu
3. Choose the transfer protocol
4. Select send or receive
5. Initiate the transfer from your terminal software

![WiRSa PCB Action](https://github.com/nullvalue0/WiRSa/blob/main/Pictures/2024-12-24T02_40_23.319Z-PXL_20241222_223612533~2.jpg)


## SLIP / PPP Gateway Mode

The PPP Gateway mode enables TCP/IP networking for vintage computers with PPP client support. This allows systems like classic Macs, Windows 3.x, or other retro computers to browse the web and access network services. Tested with Windows 98 Dial-up Networking and ETHERSL/mTCP under DOS. You can activate these gateway modes directly from their menu (over serial or on OLED display), or by dialing a special number. Call '777' or 'PPP' to enable the PPP gateway. PPP automatically assigns an IP address, so leave DHCP selected in your settings. Call '7547' or 'SLIP' to enable the SLIP gateway. SLIP requires you to manually specify your IP address. Try using Gateway: 192.168.7.1, Client: 192.168.7.2, DNS Server: 8.8.8.8. Remember to set these values both in the WiRSa SLIP menu and in your networking configurating on the computer.

## Diagnostics

The Utilities menu provides diagnostic tools for troubleshooting:

- **Signal Monitor** - Real-time display of RS232 signal states (DCD, RTS, CTS, DTR, DSR, RI)
- **Loopback Test** - Test serial communication integrity
- **Statistics** - View connection counts and byte transfer statistics

## Hardware Connections

### Pin Assignments

| Function | GPIO | Description |
|----------|------|-------------|
| RXD2 | 16 | UART2 RX (from computer) |
| TXD2 | 17 | UART2 TX (to computer) |
| DCD | 33 | Data Carrier Detect (output) |
| RTS | 15 | Request to Send (output) |
| CTS | 27 | Clear to Send (input) |
| DTR | 4 | Data Terminal Ready |
| DSR | 26 | Data Set Ready |
| RI | 25 | Ring Indicator |
| SD CS | 5 | SD Card Chip Select |
| SD MOSI | 23 | SD Card MOSI |
| SD MISO | 19 | SD Card MISO |
| SD SCK | 18 | SD Card Clock |

### Navigation Buttons

| Button | GPIO | Function |
|--------|------|----------|
| SW1 | 36 | DOWN |
| SW2 | 39 | BACK |
| SW3 | 34 | ENTER |
| SW4 | 35 | UP |

## Building the Firmware

This is a PlatformIO project. See [README-PLATFORMIO.md](README-PLATFORMIO.md) for detailed build instructions.

Quick start:
```bash
# Build firmware
platformio run

# Upload to ESP32
platformio run --target upload

# Monitor serial output
platformio device monitor
```

## Origin Story

The WiRSa originated as a film prop for Apple TV's "The Shining Girls," where it simulated realistic computer interaction by playing back pre-recorded text files while actors typed on vintage terminals.

![WiRSa On Film](https://github.com/nullvalue0/WiRSa/blob/main/Pictures/2024-12-24T02_32_21.541Z-IMG_20241223_105522_2~2(1).jpg)

## Resources

- **Source Code:** Complete firmware, schematics (KiCAD), Gerber files, and enclosure designs (OpenSCAD) available in this repository
- **Contact:** nullvalue@gmail.com

## License

MIT License - See source code header for full license text.

Copyright (C) 2026 Aron Hoekstra

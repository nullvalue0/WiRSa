/**************************************************************************
   RetroDisks WiRSa v3 - Wi-Fi RS232 Serial Modem Adapter with File Transfer features
   Copyright (C) 2024 Aron Hoekstra <nullvalue@gmail.com>

   based on:
     Virtual modem for ESP8266
     https://github.com/jsalin/esp8266_modem
     Copyright (C) 2015 Jussi Salin <salinjus@gmail.com>
   
   and the improvements added by:
     WiFi SIXFOUR - A virtual WiFi modem based on the ESP 8266 chipset by Alwyz
     https://1200baud.wordpress.com/2017/03/04/build-your-own-9600-baud-c64-wifi-modem-for-20/
   
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
 
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
 
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 **************************************************************************
 This software uses libraries published by Adafruit for LCD screen control
 Adafruit_SSD1306 & Adafruit_GFX.
 **************************************************************************
 This software uses the LinkedList library by Ivan Seidel
 http://github.com/ivanseidel/LinkedList
 **************************************************************************
 This software uses the arduino-timer libary by
 https://github.com/contrem/arduino-timer
 **************************************************************************/

 /*
 changes 3.03
 - restructured code, using platform.io now instead of Arduino IDE
 - fixed SD card r/w
 - fixed IP display (ATI etc)
 - added ZModem support
 - fix OTA update
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <LinkedList.h>
//#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SD.h"
//#include "FS.h"
#include "CRC16.h"
#include "CRC.h"
#include "arduino-timer.h"
#include <HardwareSerial.h>

// Project modules
#include "globals.h"  // ZModem constants and structures
#include "serial_io.h"
#include "settings.h"
#include "network.h"
#include "display_menu.h"  // ✅ Phase 2 complete
#include "firmware.h"      // ✅ Phase 3 complete
#include "playback.h"      // ✅ Phase 4 complete
#include "file_transfer.h" // ✅ Phase 5 complete
#include "modem.h"         // ✅ Phase 6 complete

#define VERSIONA 0
#define VERSIONB 1
#define VERSION_ADDRESS 0    // EEPROM address
#define VERSION_LEN     2    // Length in bytes
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97   // for future use
#define STATIC_IP_ADDRESS 98 // length 4, for future use
#define STATIC_GW       102  // length 4, for future use
#define STATIC_DNS      106  // length 4, for future use
#define STATIC_MASK     110  // length 4, for future use
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define SERVER_PORT_ADDRESS 113 // 2 bytes
#define AUTO_ANSWER_ADDRESS 115 // 1 byte
#define TELNET_ADDRESS  116     // 1 byte
#define VERBOSE_ADDRESS 117
#define PET_TRANSLATE_ADDRESS 118
#define FLOW_CONTROL_ADDRESS 119
#define PIN_POLARITY_ADDRESS 120
#define DIAL0_ADDRESS   200
#define DIAL1_ADDRESS   250
#define DIAL2_ADDRESS   300
#define DIAL3_ADDRESS   350
#define DIAL4_ADDRESS   400
#define DIAL5_ADDRESS   450
#define DIAL6_ADDRESS   500
#define DIAL7_ADDRESS   550
#define DIAL8_ADDRESS   600
#define DIAL9_ADDRESS   650
#define BUSY_MSG_ADDRESS 700
#define BUSY_MSG_LEN    80
#define ORIENTATION_ADDRESS 780
#define DEFAULTMODE_ADDRESS 790
#define SERIALCONFIG_ADDRESS 791
#define LAST_ADDRESS    800

#define SWITCH_PIN 0       // GPIO0 (programmind mode pin)
#define LED_PIN 2          //LED_BUILTIN          // Status LED

#define DCD_PIN 33         // DCD Carrier Status
#define RTS_PIN 15         // RTS Request to Send, connect to host's CTS pin
#define CTS_PIN 27         // CTS Clear to Send, connect to host's RTS pin

#define DTR_PIN 4          // DTR Data Terminal Ready (not yet implemented)
#define DSR_PIN 26         // DSR Data Set Ready (not yet implemented)
#define RI_PIN  25         // RI Ring Indicator (not yet implemented)

#define SW1_PIN 36         //DOWN
#define SW2_PIN 39         //BACK
#define SW3_PIN 34         //ENTER
#define SW4_PIN 35         //UP

#define MODE_MAIN 0
#define MODE_MODEM 1
#define MODE_FILE 2
#define MODE_PLAYBACK 3
#define MODE_SETTINGS 4
#define MODE_SETBAUD 5
#define MODE_LISTFILE 6
#define MODE_ORIENTATION 7
#define MODE_DEFAULTMODE 8
#define MODE_PROTOCOL 9
#define MODE_SERIALCONFIG 10

#define MENU_BOTH 0 //show menu on both serial port & display using first letter as selector
#define MENU_NUM  1 //show menu on both serial port & display but serial uses a numeric selector - has a limit of 10 items because the input currently returns after 1 character entry
#define MENU_DISP 2 //show menu on display only

#define XFER_SEND 0
#define XFER_RECV 1

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//SD Card Pins
#define SD_CS 5
#define SPI_MOSI 23 
#define SPI_MISO 19
#define SPI_SCK 18  

#define RXD2 16
#define TXD2 17

// ZMODEM constants are defined in globals.h
// ZMODEM Context - comprehensive protocol state
ZModemContext zmCtx;

uint8_t zBuf[4096];  // Increased from 2048 to 4096 to reduce CRC errors from WiFi interrupt delays

// CRC-16 CCITT table
const uint16_t crc16_ccitt_table[256] PROGMEM = {
  0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
  0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
  0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
  0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
  0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
  0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
  0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
  0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
  0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
  0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
  0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
  0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
  0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
  0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
  0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
  0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
  0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
  0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
  0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
  0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
  0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
  0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
  0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
  0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
  0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
  0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
  0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
  0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
  0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
  0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
  0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
  0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

// CRC-32 calculation
const uint32_t crc32tab[256] PROGMEM = {
  0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
  0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
  0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
  0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
  0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
  0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
  0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
  0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
  0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
  0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
  0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
  0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
  0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
  0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
  0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
  0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
  0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
  0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
  0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
  0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
  0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
  0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
  0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
  0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
  0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
  0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
  0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
  0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
  0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
  0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
  0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
  0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D
};


// Global variables
String build = "v3.03";
String cmd = "";              // Gather a new AT command to this string from serial
bool cmdMode = true;          // Are we in AT command mode or connected mode
bool callConnected = false;   // Are we currently in a call
bool telnet = false;          // Is telnet control code handling enabled
bool verboseResults = false;
//#define DEBUG 1             // Print additional debug information to serial channel
#undef DEBUG
#define LISTEN_PORT 23        // Listen to this if not connected. Set to zero to disable.
int tcpServerPort = LISTEN_PORT;
#define RING_INTERVAL 3000    // How often to print RING when having a new incoming connection (ms)
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
//long myBps;                 // What is the current BPS setting
#define MAX_CMD_LENGTH 256    // Maximum length for AT command
char plusCount = 0;           // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;   // When did we last receive a "+++" sequence
#define LED_TIME 15           // How many ms to keep LED on at activity
unsigned long ledTime = 0;
#define TX_BUF_SIZE 256       // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];
extern const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];

byte serialConfig;
extern const int bits[] = { SERIAL_5N1, SERIAL_6N1, SERIAL_7N1, SERIAL_8N1, SERIAL_5N2, SERIAL_6N2, SERIAL_7N2, SERIAL_8N2, SERIAL_5E1, SERIAL_6E1, SERIAL_7E1, SERIAL_8E1, SERIAL_5E2, SERIAL_6E2, SERIAL_7E2, SERIAL_8E2, SERIAL_5O1, SERIAL_6O1, SERIAL_7O1, SERIAL_8O1, SERIAL_5O2, SERIAL_6O2, SERIAL_7O2, SERIAL_8O2 };
String bitsDisp[] = { "5-N-1", "6-N-1", "7-N-1", "8-N-1 (default)", "5-N-2", "6-N-2", "7-N-2", "8-N-2", "5-E-1", "6-E-1", "7-E-1", "8-E-1", "5-E-2", "6-E-2", "7-E-2", "8-E-2", "5-O-1", "6-O-1", "7-O-1", "8-O-1", "5-O-2", "6-O-2", "7-O-2", "8-O-2" };

extern const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
String baudDisp[] = { "300", "1200", "2400", "4800", "9600", "19.2k", "38.4k", "57.6k", "115k" };
byte serialSpeed;

String mainMenuDisp[] = { "MODEM Mode", "File Transfer", "Playback Text", "Settings" };
String settingsMenuDisp[] = { "MAIN", "Baud Rate", "Serial Config", "Orientation", "Default Menu", "Factory Reset", "Reboot", "About" };
String orientationMenuDisp[] = { "Normal", "Flipped" };
String playbackMenuDisp[] = { "MAIN", "List Files", "Display File", "Playback File", "Evaluate Key", "Terminal Mode" };
String fileMenuDisp[] = { "MAIN", "List Files on SD", "Send (from SD)", "Recieve (to SD)" };
String protocolMenuDisp[] = { "BACK", "Raw", "XModem", "YModem", "ZModem" }; //, "Kermit" };
String defaultModeDisp[] = { "Main Menu", "MODEM Mode" };

bool echo = true;
bool autoAnswer = false;
String ssid, password, busyMsg;
byte ringCount = 0;
String resultCodes[] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE", "BUSY", "NO ANSWER" };
enum resultCodes_t { R_OK_STAT, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
unsigned long connectTime = 0;
bool petTranslate = false; // Fix PET MCTerm 1.26C Pet->ASCII encoding to actual ASCII
bool bHex = false;
enum flowControl_t { F_NONE, F_HARDWARE, F_SOFTWARE };
byte flowControl = F_NONE;      // Use flow control
bool txPaused = false;          // Has flow control asked us to pause?
enum pinPolarity_t { P_INVERTED, P_NORMAL }; // Is LOW (0) or HIGH (1) active?
byte pinPolarity = P_INVERTED;
enum dispOrientation_t { D_NORMAL, D_FLIPPED }; // Normal or Flipped
byte dispOrientation = D_NORMAL;
enum defaultMode_t { D_MAINMENU, D_MODEMMENU }; // Main or Modem
byte defaultMode = D_MAINMENU;

int menuMode = MODE_MAIN;
int xferMode = XFER_SEND;
int lastMenu = -1;
int menuCnt = 0;
int menuIdx = 0;
int dispCharCnt = 0;
int dispLineCnt = 0;
int dispAnsiCnt = 0;
unsigned long xferBytes = 0;
unsigned int xferBlock = 0;
String xferMsg = "";
unsigned long bytesSent = 0;
unsigned long bytesRecv = 0;
bool sentChanged = false;
bool recvChanged = false;
bool speedDialShown = false;

//file transfer related variables
const int chipSelect = 5; //D8
byte packetNumber=0;  //which packet number we're on - YMODEM starts with 0 which contains file name and size.
int blockSize = 1024;
int packetSize = 1029;
bool lastPacket = false; //did i just send the last packet? if so, next thing to send is EOT
int eotSent = 0; //did i send the EOT? if so, wait for ACK
bool finalFileSent = false;
bool readyToReceive = false;
int fileIndex = 0; //used to keep track of what file we're on for batch transfers
bool zeroPacket = true;
File logFile;
File xferFile;
bool EscapeTelnet = false;
CRC16 crc;
auto timer = timer_create_default();
Timer<16> modem_timer = timer_create_default();
String terminalMode = "VT100";
String fileName = "";
String files[100];
String waitTime;
bool msgFlag=false;
bool BTNUP=false;
bool BTNDN=false;
bool BTNEN=false;
bool BTNBK=false;

extern const uint8_t PROGMEM wifi_symbol[] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b00100100,
  0b01000010,
  0b00011000,
  0b00100100
};

extern const uint8_t PROGMEM phon_symbol[] = {
  0b00111100,
  0b11000011,
  0b11011011,
  0b00111100,
  0b01111110,
  0b01111110,
  0b01111110,
  0b01111110
};

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClient tcpClient;
WiFiServer tcpServer(LISTEN_PORT);        // port will be set via .begin(port)
WebServer webServer(80);
MDNSResponder mdns;
HardwareSerial PhysicalSerial(2);

// Serial I/O functions moved to src/modules/serial_io.cpp
// Settings functions moved to src/modules/settings.cpp
// Network functions moved to src/modules/network.cpp

// Forward declarations for functions still in main.cpp
void showMessage(String message);
void modemMenuMsg();
void modemConnected();
void firmwareCheck();
void firmwareUpdate();
void firmwareUpdateStarted();
void firmwareUpdateFinished();
void firmwareUpdateProgress(int cur, int total);
void firmwareUpdateError(int err);
void enterModemMode();
void mainLoop();
void modemLoop();
void fileLoop();
void playbackLoop();
void settingsLoop();
void baudLoop();
void serialLoop();
void listFilesLoop();
void orientationLoop();
void defaultModeLoop();
void protocolLoop();
void showMenu(String menuName, String options[], int sz, int dispMode, int defaultSel);
void menuOption(int idx, String item);
void fileMenu(bool arrow);
void playbackMenu(bool arrow);
void settingsMenu(bool arrow);
void baudMenu(bool arrow);
void serialMenu(bool arrow);
void orientationMenu(bool arrow);
void defaultModeMenu(bool arrow);
void listFilesMenu(bool arrow);
void listFiles();
String getLine();
void clearInputBuffer();
bool checkCancel();
void resetGlobals();
String getFileSize(String fileName);
void sendFileRaw();
void receiveFileRaw();
void sendFileXMODEM();
void receiveFileXMODEM();
void sendFileYMODEM();
void receiveFileYMODEM();
void sendLoopXMODEM(File dataFile, String fileName, String fileSize);
void sendLoopYMODEM(File dataFile, String fileName, String fileSize);
void sendPacket(File dataFile);
void sendZeroPacket(String fileName, String fileSize);
void sendFinalPacket();
void sendTelnet(uint8_t* p, int len);
void receiveLoopXMODEM();
void receiveLoopYMODEM();
void changeTerminalMode();
void evalKey();
char getKey();
void waitKey(int k1, int k2);
bool displayFile(String fileName, bool playback, int startPos);
void displayChar(char c, int direction);
void addLog(String s);
bool refreshDisplay(void* argument);

// ============================================================================
// Modem functions moved to src/modules/modem.cpp
// ============================================================================
// The following functions have been extracted to modem.cpp:
// - connectTimeString(), sendResult(), sendString(), checkButton()
// - displayCurrentSettings(), displayStoredSettings()
// - waitForSpace(), waitForEnter(), displayHelp(), storeSpeedDial()
// - prompt(), hangUp(), handleWebHangUp(), handleRoot()
// - led_on(), answerCall(), handleIncomingConnection(), dialOut()
// - command(), handleFlowControl(), enterModemMode()
// - mainLoop(), baudLoop(), serialLoop(), orientationLoop()
// - defaultModeLoop(), settingsLoop(), fileLoop(), protocolLoop()
// - listFilesLoop(), modemLoop()
// - refreshDisplay(), clearInputBuffer(), getLine()
// ============================================================================

// File transfer functions (XModem/YModem/ZModem) moved to src/modules/file_transfer.cpp
// - zmCalcCRC32(), zmSendZDLE(), zmSendHexHdr(), zmRecvByte(), zmRecvZDLE(), zmRecvHexHdr()
// - sendFileZMODEM(), receiveFileZMODEM(), sendFileXMODEM(), receiveFileXMODEM()
// - sendFileYMODEM(), receiveFileYMODEM(), sendFileRaw(), receiveFileRaw()
// - sendLoopXMODEM(), sendLoopYMODEM(), receiveLoopXMODEM(), receiveLoopYMODEM()
// - sendPacket(), sendZeroPacket(), sendFinalPacket(), sendTelnet()
// - resetGlobals(), checkCancel(), clearInputBuffer(), SerialWriteLog(), addLog(), sendStartByte()

void setup() {
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.display();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  delay(2000); // Pause for 2 seconds
  display.clearDisplay();
  display.setCursor(0, 0);     // Start at top-left corner

  pinMode(LED_PIN, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  //SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  digitalWrite(LED_PIN, HIGH); // off
  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH);
  pinMode(SW1_PIN, INPUT);
  pinMode(SW2_PIN, INPUT);
  pinMode(SW3_PIN, INPUT);
  pinMode(SW4_PIN, INPUT);
  
  //begin flow control
  pinMode(DCD_PIN, OUTPUT);
  pinMode(RTS_PIN, OUTPUT);
  digitalWrite(RTS_PIN, HIGH); // ready to receive data
  pinMode(CTS_PIN, INPUT);
  digitalWrite(CTS_PIN, HIGH); // pull up
  //end flow control

  setCarrier(false);

  EEPROM.begin(LAST_ADDRESS + 1);
  delay(10);

  if (EEPROM.read(VERSION_ADDRESS) != VERSIONA || EEPROM.read(VERSION_ADDRESS + 1) != VERSIONB) {
    defaultEEPROM();
  }

  readSettings();
  // Check if it's out of bounds-- we have to be able to talk
  if (serialSpeed < 0 || serialSpeed > sizeof(bauds)) {
    serialSpeed = 4; //9600
  }
  if (serialConfig < 0 || serialConfig > sizeof(bits)) {
    serialConfig = 3; //8-N-1
  }

  if (dispOrientation==D_NORMAL)
    display.setRotation(0);
  else
    display.setRotation(2);

 
  Serial.setRxBufferSize(4096);
  Serial.begin(115200, SERIAL_8N1); //USB Serial always runs at 115k
  PhysicalSerial.setRxBufferSize(4096);  // Larger buffer for ZModem transfers (reduces CRC errors from WiFi delays)
  PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig], RXD2, TXD2); //Physical Serial

  SerialPrintLn("");
  SerialPrintLn("-= RetroDisks  WiRSa =-");
 
  if (defaultMode==MODE_MAIN)
    mainMenu(false);
  else if (defaultMode==MODE_MODEM)
    enterModemMode();
}

/**
   Arduino main loop function
*/
void loop()
{
  if (menuMode==MODE_MAIN)
    mainLoop();
  else if (menuMode==MODE_MODEM)
    modemLoop();
  else if (menuMode==MODE_FILE)
    fileLoop();
  else if (menuMode==MODE_PLAYBACK)
    playbackLoop();
  else if (menuMode==MODE_SETTINGS)
    settingsLoop();
  else if (menuMode==MODE_SETBAUD)
    baudLoop();    
  else if (menuMode==MODE_SERIALCONFIG)
    serialLoop();    
  else if (menuMode==MODE_LISTFILE)
    listFilesLoop();
  else if (menuMode==MODE_ORIENTATION)
    orientationLoop();
  else if (menuMode==MODE_DEFAULTMODE)
    defaultModeLoop();
  else if (menuMode==MODE_PROTOCOL)
    protocolLoop();
}
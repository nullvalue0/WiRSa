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
 

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <LinkedList.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SD.h"
#include "FS.h"
#include "CRC16.h"
#include "CRC.h"
#include "arduino-timer.h"
#include <HardwareSerial.h>

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

// Global variables
String build = "v3.02";
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
const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];

byte serialConfig;
const int bits[] = { SERIAL_5N1, SERIAL_6N1, SERIAL_7N1, SERIAL_8N1, SERIAL_5N2, SERIAL_6N2, SERIAL_7N2, SERIAL_8N2, SERIAL_5E1, SERIAL_6E1, SERIAL_7E1, SERIAL_8E1, SERIAL_5E2, SERIAL_6E2, SERIAL_7E2, SERIAL_8E2, SERIAL_5O1, SERIAL_6O1, SERIAL_7O1, SERIAL_8O1, SERIAL_5O2, SERIAL_6O2, SERIAL_7O2, SERIAL_8O2 };
String bitsDisp[] = { "5-N-1", "6-N-1", "7-N-1", "8-N-1 (default)", "5-N-2", "6-N-2", "7-N-2", "8-N-2", "5-E-1", "6-E-1", "7-E-1", "8-E-1", "5-E-2", "6-E-2", "7-E-2", "8-E-2", "5-O-1", "6-O-1", "7-O-1", "8-O-1", "5-O-2", "6-O-2", "7-O-2", "8-O-2" };

const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
String baudDisp[] = { "300", "1200", "2400", "4800", "9600", "19.2k", "38.4k", "57.6k", "115k" };
byte serialSpeed;

String mainMenuDisp[] = { "MODEM Mode", "File Transfer", "Playback Text", "Settings" };
String settingsMenuDisp[] = { "MAIN", "Baud Rate", "Serial Config", "Orientation", "Default Menu", "Factory Reset", "Reboot", "About" };
String orientationMenuDisp[] = { "Normal", "Flipped" };
String playbackMenuDisp[] = { "MAIN", "List Files", "Display File", "Playback File", "Evaluate Key", "Terminal Mode" };
String fileMenuDisp[] = { "MAIN", "List Files on SD", "Send (from SD)", "Recieve (to SD)" };
String protocolMenuDisp[] = { "BACK", "Raw", "XModem", "YModem" }; //"ZModem", "Kermit" };
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
auto modem_timer = timer_create_default();
String terminalMode = "VT100";
String fileName = "";
String files[100];
String waitTime;
bool msgFlag=false;
bool BTNUP=false;
bool BTNDN=false;
bool BTNEN=false;
bool BTNBK=false;

const uint8_t PROGMEM wifi_symbol[] = {
  0b00111100,
  0b01000010,
  0b10011001,
  0b00100100,
  0b01000010,
  0b00011000,
  0b00100100
};

const uint8_t PROGMEM phon_symbol[] = {
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

void SerialPrintLn(String s) {
  Serial.println(s);
  PhysicalSerial.println(s);
}

void SerialPrintLn(char c, int format) {
  Serial.println(c, format);
  PhysicalSerial.println(c, format);
}

void SerialPrintLn(char c) {
  Serial.println(c);
  PhysicalSerial.println(c);
}

void SerialPrint(String s) {
  Serial.print(s);
  PhysicalSerial.print(s);
}

void SerialPrint(char c) {
  Serial.print(c);
  PhysicalSerial.print(c);
}

void SerialPrint(char c, int format) {
  Serial.print(c, format);
  PhysicalSerial.print(c, format);
}

void SerialPrintLn() {
  Serial.println();
  PhysicalSerial.println();
}

void SerialFlush() {
  Serial.flush();
  PhysicalSerial.flush();
}

int SerialAvailable() {
  int c = Serial.available();
  if (c==0)
    c = PhysicalSerial.available();
  return c;
}

int SerialRead() {
  int c = Serial.available();
  if (c>0)
    return Serial.read();
  else
    return PhysicalSerial.read();
}
 
void SerialWrite(int c) {
  Serial.write(c);
  PhysicalSerial.write(c);
}

String connectTimeString() {
  unsigned long now = millis();
  int secs = (now - connectTime) / 1000;
  int mins = secs / 60;
  int hours = mins / 60;
  String out = "";
  if (hours < 10) out.concat("0");
  out.concat(String(hours));
  out.concat(":");
  if (mins % 60 < 10) out.concat("0");
  out.concat(String(mins % 60));
  out.concat(":");
  if (secs % 60 < 10) out.concat("0");
  out.concat(String(secs % 60));
  return out;
}

void writeSettings() {
  setEEPROM(ssid, SSID_ADDRESS, SSID_LEN);
  setEEPROM(password, PASS_ADDRESS, PASS_LEN);
  setEEPROM(busyMsg, BUSY_MSG_ADDRESS, BUSY_MSG_LEN);

  EEPROM.write(BAUD_ADDRESS, serialSpeed);
  EEPROM.write(ECHO_ADDRESS, byte(echo));
  EEPROM.write(AUTO_ANSWER_ADDRESS, byte(autoAnswer));
  EEPROM.write(SERVER_PORT_ADDRESS, highByte(tcpServerPort));
  EEPROM.write(SERVER_PORT_ADDRESS + 1, lowByte(tcpServerPort));
  EEPROM.write(TELNET_ADDRESS, byte(telnet));
  EEPROM.write(VERBOSE_ADDRESS, byte(verboseResults));
  EEPROM.write(PET_TRANSLATE_ADDRESS, byte(petTranslate));
  EEPROM.write(FLOW_CONTROL_ADDRESS, byte(flowControl));
  EEPROM.write(PIN_POLARITY_ADDRESS, byte(pinPolarity));
  EEPROM.write(ORIENTATION_ADDRESS, byte(dispOrientation));
  EEPROM.write(DEFAULTMODE_ADDRESS, byte(defaultMode));
  EEPROM.write(SERIALCONFIG_ADDRESS, serialConfig);
   
  for (int i = 0; i < 10; i++) {
    setEEPROM(speedDials[i], speedDialAddresses[i], 50);
  }
  EEPROM.commit();
}

void readSettings() {
  echo = EEPROM.read(ECHO_ADDRESS);
  autoAnswer = EEPROM.read(AUTO_ANSWER_ADDRESS);
  serialSpeed = EEPROM.read(BAUD_ADDRESS);

  ssid = getEEPROM(SSID_ADDRESS, SSID_LEN);
  password = getEEPROM(PASS_ADDRESS, PASS_LEN);
  busyMsg = getEEPROM(BUSY_MSG_ADDRESS, BUSY_MSG_LEN);
  tcpServerPort = word(EEPROM.read(SERVER_PORT_ADDRESS), EEPROM.read(SERVER_PORT_ADDRESS + 1));
  telnet = EEPROM.read(TELNET_ADDRESS);
  verboseResults = EEPROM.read(VERBOSE_ADDRESS);
  petTranslate = EEPROM.read(PET_TRANSLATE_ADDRESS);
  flowControl = EEPROM.read(FLOW_CONTROL_ADDRESS);
  pinPolarity = EEPROM.read(PIN_POLARITY_ADDRESS);
  dispOrientation = EEPROM.read(ORIENTATION_ADDRESS);
  defaultMode = EEPROM.read(DEFAULTMODE_ADDRESS);
  serialConfig = EEPROM.read(SERIALCONFIG_ADDRESS);

  for (int i = 0; i < 10; i++) {
    speedDials[i] = getEEPROM(speedDialAddresses[i], 50);
  }
}

void defaultEEPROM() {
  EEPROM.write(VERSION_ADDRESS, VERSIONA);
  EEPROM.write(VERSION_ADDRESS + 1, VERSIONB);

  setEEPROM("", SSID_ADDRESS, SSID_LEN);
  setEEPROM("", PASS_ADDRESS, PASS_LEN);
  setEEPROM("d", IP_TYPE_ADDRESS, 1);
  EEPROM.write(SERVER_PORT_ADDRESS, highByte(LISTEN_PORT));
  EEPROM.write(SERVER_PORT_ADDRESS + 1, lowByte(LISTEN_PORT));

  EEPROM.write(BAUD_ADDRESS, 0x04);
  EEPROM.write(ECHO_ADDRESS, 0x01);
  EEPROM.write(AUTO_ANSWER_ADDRESS, 0x01);
  EEPROM.write(TELNET_ADDRESS, 0x00);
  EEPROM.write(VERBOSE_ADDRESS, 0x01);
  EEPROM.write(PET_TRANSLATE_ADDRESS, 0x00);
  EEPROM.write(FLOW_CONTROL_ADDRESS, 0x00);
  EEPROM.write(PIN_POLARITY_ADDRESS, 0x01);
  EEPROM.write(SERIALCONFIG_ADDRESS, 0x03); //8-N-1

  setEEPROM("bbs.fozztexx.com:23", speedDialAddresses[0], 50);
  setEEPROM("cottonwoodbbs.dyndns.org:6502", speedDialAddresses[1], 50);
  setEEPROM("borderlinebbs.dyndns.org:6400", speedDialAddresses[2], 50);
  setEEPROM("particlesbbs.dyndns.org:6400", speedDialAddresses[3], 50);
  setEEPROM("reflections.servebbs.com:23", speedDialAddresses[4], 50);
  setEEPROM("heatwave.ddns.net:9640", speedDialAddresses[5], 50);
  setEEPROM("bat.org:23", speedDialAddresses[6], 50);
  setEEPROM("blackflag.acid.org:23", speedDialAddresses[7], 50);
  setEEPROM("cavebbs.homeip.net:23", speedDialAddresses[8], 50);
  setEEPROM("vert.synchro.net:23", speedDialAddresses[9], 50);
 
  setEEPROM("SORRY, SYSTEM IS CURRENTLY BUSY. PLEASE TRY AGAIN LATER.", BUSY_MSG_ADDRESS, BUSY_MSG_LEN);

  EEPROM.write(ORIENTATION_ADDRESS, 0x00);
  EEPROM.write(DEFAULTMODE_ADDRESS, 0x00);
  EEPROM.commit();
}

String getEEPROM(int startAddress, int len) {
  String myString;

  for (int i = startAddress; i < startAddress + len; i++) {
    if (EEPROM.read(i) == 0x00) {
      break;
    }
    myString += char(EEPROM.read(i));
    //SerialPrint(char(EEPROM.read(i)));
  }
  //SerialPrintLn();
  return myString;
}

void setEEPROM(String inString, int startAddress, int maxLen) {
  for (int i = startAddress; i < inString.length() + startAddress; i++) {
    EEPROM.write(i, inString[i - startAddress]);
    //SerialPrint(i, DEC); SerialPrint(": "); SerialPrintLn(inString[i - startAddress]);
    //if (EEPROM.read(i) != inString[i - startAddress]) { SerialPrint(" (!)"); }
    //SerialPrintLn();
  }
  // null pad the remainder of the memory space
  for (int i = inString.length() + startAddress; i < maxLen + startAddress; i++) {
    EEPROM.write(i, 0x00);
    //SerialPrint(i, DEC); SerialPrintLn(": 0x00");
  }
}

void sendResult(int resultCode) {
  SerialPrint("\r\n");
  if (verboseResults == 0) {  
    SerialPrintLn(resultCode);
    return;
  }
  if (resultCode == R_CONNECT) {
    SerialPrint(String(resultCodes[R_CONNECT]) + " " + String(bauds[serialSpeed]));
  } else if (resultCode == R_NOCARRIER) {
    SerialPrint(String(resultCodes[R_NOCARRIER]) + " (" + connectTimeString() + ")");
  } else {
    SerialPrint(String(resultCodes[resultCode]));
  }
  SerialPrint("\r\n");
}

void sendString(String msg) {
  SerialPrint("\r\n");
  SerialPrint(msg);
  SerialPrint("\r\n");
}

// Hold for 5 seconds to switch to 300 baud
// Slow flash: keep holding
// Fast flash: let go
int checkButton() {
  long time = millis();
  while (digitalRead(SWITCH_PIN) == LOW && millis() - time < 5000) {
    delay(250);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    yield();
  }
  if (millis() - time > 5000) {
    PhysicalSerial.flush();
    PhysicalSerial.end();
    serialSpeed = 0;
    delay(100);
    PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig]);
    
    sendResult(R_OK_STAT);
    while (digitalRead(SWITCH_PIN) == LOW) {
      delay(50);
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      yield();
    }
    return 1;
  } else {
    return 0;
  }
}

void connectWiFi() {
  if (ssid == "" || password == "") {
    SerialPrintLn("CONFIGURE SSID AND PASSWORD. TYPE AT? FOR HELP.");
    showMessage("CONFIGURE SSID\nAND PASSWORD\nOVER SERIAL");
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  SerialPrint("\nCONNECTING TO SSID "); SerialPrint(ssid);
  showMessage("* PLEASE WAIT *\n CONNECTING TO\n" + ssid);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    digitalWrite(LED_PIN, LOW);
    delay(250);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    SerialPrint(".");
  }
  SerialPrintLn();
  if (i == 21) {
    SerialPrint("COULD NOT CONNECT TO "); SerialPrintLn(ssid);
    modemMenuMsg();
    display.println("COULD NOT CONNECT\nTO WIFI.\n");
    display.display();

    WiFi.disconnect();
    showWifiIcon();
  } else {
    SerialPrint("CONNECTED TO "); SerialPrintLn(WiFi.SSID());
    SerialPrint("IP ADDRESS: "); SerialPrintLn(WiFi.localIP());
    if (tcpServerPort > 0) {
      SerialPrint("LISTENING ON PORT "); SerialPrintLn(tcpServerPort);
      tcpServer.begin();
    }
    modemConnected();
    showWifiIcon();
    firmwareCheck();
  }
}

void showWifiIcon() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);  // on
    display.drawBitmap(72, 4, wifi_symbol, 8, 7, SSD1306_BLACK);
  } else {
    digitalWrite(LED_PIN, HIGH); //off
    display.fillRect(72, 4, 8, 7, SSD1306_WHITE);
  }
  display.display();
}

void disconnectWiFi() {
  WiFi.disconnect();
  delay(200);
  showWifiIcon();
}

void setBaudRate(int inSpeed) {
  if (inSpeed == 0) {
    sendResult(R_ERROR);
    return;
  }
  int foundBaud = -1;
  for (int i = 0; i < sizeof(bauds); i++) {
    if (inSpeed == bauds[i]) {
      foundBaud = i;
      break;
    }
  }
  // requested baud rate not found, return error
  if (foundBaud == -1) {
    sendResult(R_ERROR);
    return;
  }
  if (foundBaud == serialSpeed) {
    sendResult(R_OK_STAT);
    return;
  }
  SerialPrint("SWITCHING SERIAL PORT TO ");
  SerialPrint(inSpeed);
  SerialPrintLn(" IN 5 SECONDS");
  delay(5000);
  PhysicalSerial.end();
  delay(200);
  PhysicalSerial.begin(bauds[foundBaud], (SerialConfig)bits[serialConfig], RXD2, TXD2);

  serialSpeed = foundBaud;
  delay(200);
  sendResult(R_OK_STAT);
}

void setCarrier(byte carrier) {
  if (pinPolarity == P_NORMAL) carrier = !carrier;
  digitalWrite(DCD_PIN, carrier);
  showCallIcon();
}

void showCallIcon() {
  if (callConnected)
    display.drawBitmap(85, 4, phon_symbol, 8, 7, SSD1306_BLACK);
  else
    display.fillRect(85, 4, 8, 7, SSD1306_WHITE);
  display.display();
}

void displayNetworkStatus() {
  SerialPrint("WIFI STATUS: ");
  if (WiFi.status() == WL_CONNECTED) {
    SerialPrintLn("CONNECTED");
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    SerialPrintLn("OFFLINE");
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    SerialPrintLn("CONNECT FAILED");
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    SerialPrintLn("SSID UNAVAILABLE");
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    SerialPrintLn("CONNECTION LOST");
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    SerialPrintLn("DISCONNECTED");
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    SerialPrintLn("SCAN COMPLETED");
  }
  yield();

  SerialPrint("SSID.......: "); SerialPrintLn(WiFi.SSID());

  //  SerialPrint("ENCRYPTION: ");
  //  switch(WiFi.encryptionType()) {
  //    case 2:
  //      SerialPrintLn("TKIP (WPA)");
  //      break;
  //    case 5:
  //      SerialPrintLn("WEP");
  //      break;
  //    case 4:
  //      SerialPrintLn("CCMP (WPA)");
  //      break;
  //    case 7:
  //      SerialPrintLn("NONE");
  //      break;
  //    case 8:
  //      SerialPrintLn("AUTO");
  //      break;
  //    default:
  //      SerialPrintLn("UNKNOWN");
  //      break;
  //  }

  byte mac[6];
  WiFi.macAddress(mac);
  SerialPrint("MAC ADDRESS: ");
  SerialPrint(mac[0], HEX);
  SerialPrint(":");
  SerialPrint(mac[1], HEX);
  SerialPrint(":");
  SerialPrint(mac[2], HEX);
  SerialPrint(":");
  SerialPrint(mac[3], HEX);
  SerialPrint(":");
  SerialPrint(mac[4], HEX);
  SerialPrint(":");
  SerialPrintLn(mac[5], HEX);
  yield();

  SerialPrint("IP ADDRESS.: "); SerialPrintLn(WiFi.localIP()); yield();
  SerialPrint("GATEWAY....: "); SerialPrintLn(WiFi.gatewayIP()); yield();
  SerialPrint("SUBNET MASK: "); SerialPrintLn(WiFi.subnetMask()); yield();
  SerialPrint("SERVER PORT: "); SerialPrintLn(tcpServerPort); yield();
  SerialPrint("WEB CONFIG.: HTTP://"); SerialPrintLn(WiFi.localIP()); yield();
  SerialPrint("CALL STATUS: "); yield();
  if (callConnected) {
    SerialPrint("CONNECTED TO "); SerialPrintLn(ipToString(tcpClient.remoteIP())); yield();
    SerialPrint("CALL LENGTH: "); SerialPrintLn(connectTimeString()); yield();
  } else {
  }
}

void displayCurrentSettings() {
  SerialPrintLn("ACTIVE PROFILE:"); yield();
  SerialPrint("BAUD: "); SerialPrintLn(bauds[serialSpeed]); yield();
  SerialPrint("SSID: "); SerialPrintLn(ssid); yield();
  SerialPrint("PASS: "); SerialPrintLn(password); yield();
  //SerialPrint("SERVER TCP PORT: "); SerialPrintLn(tcpServerPort); yield();
  SerialPrint("BUSY MSG: "); SerialPrintLn(busyMsg); yield();
  SerialPrint("E"); SerialPrint(echo); SerialPrint(" "); yield();
  SerialPrint("V"); SerialPrint(verboseResults); SerialPrint(" "); yield();
  SerialPrint("&K"); SerialPrint(flowControl); SerialPrint(" "); yield();
  SerialPrint("&P"); SerialPrint(pinPolarity); SerialPrint(" "); yield();
  SerialPrint("NET"); SerialPrint(telnet); SerialPrint(" "); yield();
  SerialPrint("PET"); SerialPrint(petTranslate); SerialPrint(" "); yield();
  SerialPrint("S0:"); SerialPrint(autoAnswer); SerialPrint(" "); yield();
  SerialPrint("ORIENT:"); SerialPrint(dispOrientation); SerialPrint(" "); yield();
  SerialPrint("DFLTMENU:"); SerialPrint(defaultMode); SerialPrint(" "); yield();
  SerialPrintLn(); yield();

  SerialPrintLn("SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    SerialPrint(i); SerialPrint(": "); SerialPrintLn(speedDials[i]);
    yield();
  }
  SerialPrintLn();
}

void displayStoredSettings() {
  SerialPrintLn("STORED PROFILE:"); yield();
  SerialPrint("BAUD: "); SerialPrintLn(bauds[EEPROM.read(BAUD_ADDRESS)]); yield();
  SerialPrint("SSID: "); SerialPrintLn(getEEPROM(SSID_ADDRESS, SSID_LEN)); yield();
  SerialPrint("PASS: "); SerialPrintLn(getEEPROM(PASS_ADDRESS, PASS_LEN)); yield();
  //SerialPrint("SERVER TCP PORT: "); SerialPrintLn(word(EEPROM.read(SERVER_PORT_ADDRESS), EEPROM.read(SERVER_PORT_ADDRESS+1))); yield();
  SerialPrint("BUSY MSG: "); SerialPrintLn(getEEPROM(BUSY_MSG_ADDRESS, BUSY_MSG_LEN)); yield();
  SerialPrint("E"); SerialPrint(EEPROM.read(ECHO_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("V"); SerialPrint(EEPROM.read(VERBOSE_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("&K"); SerialPrint(EEPROM.read(FLOW_CONTROL_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("&P"); SerialPrint(EEPROM.read(PIN_POLARITY_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("NET"); SerialPrint(EEPROM.read(TELNET_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("PET"); SerialPrint(EEPROM.read(PET_TRANSLATE_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("S0:"); SerialPrint(EEPROM.read(AUTO_ANSWER_ADDRESS)); SerialPrint(" "); yield();
  SerialPrint("ORIENT:"); SerialPrint(EEPROM.read(ORIENTATION_ADDRESS)); SerialPrint(" "); yield();
  SerialPrintLn(); yield();

  SerialPrintLn("STORED SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    SerialPrint(i); SerialPrint(": "); SerialPrintLn(getEEPROM(speedDialAddresses[i], 50));
    yield();
  }
  SerialPrintLn();
}

void waitForSpace() {
  SerialPrint("PRESS SPACE");
  char c = 0;
  while (c != 0x20) {
    if (SerialAvailable() > 0) {
      c = SerialRead();
      if (petTranslate == true){
        if (c > 127) c-= 128;
      }
    }
  }
  SerialPrint("\r");
}

void waitForEnter() {
  SerialPrint("PRESS ENTER");
  char c = 0;
  while (c != 10 && c != 13) {
    if (SerialAvailable() > 0) {
      c = SerialRead();
      if (petTranslate == true){
        if (c > 127) c-= 128;
      }
    }
  }
  SerialPrint("\r");
}

void displayHelp() {
 
  SerialPrintLn("AT COMMAND SUMMARY:"); yield();
  SerialPrintLn("DIAL HOST......: ATDTHOST:PORT"); yield();
  SerialPrintLn("                 ATDTNNNNNNN (N=0-9)"); yield();
  SerialPrintLn("SPEED DIAL.....: ATDSN (N=0-9)"); yield();
  SerialPrintLn("SET SPEED DIAL.: AT&ZN=HOST:PORT (N=0-9)"); yield();
  SerialPrintLn("HANDLE TELNET..: ATNETN (N=0,1)"); yield();
  SerialPrintLn("PET MCTERM TR..: ATPETN (N=0,1)"); yield();
  SerialPrintLn("NETWORK INFO...: ATI"); yield();
  SerialPrintLn("HTTP GET.......: ATGET<URL>"); yield();
  //SerialPrintLn("SERVER PORT....: AT$SP=N (N=1-65535)"); yield();
  SerialPrintLn("AUTO ANSWER....: ATS0=N (N=0,1)"); yield();
  SerialPrintLn("SET BUSY MSG...: AT$BM=YOUR BUSY MESSAGE"); yield();
  SerialPrintLn("LOAD NVRAM.....: ATZ"); yield();
  SerialPrintLn("SAVE TO NVRAM..: AT&W"); yield();
  SerialPrintLn("SHOW SETTINGS..: AT&V"); yield();
  SerialPrintLn("FACT. DEFAULTS.: AT&F"); yield();
  SerialPrintLn("PIN POLARITY...: AT&PN (N=0/INV,1/NORM)"); yield();
  SerialPrintLn("ECHO OFF/ON....: ATE0 / ATE1"); yield();
  SerialPrintLn("VERBOSE OFF/ON.: ATV0 / ATV1"); yield();
  SerialPrintLn("SET SSID.......: AT$SSID=WIFISSID"); yield();
  SerialPrintLn("SET PASSWORD...: AT$PASS=WIFIPASSWORD"); yield();
  waitForSpace();
  SerialPrintLn("SET BAUD RATE..: AT$SB=N (3,12,24,48,96"); yield();
  SerialPrintLn("                 192,384,576,1152)*100"); yield();
  SerialPrintLn("SET PORT.......: AT$SP=PORT"); yield();
  SerialPrintLn("FLOW CONTROL...: AT&KN (N=0/N,1/HW,2/SW)"); yield();
  SerialPrintLn("WIFI OFF/ON....: ATC0 / ATC1"); yield();
  SerialPrintLn("HANGUP.........: ATH"); yield();
  SerialPrintLn("ENTER CMD MODE.: +++"); yield();
  SerialPrintLn("EXIT CMD MODE..: ATO"); yield();
  SerialPrintLn("FIRMWARE CHECK.: ATFC"); yield();
  SerialPrintLn("FIRMWARE UPDATE: ATFU"); yield();
  SerialPrintLn("EXIT MODEM MODE: ATX"); yield();
  SerialPrintLn("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //SerialPrint("STORED "); SerialPrint(num); SerialPrint(": "); SerialPrintLn(location);
}


String padLeft(String s, int len) {
  while (s.length()<len)
  {
    s = " " + s;
  }
  return s;
}

String padRight(String s, int len) {
  while (s.length()<len)
  {
    s = s + " ";
  }
  return s;
}

void mainMenu(bool arrow) {
  menuMode=MODE_MAIN;
  showMenu("MAIN", mainMenuDisp, 4, (arrow?MENU_DISP:MENU_BOTH), 0);
}

//returns true/false depending on if default menu item should be set
bool showHeader(String menu, int dispMode) {
  bool setDef = false;
 
  if (dispMode!=MENU_DISP) {
    SerialPrintLn();
    SerialPrintLn();
    SerialPrintLn("-=-=- " + menu + " MENU -=-=-");
  }
 
  //21 chars wide x 8 lines);
  if (lastMenu != menuMode || msgFlag)
  { //clear the whole display, reset the selction
    msgFlag=false;
    display.clearDisplay();
    menuIdx=0;
    setDef=true;
    lastMenu=menuMode;
    display.drawLine(0,0,0,63,SSD1306_WHITE);
    display.drawLine(127,0,127,63,SSD1306_WHITE);
    display.drawLine(0,63,127,63,SSD1306_WHITE);
    display.drawLine(0,16,127,16,SSD1306_WHITE);
  } else {
    //just clear the selection area
    display.fillRect(6, 17, 8, 46, SSD1306_BLACK);
  }
 
  //display.fillRect(0, 0, 127, 9, SSD1306_WHITE);
  display.fillRect(0, 0, 127, 16, SSD1306_WHITE); //fill in the yellow header area
  
  display.drawLine(1, 1, 126, 1, SSD1306_BLACK);  //add an outline to fill this
  display.drawLine(1, 14, 126, 14, SSD1306_BLACK); //somewhat annoyingly large area
  display.drawLine(1, 1, 1, 14, SSD1306_BLACK);
  display.drawLine(126, 1, 126, 14, SSD1306_BLACK);
  

  //display.drawLine(0,0,0,63,SSD1306_WHITE);
    //display.setFont(&FreeMono9pt7b);
  display.setCursor(4, 4);     // Start at top-left corner
  display.setTextColor(SSD1306_BLACK); // Draw white text
  String line = "WiRSa " + menu;
  display.print(line);  
  String bps = baudDisp[serialSpeed];
  
  display.print(padLeft(bps,20-line.length()));
  display.setTextColor(SSD1306_WHITE); // Draw white text
  //display.setCursor(0, 13);     // Start at top-left corner
  display.setCursor(0, 17);     // Start at top-left corner
  //display.setFont();
  display.display();

  return setDef;
}

void showMessage(String message) {
  //display.fillRect(1, 10, 125, 53, SSD1306_BLACK);
  //display.drawRect(15, 12, 97, 40, SSD1306_WHITE);
  //display.fillRect(17, 14, 93, 36, SSD1306_WHITE);
  display.fillRect(1, 17, 125, 44, SSD1306_BLACK);
  display.drawRect(15, 18, 97, 43, SSD1306_WHITE);
  display.fillRect(17, 20, 93, 39, SSD1306_WHITE);

  int x = 19;
  //int y = 16;
  int y = 21;
  int c= 0;
  display.setCursor(x,y);
  display.setTextColor(SSD1306_BLACK);
 
  for (int i = 0; i < message.length(); i++) {
    if (message[i] == '\n') {
      x = 19; y += 8; c = 0;
      display.setCursor(x, y);
    } else if (c == 15) {
      x = 19; y += 8; c = 0;
      display.setCursor(x, y);
      display.write(message[i]);
    } else {
      display.write(message[i]);
      c++;
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.display();
  msgFlag=true;
}

void updateXferMessage() {
  xferBytes++;
  xferBlock++;
  if (xferBytes==0 || xferBlock==1024) {
    xferBlock=0;
    showMessage(xferMsg + String(xferBytes));
  }
}

void showMenu(String menuName, String options[], int sz, int dispMode, int defaultSel) {
  if (showHeader(menuName, dispMode))
    menuIdx = defaultSel;
 
  menuCnt = sz;

  int menuStart=0;
  int menuEnd=0;

  int maxItems=5; //can only show 5 menu items per page

  if (menuCnt<=maxItems)
    menuEnd=menuCnt;
  else {
    //clear out menu area
    display.fillRect(1, 17, 126, 46, SSD1306_BLACK);
   
    menuStart=menuIdx;
    if (menuIdx<maxItems)
      menuStart=0;
    else
      menuStart=menuIdx-(maxItems-1);
   
    menuEnd=menuStart+maxItems;
    if (menuEnd>menuCnt)
      menuEnd=menuCnt;
  }
  
  display.setCursor(1, 19);
  
  for (int i=menuStart;i<menuEnd;i++) {
    menuOption(i, options[i]);
  }

  if (dispMode!=MENU_DISP) {
    int c = 0;
    for (int i=0;i<menuCnt;i++) {
      if (dispMode==MENU_NUM)
      {
        if (c==2) {
          SerialPrintLn(" [" + String((char)(65+i)) + "] " + options[i]);
          c=0;
        }          
        else {
          SerialPrint(" [" + String((char)(65+i)) + "] " + padRight(options[i], 15));
          c++;
        }
      }
      else //MENU_BOTH
        SerialPrintLn(" [" + options[i].substring(0,1) + "]" + options[i].substring(1));
    }
  }
 
  if (menuStart>0) {
    display.setCursor(120, 19);
    display.write(0x1E);
  }

  if (menuEnd<menuCnt) {
    display.setCursor(120, 54);
    display.write(0x1F);
  }

  display.display();

  if (dispMode!=MENU_DISP) {
    SerialPrintLn();
    SerialPrint("> ");
  }
}

void menuOption(int idx, String item) {
  display.print(" ");
  if (menuIdx==idx)
    display.write(16);
  else
    display.print(" ");
  display.print(" ");
  display.println(item.substring(0,17));
}

/**
   Arduino main init function
*/
void setup() {
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    SerialPrintLn(F("SSD1306 allocation failed"));
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
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
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

 
  Serial.begin(115200, SERIAL_8N1); //USB Serial 
  PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig], RXD2, TXD2); //Physical Serial

  SerialPrintLn("");
  SerialPrintLn("-= RetroDisks  WiRSa =-");
 
  if (defaultMode==MODE_MAIN)
    mainMenu(false);
  else if (defaultMode==MODE_MODEM)
    enterModemMode();
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

void hangUp() {
  tcpClient.stop();
  callConnected = false;
  setCarrier(callConnected);
  sendResult(R_NOCARRIER);
  connectTime = 0;
}

void handleWebHangUp() {
  String t = "NO CARRIER (" + connectTimeString() + ")";
  hangUp();
  webServer.send(200, "text/plain", t);
}

void handleRoot() {
  String page = "WIFI STATUS: ";
  if (WiFi.status() == WL_CONNECTED) {
    page.concat("CONNECTED");
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    page.concat("OFFLINE");
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    page.concat("CONNECT FAILED");
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    page.concat("SSID UNAVAILABLE");
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    page.concat("CONNECTION LOST");
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    page.concat("DISCONNECTED");
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    page.concat("SCAN COMPLETED");
  }
  yield();
  page.concat("\nSSID.......: " + WiFi.SSID());

  byte  mac[6];
  WiFi.macAddress(mac);
  page.concat("\nMAC ADDRESS: ");
  page.concat(String(mac[0], HEX));
  page.concat(":");
  page.concat(String(mac[1], HEX));
  page.concat(":");
  page.concat(String(mac[2], HEX));
  page.concat(":");
  page.concat(String(mac[3], HEX));
  page.concat(":");
  page.concat(String(mac[4], HEX));
  page.concat(":");
  page.concat(String(mac[5], HEX));
  yield();

  page.concat("\nIP ADDRESS.: "); page.concat(ipToString(WiFi.localIP()));
  page.concat("\nGATEWAY....: "); page.concat(ipToString(WiFi.gatewayIP()));
  yield();

  page.concat("\nSUBNET MASK: "); page.concat(ipToString(WiFi.subnetMask()));
  yield();
  page.concat("\nSERVER PORT: "); page.concat(tcpServerPort);
  page.concat("\nCALL STATUS: ");
  if (callConnected) {
    page.concat("CONNECTED TO ");
    page.concat(ipToString(tcpClient.remoteIP()));
    page.concat("\nCALL LENGTH: "); page.concat(connectTimeString()); yield();
  } else {
    page.concat("NOT CONNECTED");
  }
  page.concat("\n");
  webServer.send(200, "text/plain", page);
  delay(100);
}

/**
   Turn on the LED and store the time, so the LED will be shortly after turned off
*/
void led_on()
{
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  ledTime = millis();
}

void readSwitches()
{ //these digital pins are pulled HIGH, pushed=LOW
  //if (dispOrientation==D_NORMAL) {
  BTNUP = !digitalRead(SW1_PIN);
  BTNBK = !digitalRead(SW2_PIN); 
  BTNEN = !digitalRead(SW3_PIN);
  BTNDN = !digitalRead(SW4_PIN);
}

void readBackSwitch()
{ //not sure why but when in a call, SW1 analog read hangs the connection [obsolete: delay(3) workaround resolves]
  //only concerned about 'back' button push anyways, hence this function
  BTNDN = LOW; //must read a0 as an analog, < 100 = pushed
  BTNBK = LOW;
  BTNEN = LOW;
  //SW4 = !digitalRead(D4);
  BTNUP = LOW;
}

void waitSwitches()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(BTNDN||BTNBK||BTNEN||BTNUP) {
      delay(10);
      readSwitches();
    }
  }
}

void waitBackSwitch()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(BTNDN||BTNBK||BTNEN||BTNUP) {
      delay(10);
      readBackSwitch();
    }
  }
}
void answerCall() {
  tcpClient = tcpServer.available();
  tcpClient.setNoDelay(true); // try to disable naggle
  //tcpServer.stop();
  sendResult(R_CONNECT);
  connectTime = millis();
  cmdMode = false;
  callConnected = true;
  setCarrier(callConnected);
  SerialFlush();
}

void handleIncomingConnection() {
  if (callConnected == 1 || (autoAnswer == false && ringCount > 3)) {
    // We're in a call already or didn't answer the call after three rings
    // We didn't answer the call. Notify our party we're busy and disconnect
    ringCount = lastRingMs = 0;
    WiFiClient anotherClient = tcpServer.available();
    anotherClient.print(busyMsg);
    anotherClient.print("\r\n");
    anotherClient.print("CURRENT CALL LENGTH: ");
    anotherClient.print(connectTimeString());
    anotherClient.print("\r\n");
    anotherClient.print("\r\n");
    anotherClient.flush();
    anotherClient.stop();
    return;
  }

  if (autoAnswer == false) {
    if (millis() - lastRingMs > 6000 || lastRingMs == 0) {
      lastRingMs = millis();
      sendResult(R_RING);
      ringCount++;
    }
    return;
  }

  if (autoAnswer == true) {
    tcpClient = tcpServer.available();
    sendString(String("RING ") + ipToString(tcpClient.remoteIP()));
    delay(1000);
    sendResult(R_CONNECT);
    connectTime = millis();
    cmdMode = false;
    tcpClient.flush();
    callConnected = true;
    setCarrier(callConnected);
  }
}

void dialOut(String upCmd) {
  // Can't place a call while in a call
  if (callConnected) {
    sendResult(R_ERROR);
    return;
  }
  String host, port;
  int portIndex;
  // Dialing a stored number
  if (upCmd.indexOf("ATDS") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    portIndex = speedDials[speedNum].indexOf(':');
    if (portIndex != -1) {
      host = speedDials[speedNum].substring(0, portIndex);
      port = speedDials[speedNum].substring(portIndex + 1);
    } else {
      host = speedDials[speedNum];
      port = "23";
    }
  } else {
    // Dialing an ad-hoc number
    int portIndex = cmd.indexOf(":");
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
      if ((host == "0000000") ||
          (host == "1111111") ||
          (host == "2222222") ||
          (host == "3333333") ||
          (host == "4444444") ||
          (host == "5555555") ||
          (host == "6666666") ||
          (host == "7777777") ||
          (host == "8888888") ||
          (host == "9999999")) {
        byte speedNum = host.substring(0, 1).toInt();
        portIndex = speedDials[speedNum].indexOf(':');
        if (portIndex != -1) {
          host = speedDials[speedNum].substring(0, portIndex);
          port = speedDials[speedNum].substring(portIndex + 1);
        } else {
          host = speedDials[speedNum];
          port = "23";
        }
      }
    }
  }
  host.trim(); // remove leading or trailing spaces
  port.trim();
  SerialPrint("DIALING "); SerialPrint(host); SerialPrint(":"); SerialPrintLn(port);
  char *hostChr = new char[host.length() + 1];
  host.toCharArray(hostChr, host.length() + 1);
  int portInt = port.toInt();
  tcpClient.setNoDelay(true); // Try to disable naggle
  if (tcpClient.connect(hostChr, portInt))
  {
    tcpClient.setNoDelay(true); // Try to disable naggle
    sendResult(R_CONNECT);
    connectTime = millis();
    cmdMode = false;
    SerialFlush();
    callConnected = true;
    setCarrier(callConnected);
    //if (tcpServerPort > 0) tcpServer.stop();
  }
  else
  {
    sendResult(R_NOANSWER);
    callConnected = false;
    setCarrier(callConnected);
  }
  delete hostChr;
}

/**
   Perform a command given in command mode
*/
void command()
{
  cmd.trim();
  if (cmd == "") return;
  SerialPrintLn();
  String upCmd = cmd;
  upCmd.toUpperCase();

  /**** Just AT ****/
  if (upCmd == "AT") sendResult(R_OK_STAT);

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0) || (upCmd.indexOf("ATDS") == 0))
  {
    dialOut(upCmd);
  }

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    sendResult(R_OK_STAT);
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    sendResult(R_OK_STAT);
  }

  else if (upCmd == "ATNET?") {
    SerialPrintLn(String(telnet));
    sendResult(R_OK_STAT);
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient()) {
    answerCall();
  }

  /**** Display Help ****/
  else if (upCmd == "AT?" || upCmd == "ATHELP") {
    displayHelp();
    sendResult(R_OK_STAT);
  }

  /**** Reset, reload settings from EEPROM ****/
  else if (upCmd == "ATZ") {
    readSettings();
    sendResult(R_OK_STAT);
  }

  /**** Disconnect WiFi ****/
  else if (upCmd == "ATC0") {
    disconnectWiFi();
    sendResult(R_OK_STAT);
  }

  /**** Connect WiFi ****/
  else if (upCmd == "ATC1") {
    connectWiFi();
    sendResult(R_OK_STAT);
  }

  /**** Control local echo in command mode ****/
  else if (upCmd.indexOf("ATE") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(echo));
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(3, 4) == "0") {
      echo = 0;
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(3, 4) == "1") {
      echo = 1;
      sendResult(R_OK_STAT);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control verbosity ****/
  else if (upCmd.indexOf("ATV") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(verboseResults));
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(3, 4) == "0") {
      verboseResults = 0;
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(3, 4) == "1") {
      verboseResults = 1;
      sendResult(R_OK_STAT);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control pin polarity of CTS, RTS, DCD ****/
  else if (upCmd.indexOf("AT&P") == 0) {
    if (upCmd.substring(4, 5) == "?") {
      sendString(String(pinPolarity));
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(4, 5) == "0") {
      pinPolarity = P_INVERTED;
      sendResult(R_OK_STAT);
      setCarrier(callConnected);
    }
    else if (upCmd.substring(4, 5) == "1") {
      pinPolarity = P_NORMAL;
      sendResult(R_OK_STAT);
      setCarrier(callConnected);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control Flow Control ****/
  else if (upCmd.indexOf("AT&K") == 0) {
    if (upCmd.substring(4, 5) == "?") {
      sendString(String(flowControl));
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(4, 5) == "0") {
      flowControl = 0;
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(4, 5) == "1") {
      flowControl = 1;
      sendResult(R_OK_STAT);
    }
    else if (upCmd.substring(4, 5) == "2") {
      flowControl = 2;
      sendResult(R_OK_STAT);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Set current baud rate ****/
  else if (upCmd.indexOf("AT$SB=") == 0) {
    setBaudRate(upCmd.substring(6).toInt());
  }

  /**** Display current baud rate ****/
  else if (upCmd.indexOf("AT$SB?") == 0) {
    sendString(String(bauds[serialSpeed]));;
  }

  /**** Set busy message ****/
  else if (upCmd.indexOf("AT$BM=") == 0) {
    busyMsg = cmd.substring(6);
    sendResult(R_OK_STAT);
  }

  /**** Display busy message ****/
  else if (upCmd.indexOf("AT$BM?") == 0) {
    sendString(busyMsg);
    sendResult(R_OK_STAT);
  }

  /**** Display Network settings ****/
  else if (upCmd == "ATI") {
    displayNetworkStatus();
    sendResult(R_OK_STAT);
  }

  /**** Display profile settings ****/
  else if (upCmd == "AT&V") {
    displayCurrentSettings();
    waitForSpace();
    displayStoredSettings();
    sendResult(R_OK_STAT);
  }

  /**** Save (write) current settings to EEPROM ****/
  else if (upCmd == "AT&W") {
    writeSettings();
    sendResult(R_OK_STAT);
  }

  /**** Set or display a speed dial number ****/
  else if (upCmd.indexOf("AT&Z") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    if (speedNum >= 0 && speedNum <= 9) {
      if (upCmd.substring(5, 6) == "=") {
        String speedDial = cmd;
        storeSpeedDial(speedNum, speedDial.substring(6));
        sendResult(R_OK_STAT);
      }
      if (upCmd.substring(5, 6) == "?") {
        sendString(speedDials[speedNum]);
        sendResult(R_OK_STAT);
      }
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** Set WiFi SSID ****/
  else if (upCmd.indexOf("AT$SSID=") == 0) {
    ssid = cmd.substring(8);
    sendResult(R_OK_STAT);
  }

  /**** Display WiFi SSID ****/
  else if (upCmd == "AT$SSID?") {
    sendString(ssid);
    sendResult(R_OK_STAT);
  }

  /**** Set WiFi Password ****/
  else if (upCmd.indexOf("AT$PASS=") == 0) {
    password = cmd.substring(8);
    sendResult(R_OK_STAT);
  }

  /**** Display WiFi Password ****/
  else if (upCmd == "AT$PASS?") {
    sendString(password);
    sendResult(R_OK_STAT);
  }

  /**** Reset EEPROM and current settings to factory defaults ****/
  else if (upCmd == "AT&F") {
    defaultEEPROM();
    readSettings();
    sendResult(R_OK_STAT);
  }

  /**** Set auto answer off ****/
  else if (upCmd == "ATS0=0") {
    autoAnswer = false;
    sendResult(R_OK_STAT);
  }

  /**** Set auto answer on ****/
  else if (upCmd == "ATS0=1") {
    autoAnswer = true;
    sendResult(R_OK_STAT);
  }

  /**** Display auto answer setting ****/
  else if (upCmd == "ATS0?") {
    sendString(String(autoAnswer));
    sendResult(R_OK_STAT);
  }

  /**** Set PET MCTerm Translate On ****/
  else if (upCmd == "ATPET=1") {
    petTranslate = true;
    sendResult(R_OK_STAT);
  }

  /**** Set PET MCTerm Translate Off ****/
  else if (upCmd == "ATPET=0") {
    petTranslate = false;
    sendResult(R_OK_STAT);
  }

  /**** Display PET MCTerm Translate Setting ****/
  else if (upCmd == "ATPET?") {
    sendString(String(petTranslate));
    sendResult(R_OK_STAT);
  }

  /**** Set HEX Translate On ****/
  else if (upCmd == "ATHEX=1") {
    bHex = true;
    sendResult(R_OK_STAT);
  }

  /**** Set HEX Translate Off ****/
  else if (upCmd == "ATHEX=0") {
    bHex = false;
    sendResult(R_OK_STAT);
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("ATH") == 0) {
    hangUp();
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("AT$RB") == 0) {
    sendResult(R_OK_STAT);
    SerialFlush();
    delay(500);
    ESP.restart(); //ESP.reset();
  }

  /**** Exit modem command mode, go online ****/
  else if (upCmd == "ATO") {
    if (callConnected == 1) {
      sendResult(R_CONNECT);
      cmdMode = false;
    } else {
      sendResult(R_ERROR);
    }
  }

  else if (upCmd == "ATFC") {
    firmwareCheck();
  }
  else if (upCmd == "ATFU") {
    firmwareUpdate();
  }
  else if (upCmd == "ATX") {
    mainMenu(false);
  }

  /**** Set incoming TCP server port ****/
  else if (upCmd.indexOf("AT$SP=") == 0) {
    tcpServerPort = upCmd.substring(6).toInt();
    sendString("CHANGES REQUIRES NV SAVE (AT&W) AND RESTART");
    sendResult(R_OK_STAT);
  }

  /**** Display incoming TCP server port ****/
  else if (upCmd == "AT$SP?") {
    sendString(String(tcpServerPort));
    sendResult(R_OK_STAT);
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP?")
  {
    SerialPrintLn(WiFi.localIP());
    sendResult(R_OK_STAT);
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      sendResult(R_NOCARRIER);
      connectTime = 0;
      callConnected = false;
      setCarrier(callConnected);
    }
    else
    {
      sendResult(R_CONNECT);
      connectTime = millis();
      cmdMode = false;
      callConnected = true;
      setCarrier(callConnected);

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Unknown command ****/
  else sendResult(R_ERROR);

  cmd = "";
}

// RTS/CTS protocol is a method of handshaking which uses one wire in each direction to allow each
// device to indicate to the other whether or not it is ready to receive data at any given moment.
// One device sends on RTS and listens on CTS; the other does the reverse. A device should drive
// its handshake-output wire low when it is ready to receive data, and high when it is not. A device
// that wishes to send data should not start sending any bytes while the handshake-input wire is low;
// if it sees the handshake wire go high, it should finish transmitting the current byte and then wait
// for the handshake wire to go low before transmitting any more.
// http://electronics.stackexchange.com/questions/38022/what-is-rts-and-cts-flow-control
void handleFlowControl() {
  if (flowControl == F_NONE) return;
  if (flowControl == F_HARDWARE) {
    if (digitalRead(CTS_PIN) == pinPolarity) txPaused = true;
    else txPaused = false;
  }
  if (flowControl == F_SOFTWARE) {
   
  }
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

void enterModemMode()
{
    SerialPrintLn("\r\nEntering MODEM Mode...");
    menuMode = MODE_MODEM;
    ///if (tcpServerPort > 0) tcpServer.begin(tcpServerPort);
    bytesSent=0;
    bytesRecv=0;
    sentChanged=false;
    recvChanged=false;
    speedDialShown=false;
    callConnected=false;
   
    modem_timer.every(250, refreshDisplay);
 
    WiFi.mode(WIFI_STA);
    connectWiFi();
    sendResult(R_OK_STAT);
 
    digitalWrite(LED_PIN, LOW); // on
 
    webServer.on("/", handleRoot);
    webServer.on("/ath", handleWebHangUp);
    webServer.begin();
   
    mdns.begin("WiRSa"); // Set the network hostname to "wirsa.local"
}

void mainLoop()
{
    int menuSel=-1;
    readSwitches();
    if (BTNUP) {  //UP
        menuIdx--;
        if (menuIdx<0)
          menuIdx=menuCnt-1;
        mainMenu(true);
    }
    else if (BTNDN) { //DOWN
      menuIdx++;
      if (menuIdx>(menuCnt-1))
        menuIdx=0;
      mainMenu(true);
    }
    else if (BTNEN) { //ENTER
      menuSel = menuIdx;
    }
    else if (BTNBK) { //BACK
    }
    waitSwitches();

    int serAvl = SerialAvailable();
    char chr;
    if (serAvl>0)
      chr = SerialRead();

    if (serAvl>0 || menuSel>-1)
    {
      if (chr=='M'||chr=='m'||menuSel==0) //enter modem mode
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        enterModemMode();
      }
      else if (chr=='F'||chr=='f'||menuSel==1)
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        SerialPrintLn("");
        SerialPrintLn("Initializing SD card...");
        if (!SD.begin(SD_CS)) {
          SerialPrintLn("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        SerialPrintLn("Initialization Complete.");
        fileMenu(false);
      }
      else if (chr=='P'||chr=='p'||menuSel==2)
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        SerialPrintLn("");
        SerialPrintLn("Initializing SD card...");
        if (!SD.begin(SD_CS)) {
          SerialPrintLn("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        SerialPrintLn("Initialization Complete.");        
        playbackMenu(false);
      }
      else if (chr=='S'||chr=='s'||menuSel==3)
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        settingsMenu(false);
      }
      else if (chr=='\r'||chr=='\n'||chr=='?')
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        mainMenu(false);
      }
      else if (serAvl>0)
      { //redisplay the menu
        //mainMenu(false);
      }
    }
}

void baudLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      baudMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    baudMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    settingsMenu(false);
  }
  waitSwitches();

  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }
 
  if (menuSel>-1)
  {
    serialSpeed = menuIdx;
    writeSettings();
    PhysicalSerial.end();
    delay(200);
    PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig]);
    settingsMenu(false);
  } else if (serAvl>0) {
    //between A-I                                or a-i
    if (chr>=65 && chr <= 65+sizeof(bauds) || chr>=97 && chr <= 97+sizeof(bauds))
    {
      if (chr>=97)
        chr -= 32; //convert to uppercase
      serialSpeed = chr-65;
      writeSettings();
      PhysicalSerial.end();
      delay(200);
      PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig]);
      settingsMenu(false);
    } else
      baudMenu(false);
  }
}

void serialLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      serialMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    serialMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    settingsMenu(false);
  }
  waitSwitches();

  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }
 
  if (menuSel>-1)
  {
    serialConfig = menuIdx;
    writeSettings();
    PhysicalSerial.end();
    delay(200);
    PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig]);
    settingsMenu(false);
  } else if (serAvl>0) {
    //between A-X                                or a-x
    if (chr>=65 && chr <= 65+sizeof(bits) || chr>=97 && chr <= 97+sizeof(bits))
    {
      if (chr>=97)
        chr -= 32; //convert to uppercase
      serialConfig = chr-65;
      writeSettings();
      PhysicalSerial.end();
      delay(200);
      PhysicalSerial.begin(bauds[serialSpeed], (SerialConfig)bits[serialConfig]);
      settingsMenu(false);
    } else
      serialMenu(false);
  }
}

void orientationLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      orientationMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    orientationMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    settingsMenu(false);
  }
  waitSwitches();
 
  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {
    if (chr=='N'||chr=='n')
      menuSel=0;
    else if (chr=='F'||chr=='f')
      menuSel=1;
     
    if (menuSel>-1)
    {
      dispOrientation = menuSel;
      writeSettings();
      if (dispOrientation==D_NORMAL)
        display.setRotation(0);
      else
        display.setRotation(2);
      settingsMenu(false);
    }
    else if (serAvl>0)
    {
      orientationMenu(false);
    }
  }
}

void defaultModeLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      defaultModeMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    defaultModeMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    settingsMenu(false);
  }
  waitSwitches();
 
  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {
    if (chr=='A'||chr=='a')
      menuSel=0;
    else if (chr=='B'||chr=='b')
      menuSel=1;
     
    if (menuSel>-1)
    {
      defaultMode = menuSel;
      writeSettings();
      settingsMenu(false);
    }
    else
      defaultModeMenu(false);
  }
}

void settingsLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      settingsMenu(true);
  } else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    settingsMenu(true);
  } else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  } else if (BTNBK) { //BACK
    mainMenu(false);
  }
  waitSwitches();
   
  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {
    if (chr=='M'||chr=='m'||menuSel==0) //main menu
    {
      mainMenu(false);
    }
    else if (chr=='B'||chr=='b'||menuSel==1)
    {
      baudMenu(false);
    }
    else if (chr=='S'||chr=='s'||menuSel==2)
    {
      serialMenu(false);
    }
    else if (chr=='O'||chr=='o'||menuSel==3)
    {
      orientationMenu(false);
    }
    else if (chr=='D'||chr=='d'||menuSel==4)
    {
      defaultModeMenu(false);
    }
    else if (chr=='F'||chr=='f'||menuSel==5)
    {
       SerialPrintLn("** PLEASE WAIT: FACTORY RESET **");
       showMessage("***************\n* PLEASE WAIT *\n*FACTORY RESET*\n***************");
       defaultEEPROM();
       ESP.restart();
    }
    else if (chr=='R'||chr=='r'||menuSel==6)
    {
      SerialPrintLn("** PLEASE WAIT: REBOOTING **");
      showMessage("***************\n* PLEASE WAIT *\n*  REBOOTING  *\n***************");
      ESP.restart();
    }    
    else if (chr=='A'||chr=='a'||menuSel==7)
    {
      SerialPrintLn("\n** WiRSa BUILD:   " + build + " **");
      showMessage("***************\n* WiRSa BUILD *\n*    " + build + "    *\n***************");
    }
    else if (serAvl>0)
    {
      settingsMenu(false);
    }
  }
}

void playbackMenu(bool arrow) {
  menuMode = MODE_PLAYBACK;
  showMenu("PLAYBACK", playbackMenuDisp, 6, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void settingsMenu(bool arrow) {
  menuMode = MODE_SETTINGS;
  showMenu("SETTINGS", settingsMenuDisp, 8, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void orientationMenu(bool arrow) {
  menuMode = MODE_ORIENTATION;
  showMenu("DISPLAY", orientationMenuDisp, 2, (arrow?MENU_DISP:MENU_BOTH), dispOrientation);
}

void defaultModeMenu(bool arrow) {
  menuMode = MODE_DEFAULTMODE;
  showMenu("DEFAULT", defaultModeDisp, 2, (arrow?MENU_DISP:MENU_NUM), defaultMode);
}

void protocolMenu(bool arrow) {
  menuMode = MODE_PROTOCOL;
  showMenu("PROTOCOL", protocolMenuDisp, 6, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void baudMenu(bool arrow) {
  menuMode = MODE_SETBAUD;
  showMenu("BAUD RATE", baudDisp, 9, (arrow?MENU_DISP:MENU_NUM), serialSpeed);
}

void serialMenu(bool arrow) {
  menuMode = MODE_SERIALCONFIG;
  showMenu("SERIAL", bitsDisp, 24, (arrow?MENU_DISP:MENU_NUM), serialConfig);
}

void fileMenu(bool arrow) {
  menuMode = MODE_FILE;
  showMenu("FILE XFER", fileMenuDisp, 4, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void fileLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      fileMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    fileMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    mainMenu(false);
  }
  waitSwitches();

  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {    
    if (chr=='M'||chr=='m'||menuSel==0) //main menu
    {
      mainMenu(false);
    }
    else if (chr=='L'||chr=='l') //list files
    {
      listFiles();
    }
    else if (menuSel==1)
    {
      listFilesMenu(false);
    }
    else if (chr=='S'||chr=='s'||menuSel==2) //send file (from SD)
    {
      xferMode = XFER_SEND;
      protocolMenu(false);
    }
    else if (chr=='R'||chr=='r'||menuSel==3) //receive file (to SD)
    {
      xferMode = XFER_RECV;
      protocolMenu(false);
    }
    else if (chr=='G'||chr=='g') //print out the logfile
    {
      SerialPrintLn("\r\n\r\nTransfer Log:");
      logFile = SD.open("logfile.txt");
      if (logFile) {
        while (logFile.available()) {
          SerialWrite(logFile.read());
        }
        logFile.close();
      } else
        SerialPrintLn("Unable to open log file");
    }
    /*else if (chr=='S'||chr=='s') //send pipe mode
    {
      sendPipeMode();
    }
    else if (chr=='R'||chr=='r') //receive pipe mode
    {
      receivePipeMode();
    }*/  
    else {
      fileMenu(false);
    }
  }
}

void protocolLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      protocolMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    protocolMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    fileMenu(false);
  }
  waitSwitches();

  int serAvl = SerialAvailable();
  char chr;
  if (serAvl>0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {    
    if (chr=='B'||chr=='b'||menuSel==0) //main menu
    {
      fileMenu(false);
    }
    else if (chr=='R'||chr=='r'||menuSel==1) //Raw Mode
    {
      if (xferMode == XFER_SEND)
        sendFileRaw();
      else if (xferMode == XFER_RECV)
        receiveFileRaw();
    }
    else if (chr=='X'||chr=='x'||menuSel==2) //XMODEM
    {
      if (xferMode == XFER_SEND)
        sendFileXMODEM();
      else if (xferMode == XFER_RECV)
        receiveFileXMODEM();
    }
    else if (chr=='Y'||chr=='y'||menuSel==3) //YMODEM
    {
      if (xferMode == XFER_SEND)
        sendFileYMODEM();
      else if (xferMode == XFER_RECV)
        receiveFileYMODEM();
    }
    else if (chr=='Z'||chr=='z'||menuSel==4) //ZMODEM
    {
      showMessage("NOT YET\nIMPLEMENTED");
      SerialPrintLn("NOT YET IMPLEMENTED");
      /*if (xferMode == XFER_SEND)
        sendFileZMODEM();
      else if (xferMode == XFER_RECV)
        receiveFileZMODEM();*/
    }
    else if (chr=='K'||chr=='k'||menuSel==5) //KERMIT
    {
      showMessage("NOT YET\nIMPLEMENTED");
      SerialPrintLn("NOT YET IMPLEMENTED");
      /*if (xferMode == XFER_SEND)
        sendFileKERMIT();
      else if (xferMode == XFER_RECV)
        receiveFileKERMIT();*/
    }
    else {
      protocolMenu(false);
    }
  }
}

bool checkCancel() {
  readBackSwitch();
  bool cncl = BTNBK;
  waitBackSwitch();
  return cncl;
}

void listFilesLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      listFilesMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    listFilesMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    fileMenu(false);
  }
  waitSwitches();
 
  if (SerialAvailable() || menuSel>=-1)
  {
    char chr = SerialRead();
    SerialPrint(chr);

    if (menuSel>-1){
      fileName = files[menuSel];
      fileMenu(false);
      SerialPrintLn("Chosen file: " + fileName);
    } else {
      //listFilesMenu();
    }
  }
}


void resetGlobals()
{
  packetNumber = 0;
  lastPacket = false;
  eotSent = 0;
  readyToReceive = false;
  zeroPacket = true;
  blockSize = 1024;
  packetSize = 1029;
  finalFileSent = false;
  fileIndex = 0;
  xferBytes = 0;
}

void SerialWriteLog(char c, String log)
{
  if (log!="")
    addLog("> [" + log + "] 0x" + String(c, HEX) + " " + String(c) + "\r\n");
  updateXferMessage();
  SerialWrite(c);
}

void addLog(String logmsg)
{
  //logFile = SD.open("logfile.txt", FILE_WRITE);
  //logFile.println(logmsg);
  //logFile.flush();
  //logFile.close();
}

String prompt(String msg, String dflt="")
{
  SerialPrintLn("");
  SerialPrintLn("");
  SerialPrint(msg);
  if (dflt!="")
    SerialPrint("[" + dflt + "] ");
  showMessage("{SERIAL}\n"+msg);
  String resp = getLine();
  if (resp=="")
    resp = dflt;
  clearInputBuffer();
  return resp;
}

void sendFileXMODEM()
{
  resetGlobals();
  packetNumber = 1;
  fileName=prompt("Please enter the filename to send: ");
  SerialPrintLn("Sending " + fileName);
  SerialPrintLn("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //SerialPrintLn("Couldn't open log file");
  File dataFile = SD.open(fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING XMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopXMODEM(dataFile, fileName, fileSize);
    SerialPrintLn("Download Complete");
    dataFile.close();
  } else {
    SerialPrintLn("Invalid File");
  }
}

void sendFileYMODEM()
{
  resetGlobals();
  fileName=prompt("Please enter the filename to send: ");
  SerialPrintLn("Sending " + fileName);
  SerialPrintLn("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //SerialPrintLn("Couldn't open log file");
  File dataFile = SD.open(fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING YMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopYMODEM(dataFile, fileName, fileSize);
    SerialPrintLn("Download Complete");
    dataFile.close();
  } else {
    SerialPrintLn("Invalid File");
  }
}

void sendFileRaw()
{
  digitalWrite(LED_PIN, HIGH);
  resetGlobals();
  fileName=prompt("Please enter the filename: ");
  File dataFile = SD.open(fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING RAW\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    waitTime = prompt("Start time in seconds: ", "30");  
    SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
    digitalWrite(LED_PIN, LOW);
    delay(waitTime.toInt()*1000);
    digitalWrite(LED_PIN, HIGH);
    int c=0;
    for (int i=0; i<fileSize.toInt(); i++)
    {
      if (dataFile.available())
      {
        SerialWrite(dataFile.read());
        if (c==512)
        { //flush the buffer every so often to avoid a WDT reset
          SerialFlush();
          led_on();
          yield();
          c=0;
        }
        else
          c++;
      }
    }
    SerialFlush();
    SerialWrite((char)26); //DOS EOF
    SerialFlush();
    yield();
    digitalWrite(LED_PIN, LOW);
    dataFile.close();
  } else {
    SerialPrintLn("\r\nInvalid File");
  }
}

void receiveFileRaw()
{
  fileName=prompt("Please enter the filename: ");
  xferFile = SD.open(fileName, FILE_WRITE);
  if (xferFile) {
    xferMsg = "RECEIVE RAW\nBytes Recevied:\n";
    while (true)
    {
      if (SerialAvailable() > 0) {
        char c = SerialRead();
        if (c==27)
          break;
        else
        {
          updateXferMessage();
          xferFile.write(c);
        }
      }
    }
    xferFile.flush();
    xferFile.close();
  } else
    SerialPrintLn("Transfer Cancelled");
}

void sendLoopXMODEM(File dataFile, String fileName, String fileSize)
{
  bool forceStart=false;
 
  while(true)
  {
    //readSwitches();
    //if (SW3 && packetNumber==1) {
    //  waitSwitches();
    //  forceStart=true;
    //}
    if (SerialAvailable() > 0 || forceStart) {
      char c = SerialRead();
      if (forceStart)
        c=0x15; //NAK will start the transfer
       
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          if (lastPacket)
          {
            SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
            eotSent++;
            if (eotSent==2)
              return;
          }
          else
          {
            packetNumber++;
            addLog("packetNumber: " + String(packetNumber));
            addLog("sending packet after ACK");
            sendPacket(dataFile);
          }
          break;
        case 0x15: //NAK
          blockSize = 128;
        case 0x43: //'C'
          //SerialPrintLn("NAK");
          if (lastPacket)
          {
              SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //SerialPrintLn("EOT");
              eotSent++;
              if (eotSent==2)
                return;
          }
          else
          {
            addLog("Sending Packet after C");
            sendPacket(dataFile);
          }
          break;
      }
    }
  }
}

void sendLoopYMODEM(File dataFile, String fileName, String fileSize)
{
  while(true)
  {
    if (SerialAvailable() > 0) {
      char c = SerialRead();
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          //SerialPrintLn("ACK");
          if (lastPacket && eotSent<2)
          {
            SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
            //SerialPrintLn("EOT");
            eotSent++;
          }
          else if (lastPacket && (eotSent==2) && finalFileSent)
          {
            return;
          }
          else
          {
            packetNumber++;
            addLog("packetNumber: " + String(packetNumber));
            if (zeroPacket == true)
              zeroPacket = false; //wait for 'C' then send first actual packet
            else
            {
              addLog("sending packet after ACK");
              sendPacket(dataFile);
            }
          }
          break;
        case 0x15: //NAK
        case 0x43: //'C'
          //SerialPrintLn("NAK");
          finalFileSent = false;
          if (lastPacket && eotSent<2)
          {
              SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //SerialPrintLn("EOT");
              eotSent++;
          }
          else if (lastPacket && (eotSent == 2))
          {
            addLog("Sending Final Packet");
            sendFinalPacket();
            finalFileSent = true;
          }
          else if (packetNumber==0)
          {
            addLog("Sending Zero Packet");
            sendZeroPacket(fileName, fileSize);
          }
          else
          {
            addLog("Sending Packet after C");
            sendPacket(dataFile);
          }
          break;
      }
    }
  }
}

String getFileSize(String fileName)
{
   File root = SD.open("/");
   while(true) {
     File entry =  root.openNextFile();
     if (!entry) {
       // no more files
       break;
     }
     if (!entry.isDirectory()) {
         String fn = entry.name();
         if (fn==fileName)
         {
           String sz = String(entry.size());
           entry.close();
           root.close();
           return sz;
         }
     }
     entry.close();
   }
  root.close();
  return "0";
}

void sendZeroPacket(String fileName, String fileSize)
{
  LinkedList<byte> packetData = LinkedList<byte>();
  for(int i = 0; i<fileName.length(); i++)
  {
    packetData.add(fileName[i]); //add the filename
  }
  packetData.add(0x00); //add NUL as delimiter
  //add the file size as an ascii-converted hex number
  for (int i = 0; i<fileSize.length(); i++)
  {
      packetData.add(fileSize[i]);
  }
  packetData.add(0x00);
  while (packetData.size() < 128)
  {
      packetData.add(0x00);
  }
  //uint16_t crc = ComputeCRC(packetData);
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < packetData.size(); i++)
  {
    crc.add(packetData.get(i));
  }
  uint16_t c = crc.getCRC();
  uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
  uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left
  LinkedList<byte> packet = LinkedList<byte>();
  packet.add(0x01); //SOH
  packet.add(packetNumber);
  packet.add(0xFF); //inverse packet number
  for (int i = 0; i<packetData.size(); i++)
  {
      packet.add(packetData.get(i));
  }
  packet.add(hi);
  packet.add(lo);
  //Console.println("Sending packet " + packetNumber.ToString() + " " + packet.ToArray().Length.ToString());
  //convert it back to a byte array
  byte p[packet.size()];
  for (int i = 0; i<packet.size(); i++)
  {
      p[i]=packet.get(i);
  }

  sendTelnet(p, sizeof(p));
}

void sendFinalPacket()
{
  LinkedList<byte> packetData = LinkedList<byte>();
  while (packetData.size() < 128)
  {
      packetData.add(0x00);
  }
  //uint16_t crc = ComputeCRC(packetData);
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < packetData.size(); i++)
  {
    crc.add(packetData.get(i));
  }
  uint16_t c = crc.getCRC();
  uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
  uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left
  LinkedList<byte> packet = LinkedList<byte>();
  packet.add(0x01); //SOH
  packet.add(0x00);
  packet.add(0xFF); //inverse packet number
  for (int i = 0; i<packetData.size(); i++)
  {
    packet.add(packetData.get(i));
  }
  packet.add(hi);
  packet.add(lo);
  //Console.println("Sending packet " + packetNumber.ToString() + " " + packet.ToArray().Length.ToString());
  //convert it back to a byte array
  byte p[packet.size()];
  for (int i = 0; i<packet.size(); i++)
  {
    p[i]=packet.get(i);
  }

  sendTelnet(p, sizeof(p));
}

void sendPacket(File dataFile)
{
  byte packetInverse = (byte)(0xFF - (int)packetNumber);
  LinkedList<byte> packetData = LinkedList<byte>();
  addLog("Sending Packet: " + String(packetNumber) + " - Block Size: " + blockSize);
  for (int i=0; i<blockSize; i++)
  {
    if (dataFile.available())
    {
      packetData.add(dataFile.read());
    }
  }
  if (packetData.size() == 0)
  {
    addLog("setting Last Packet");
    lastPacket = true;
  }
  else
  {
    while (packetData.size() < blockSize)
    {
      //addLog("filling");
      packetData.add(0x1A); //Fill packet
      lastPacket = true;
    }
    if (lastPacket)
      addLog("Last Packet Set");
     
    crc.reset();
    crc.setPolynome(0x1021);
    for (int i = 0; i < packetData.size(); i++)
    {
      crc.add(packetData.get(i));
    }
    uint16_t c = crc.getCRC();
    uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
    uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left

    LinkedList<byte> packet = LinkedList<byte>();
    packet.add(0x02); //STX
    packet.add(packetNumber);
    packet.add(packetInverse);
    for (int i = 0; i<packetData.size(); i++)
    {
      packet.add(packetData.get(i));
    }
    packet.add(hi);
    packet.add(lo);
    addLog("Packet Size: " + String(packet.size()));
    byte p[packet.size()];
    for (int i = 0; i<packet.size(); i++)
    {
      p[i]=packet.get(i);
    }
 
    sendTelnet(p, sizeof(p));
  }
}

/*ushort ComputeCRC(LinkedList<byte> bytes)
{
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < bytes.size(); i++)
  {
    crc.add(bytes.get(i));
  }
  return crc.getCRC();
}*/

void sendTelnet(byte packet[], int sz)
{  
  String logmsg = "";
  for (int i=0; i<sz; i++)
  {
    //logmsg = "P#" + String(packetNumber) + " b#" + String(i);
   
    if (packet[i] == 0xFF && EscapeTelnet)
      SerialWriteLog(0xFF, "escaping FF");
     
    SerialWriteLog(packet[i], logmsg);
  }
  SerialFlush();
}

void receiveFileXMODEM()
{
  SD.remove("logfile.txt");
  SerialPrintLn("");
  SerialPrintLn("");
  fileName=prompt("Please enter the filename to receive: ");

  SD.remove(fileName);
  xferFile = SD.open(fileName, FILE_WRITE);
  if (xferFile) {
    xferMsg = "RECEIVE XMODEM\nBytes Recevied:\n";
    waitTime = prompt("Start time in seconds: ", "30");  
    SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
    receiveLoopXMODEM();
   
    xferFile.flush();
    xferFile.close();
    clearInputBuffer();
    SerialPrintLn("Download Complete");
  } else
    SerialPrintLn("Transfer Cancelled");
}

void receiveFileYMODEM()
{
  SD.remove("logfile.txt");
  xferMsg = "RECEIVE YMODEM\nBytes Recevied:\n";
  SerialPrintLn("");
  SerialPrintLn("");
  waitTime = prompt("Start time in seconds: ", "30");  
  SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
  receiveLoopYMODEM();
  xferFile.flush();
  xferFile.close();
  clearInputBuffer();
  SerialPrintLn("Download Complete");
}

void clearInputBuffer()
{
  while(SerialAvailable()>0)
  {
    char discard = SerialRead();
  }
}

bool sendStartByte(void *)
{
  //if (packetNumber>0)
  if (packetNumber>1)
    return false;
  else
  {
    SerialWriteLog(0x43, "Sending Start C from Timer"); //letter C
    readyToReceive = true;
    return true;
  }
}

void receiveLoopXMODEM()
{
  LinkedList<char> buffer = LinkedList<char>();  
  resetGlobals();
  packetNumber=1;

  timer.every(waitTime.toInt()*1000, sendStartByte);

  while (true)
  {    
    timer.tick();

    if (SerialAvailable() > 0) {
      char c = SerialRead();

      //addLog("RCV: " + String(c, HEX) + " " + String(c));

      if (!readyToReceive)
      {
        //we got something so maybe client is ready to send? clear the incoming buffer,
        //wait and send a C to indicate I'm ready to receive
        //timer.Enabled = false;
        timer.cancel();
        delay(100); //wait for anything else incoming
        clearInputBuffer();
        //delay(2000); //wait a bit
        SerialWriteLog(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          SerialWriteLog(0x06, "ACK after EOT"); //ACK              
          return;
        }
        else
        {
          updateXferMessage();
          buffer.add(c);

          //if we've hit the expected packet size
          bool processPacket = false;
          if (buffer.size() == packetSize && (buffer.get(0) == 0x02))
          {
            processPacket = true;
            blockSize = 1024;
            packetSize = 1029;
          }
          else if (buffer.size() == 133 && buffer.get(0) == 0x01)
          {
            //even though we're in 1K mode, I still have to process 128-byte packets
            addLog("buffer.get(0): " + String(buffer.get(0)));
            addLog("buffer.get(1): " + String(buffer.get(1)));
            processPacket = true;
            blockSize = 128;
            packetSize = 133;
          }
   
          if (processPacket)
          {
            addLog(String(buffer.size()) + " Process now");
            if (processPacket)
            {
              bool goodPacket = false;
              //verify the packet number matches what's expected
              if (buffer.get(1) == packetNumber)
              {
                //check the inverse for good measure
                if (buffer.get(2) == 255 - packetNumber)
                {
                  //get the CRC for the packet's data
                  crc.reset();
                  crc.setPolynome(0x1021);
                  for (int i = 3; i < packetSize-2; i++)
                  {
                    crc.add(buffer.get(i));
                  }
                  uint16_t c = crc.getCRC();
                  uint8_t hi = (char)(c >> 8); // Shift 8 bytes -> tell get dropped
                  uint8_t lo = (char)(c & 0xFF); // Only last Byte is left
     
                  //compare it to the received CRC - upper byte first
                  addLog("hi CRC: " + String(hi) + " " + buffer.get(packetSize - 2));
                  if (hi == buffer.get(packetSize - 2))
                  {
                    addLog("lo CRC: " + String(lo) + " " + buffer.get(packetSize - 1));
                    //and then compare the lower byte
                    if (lo == buffer.get(packetSize - 1))
                    {
                      goodPacket = true; //mark as a good packet so we send ACK below
                      //since it's good, add this packet to the file
                      for (int i = 3; i < packetSize - 2; i++)
                      {
                          xferFile.write(buffer.get(i));
                      }
                      xferFile.flush();
                      buffer.clear(); //clear the input buffer to get ready for next pack
                    }
                    else
                      addLog("Bad CRC (lower byte)");
                  }
                  else
                    addLog("Bad CRC (upper byte)");
                }
                else
                  addLog("Wrong inverse packet number");
              }
              else {
                addLog("Wrong packet number, expected ");
                addLog(String(packetNumber, HEX));
                addLog(" got ");
                addLog(String(buffer.get(1), HEX));
              }
              if (goodPacket)
              {
                addLog("\nGood packet, sending ACK");
                packetNumber++; //expect the next packet
                SerialWriteLog(0x06, "ACK after good packet"); //ACK
              }              
            }
          }
        }
      }
    }
  }
}

void receiveLoopYMODEM()
{
  LinkedList<char> buffer = LinkedList<char>();  
  bool lastByteIAC = false;
  resetGlobals();
  fileName="";
  String fileSize="";
  unsigned int fileSizeNum=0;
  unsigned int bytesWritten=0;

  timer.every(waitTime.toInt()*1000, sendStartByte);

  int XCount=0;

  while (true)
  {    
    timer.tick();

    if (SerialAvailable() > 0) {
      char c = SerialRead();

      //addLog("received: " + String(c, HEX) + " " + String(c) + " " + String(buffer.size()));
      //addLog("received: " + String(c, HEX) + " " + String(buffer.size()));
      addLog("received: " + String(c));
     
      if (c=='X' && XCount>=4)
        return;
      else if (c=='X')
        XCount++;
      else
        XCount=0;      

      if (!readyToReceive)
      {
        //we got something so maybe client is ready to send? clear the incoming buffer,
        //wait and send a C to indicate I'm ready to receive
        //timer.Enabled = false;
        timer.cancel();
        delay(100); //wait for anything else incoming
        clearInputBuffer();
        //delay(2000); //wait a bit
        SerialWriteLog(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        updateXferMessage();
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          SerialWriteLog(0x06, "ACK after EOT"); //ACK              
          return;
        }
        else
        {
          buffer.add(c);

          //if we've hit the expected packet size
          bool processPacket = false;
          if (buffer.size() == packetSize && (buffer.get(0) == 0x02))
          {
            processPacket = true;
            blockSize = 1024;
            packetSize = 1029;
          }
          else if (buffer.size() == 133 && buffer.get(0) == 0x01)
          {
            //even though we're in 1K mode, I still have to process 128-byte packets
           
            addLog("buffer.get(0): " + String(buffer.get(0)));
            addLog("buffer.get(1): " + String(buffer.get(1)));
            processPacket = true;
            blockSize = 128;
            packetSize = 133;
          }
   
          if (processPacket)
          {
            addLog(String(buffer.size()) + " Process now");
            if (processPacket)
            {
              bool goodPacket = false;
              //verify the packet number matches what's expected
              if (buffer.get(1) == packetNumber)
              {
                //check the inverse for good measure
                if (buffer.get(2) == 255 - packetNumber)
                {
                  //get the CRC for the packet's data
                  crc.reset();
                  crc.setPolynome(0x1021);
                  for (int i = 3; i < packetSize-2; i++)
                  {
                    crc.add(buffer.get(i));
                  }
                  uint16_t c = crc.getCRC();
                  uint8_t hi = (char)(c >> 8); // Shift 8 bytes -> tell get dropped
                  uint8_t lo = (char)(c & 0xFF); // Only last Byte is left
     
                  //compare it to the received CRC - upper byte first
                  addLog("hi CRC: " + String(hi) + " " + buffer.get(packetSize - 2));
                  if (hi == buffer.get(packetSize - 2))
                  {
                    addLog("lo CRC: " + String(lo) + " " + buffer.get(packetSize - 1));
                    //and then compare the lower byte
                    if (lo == buffer.get(packetSize - 1))
                    {
                      goodPacket = true; //mark as a good packet so we send ACK below
                      if (zeroPacket)
                      {
                        //parse the zero packet for the filename and size
                        int zeroCount=0;
                        for (int i = 3; i < packetSize - 2; i++)
                        {
                          if (buffer.get(i)==0x0 || (zeroCount>0 && buffer.get(i)==0x20))
                          {
                            zeroCount++;
                            if (zeroCount==2)
                              break;
                          } else {
                            if (zeroCount==0)
                              fileName += char(buffer.get(i));
                            else
                              fileSize += char(buffer.get(i));
                          }
                        }
                        SD.remove(fileName);
                        fileSizeNum = fileSize.toInt();
                        xferFile = SD.open(fileName, FILE_WRITE);
                        addLog("receiving file " + fileName + " of size " + String(fileSizeNum));
                      }
                      else
                      {
                        //since it's good, add this packet to the file
                        for (int i = 3; i < packetSize - 2; i++)
                        {
                          if (bytesWritten < fileSizeNum)
                          {
                            xferFile.write(buffer.get(i));
                            bytesWritten++;
                          }
                        }
                        xferFile.flush();
                      }
                      buffer.clear(); //clear the input buffer to get ready for next pack
                    }
                    else
                      addLog("Bad CRC (lower byte)");
                  }
                  else
                    addLog("Bad CRC (upper byte)");
                }
                else
                  addLog("Wrong inverse packet number");
              }
              else
                addLog("Wrong packet number");

              if (goodPacket)
              {
                addLog("\nGood packet, sending ACK");
                packetNumber++; //expect the next packet
                SerialWriteLog(0x06, "ACK after good packet"); //ACK
                if (zeroPacket)
                {
                  SerialWriteLog(0x43, "send C after first ACK"); //'C'  //also send a "C" after the first ACK so actual transfer begins
                  zeroPacket = false;
                  //expect the data packets should come as 1K
                  blockSize = 1024;
                  packetSize = 1029;
                }
              }              
            }
          }
        }
      }
    }
  }
}

void playbackLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      playbackMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    playbackMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    mainMenu(false);
  }
  waitSwitches();

  if (SerialAvailable() || menuSel>-1)
  {
    char chr = SerialRead();
    SerialPrintLn(chr);
   
    if (chr == 'L' || chr == 'l') {
      listFiles();
    } else if (chr == 'T' || chr == 't') {
      changeTerminalMode();
    } else if (chr == 'E' || chr == 'e') {
      evalKey();
    } else if (chr == 'D' || chr == 'd') {
      SerialPrint("Enter Filename: ");
      String fileName = getLine();
      if (fileName!="") {
        if (displayFile(fileName, false, 0))
          waitKey(27,96);
        SerialPrintLn("");
        playbackMenu(false);
      }
    } else if (chr == 'P' || chr == 'p') {
      SerialPrint("Enter Filename: ");
      fileName = getLine();
      if (fileName!="") {
        SerialPrintLn("");
        SerialPrint("Begin at Position: ");
        String pos = getLine();
        if (displayFile(fileName, true, pos.toInt()))
          waitKey(27,96);
        SerialPrintLn("");
        playbackMenu(false);
      }
    } else if (chr=='M'||chr=='m'||menuSel==0) {
      mainMenu(false);
    } else {
      playbackMenu(false);
    }
  }
}


void listFiles()
{
   SerialPrintLn("");
   SerialPrintLn("listing files on SD card..");
   SerialPrintLn("");
   File root = SD.open("/");
   SerialPrintLn("FILENAME            SIZE");
   SerialPrintLn("--------            ----");
   while(true) {
     File entry =  root.openNextFile();
     if (! entry) {
       // no more files
       break;
     }
     if (!entry.isDirectory()) {
         String fn = entry.name();
         SerialPrint(fn);
         for(int i=0; i<20-fn.length(); i++)
         {
            SerialPrint(" ");
         }
         SerialPrintLn(entry.size(), DEC);
     }
     entry.close();
   }
   root.close();
   SerialPrintLn("");
   SerialPrint("> ");
}

void listFilesMenu(bool arrow) {
  menuMode = MODE_LISTFILE;
  int c=0;
  File root = SD.open("/");
  while(true) {
    File entry =  root.openNextFile();
    if (! entry) {
     // no more files
      break;
    }
    if (!entry.isDirectory()) {
      if (c<100)
      {
        String fn = entry.name();
        files[c]=fn;
        c++;
      }
      //SerialPrintLn(entry.size(), DEC);
    }
    entry.close();
  }
  root.close();

  showMenu("FILE LIST", files, c, (arrow?MENU_DISP:MENU_NUM), 0);
}

void modemMenu() {
  modem_timer.cancel();
  menuMode = MODE_MODEM;
  speedDialShown=true;
  showMenu("DIAL LIST", speedDials, 10, MENU_DISP, 0);
}

void modemMenuMsg() {
  menuMode = MODE_MODEM;
  display.clearDisplay();
  display.setCursor(1, 1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Modem mode\nUp/Down show menu");
  display.display();
  dispCharCnt=21*2;
  dispLineCnt=0;
}

void modemConnected() {  
  showMenu("MODEM", {}, 0, MENU_DISP, 0);
  showWifiIcon();
  showCallIcon();
  display.setTextWrap(false);

  display.setCursor(3, 19);
  display.print("WIFI: ");
  display.print(WiFi.SSID());

  display.setCursor(3, 29);
  //display.print("IP: ");
  display.print(WiFi.localIP());

  if (tcpServerPort > 0) {
    display.print(":");
    display.print(tcpServerPort);
  }

  display.drawLine(0,40,127,40,SSD1306_WHITE);
   
  display.setCursor(3, 43);
  display.print("SENT: ");
  display.print(String(bytesSent));
  display.print(" B");

  display.setCursor(3, 53);
  display.print("RECV: ");
  display.print(String(bytesRecv));
  display.print(" B");

  display.display();
  display.setTextWrap(true);
}

void firmwareCheck() {
  //http://update.retrodisks.com/wirsa-v3.php
  //This works by checking the latest release tag on the hosted github page
  //I have to call the github API page at https://api.github.com/repos/nullvalue0/WiRSa/releases/latest
  //However the problem is this only works over SSL and returns a large result as JSON. Hitting SSL
  //pages from the ESP8266 is a lot of work, so to get around it a built a very simple PHP script that
  //I host on a plain HTTP site. It hits the github API endpoint over HTTPS, parses the JSON and just
  //returns the latest version string over HTTP as plain text - this way the ESP8266 can easily check
  //the latest version.

  SerialPrintLn("\nChecking firmware version..."); yield();
  WiFiClient client;

  HTTPClient http;
  if (http.begin(client, "http://update.retrodisks.com/wirsa-v3.php")) {
      int httpCode = http.GET();
      if (httpCode == 200) {
        http.getString();
        String version = http.getString();
        SerialPrintLn("Latest Version: " + version + ", Device Version: " + build);
        if (build!=version)
          SerialPrintLn("WiRSa firmware update available, download the latest release at https://github.com/nullvalue0/WiRSa or use commmand ATFU to apply updates now.");
        else
          SerialPrintLn("Your WiRSa is running the latest firmware version.");
      }
      else
        SerialPrintLn("Firmware version check failed.");
  }
  
}

// void firmwareUpdate() {

//   WiFiClient client;

//   ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
//   ESPhttpUpdate.onStart(firmwareUpdateStarted);
//   ESPhttpUpdate.onEnd(firmwareUpdateFinished);
//   ESPhttpUpdate.onProgress(firmwareUpdateProgress);
//   ESPhttpUpdate.onError(firmwareUpdateError);

//   t_httpUpdate_return ret = ESPhttpUpdate.update(client, "http://update.retrodisks.com/wirsa-bin-v3.php");
//   switch (ret) {
//     case HTTP_UPDATE_FAILED:
//       Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
//       break;

//     case HTTP_UPDATE_NO_UPDATES:
//       SerialPrintLn("HTTP_UPDATE_NO_UPDATES");
//       break;

//     case HTTP_UPDATE_OK:
//       SerialPrintLn("HTTP_UPDATE_OK");
//       break;
//   }
// }

void firmwareUpdate() {
  //Similar to the firmware check, I have a proxy php script calls the github, gets the latest release
  //url, downloads the binary and forwards it over plain HTTP so the ESP8266 can download it and use
  //it for OTA updates. These php scripts are in the github repository under the firmware folder.

  SerialPrintLn("Starting OTA update...");

  // Begin HTTP client
  HTTPClient http;
  http.begin("http://update.retrodisks.com/wirsa-bin-v3.php");

  // Start the update process
  if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
    SerialPrintLn("Downloading...");

    // Start the download
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient& stream = http.getStream();
      uint8_t buffer[1024];
      int bytesRead;

      // Write the stream to the Update library in chunks
      while ((bytesRead = stream.readBytes(buffer, sizeof(buffer))) > 0) {
        if (Update.write(buffer, bytesRead) != bytesRead) {
          SerialPrintLn("Error during OTA update. Please try again.");
          Update.end(false); // false parameter indicates a failed update
          return;
        }
      }

      // End the update process
      if (Update.end(true)) {
        SerialPrintLn("OTA update complete. Rebooting...");
        ESP.restart();
      } else {
        SerialPrintLn("Error during OTA update. Please try again.");
        Update.end(false); // false parameter indicates a failed update
      }
    } else {
      SerialPrintLn("Failed to download firmware.");
      Update.end(false); // false parameter indicates a failed update
    }
  } else {
    SerialPrintLn("Failed to start OTA update.");
  }

  // End HTTP client
  http.end();
}

void firmwareUpdateStarted() {
  SerialPrintLn("HTTP update process started");
}

void firmwareUpdateFinished() {
  SerialPrintLn("HTTP update process finished...\r\n\r\nPLEASE WAIT, APPLYING UPDATE - DEVICE WILL REBOOT ON IT'S OWN WHEN COMPLETE IN ABOUT 10 SECONDS\r\n\r\n");
}

void firmwareUpdateProgress(int cur, int total) {
  Serial.printf("HTTP update process at %d of %d bytes...\r\n", cur, total);
  PhysicalSerial.printf("HTTP update process at %d of %d bytes...\r\n", cur, total);
}

void firmwareUpdateError(int err) {
  Serial.printf("HTTP update fatal error code %\r\n", err);
  PhysicalSerial.printf("HTTP update fatal error code %\r\n", err);
}

bool function_to_call(void *argument /* optional argument given to in/at/every */) {
    return true; // to repeat the action - false to stop
}

void modemLoop()
{
  modem_timer.tick();

  // Check flow control
  handleFlowControl();
   
  // Service the Web server
  webServer.handleClient();

  // Check to see if user is requesting rate change to 300 baud
  checkButton();

  // New unanswered incoming connection on server listen socket
  if (tcpServer.hasClient()) {
    handleIncomingConnection();
  }

  if (cmdMode == true)
    readSwitches();
  else
    readBackSwitch();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      modemMenu();
  } else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    modemMenu();
  } else if (BTNEN) { //ENTER
    dialOut("ATDS" + String(menuIdx));
    modem_timer.every(250, refreshDisplay);
  } else if (BTNBK) { //BACK
    //if in a call, first back push ends call, 2nd exits modem mode
    if (cmdMode==true)
      mainMenu(false);
    else {
      hangUp();
      cmdMode = true;  
      msgFlag=true; //force full menu redraw
      modem_timer.every(250, refreshDisplay);
    }
  }
  if (cmdMode == true)
    waitSwitches();
  else
    waitBackSwitch();
   
  /**** AT command mode ****/
  if (cmdMode == true)
  {
    // In command mode - don't exchange with TCP but gather characters to a string
    if (SerialAvailable())
    {
      char chr = SerialRead();

      if (petTranslate == true)
        // Fix PET MCTerm 1.26C Pet->ASCII encoding to actual ASCII
        if (chr > 127) chr-= 128;
      else
        // Convert uppercase PETSCII to lowercase ASCII (C64) in command mode only
        if ((chr >= 193) && (chr <= 218)) chr-= 96;

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        displayChar('\n', XFER_RECV);
        display.display();
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          SerialWrite(chr);
          displayChar(chr, XFER_RECV);
          display.display();
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          SerialWrite(chr);
          displayChar(chr, XFER_RECV);
        }
        if (bHex) {
          SerialPrint(chr, HEX);
        }
      }
    }
  }
  /**** Connected mode ****/
  else
  {
    // Transmit from terminal to TCP
    size_t len = 0;
    if (SerialAvailable()) {
      led_on();

      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      if (Serial.available()) {
        len = std::min(Serial.available(), max_buf_size);
        len = Serial.readBytes(&txBuf[0], len);
        //if (len > 0) displayChar('[');
        // Enter command mode with "+++" sequence
        for (int i = 0; i < (int)len; i++)
        {
          if (txBuf[i] == '+') plusCount++; else plusCount = 0;
          if (plusCount >= 3)
          {
            plusTime = millis();
          }
          if (txBuf[i] != '+')
          {
            plusCount = 0;
          }
          displayChar(txBuf[i], XFER_SEND);
        }
      }

      // Read from serial, the amount available up to
      // maximum size of the buffer
      if (PhysicalSerial.available()) {
        len = std::min(PhysicalSerial.available(), max_buf_size);
        len = PhysicalSerial.readBytes(&txBuf[0], len);
        //if (len > 0) displayChar('[');
        // Enter command mode with "+++" sequence
        for (int i = 0; i < (int)len; i++)
        {
          if (txBuf[i] == '+') plusCount++; else plusCount = 0;
          if (plusCount >= 3)
          {
            plusTime = millis();
          }
          if (txBuf[i] != '+')
          {
            plusCount = 0;
          }
          displayChar(txBuf[i], XFER_SEND);
        }
      }

      if (len > 0)
      {
        //displayChar(']');
        display.display();
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }
      // Fix PET MCTerm 1.26C Pet->ASCII encoding to actual ASCII
      if (petTranslate == true) {
        for (int i = len - 1; i >= 0; i--) {
          if (txBuf[i] > 127) txBuf[i]-= 128;
        }
      }
      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      yield();
    }

    // Transmit from TCP to terminal
    while (tcpClient.available() && txPaused == false)
    {
      led_on();
      uint8_t rxByte = tcpClient.read();

      // Is a telnet control code starting?
      if ((telnet == true) && (rxByte == 0xff))
      {
#ifdef DEBUG
        SerialPrint("<t>");
#endif
        rxByte = tcpClient.read();
        if (rxByte == 0xff)
        {
          // 2 times 0xff is just an escaped real 0xff
          SerialWrite(0xff); SerialFlush();
        }
        else
        {
          // rxByte has now the first byte of the actual non-escaped control code
#ifdef DEBUG
          SerialPrint(rxByte);
          SerialPrint(",");
#endif
          uint8_t cmdByte1 = rxByte;
          rxByte = tcpClient.read();
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
#ifdef DEBUG
          SerialPrint(rxByte); SerialFlush();
#endif
          // We are asked to do some option, respond we won't
          if (cmdByte1 == DO)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)WONT); tcpClient.write(cmdByte2);
          }
          // Server wants to do any option, allow it
          else if (cmdByte1 == WILL)
          {
            tcpClient.write((uint8_t)255); tcpClient.write((uint8_t)DO); tcpClient.write(cmdByte2);
          }
        }
#ifdef DEBUG
        SerialPrint("</t>");
#endif
      }
      else
      {
        // Non-control codes pass through freely
        SerialWrite(rxByte);
        displayChar(rxByte, XFER_RECV);
        SerialFlush(); yield();
       }
      handleFlowControl();
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes
  if (plusCount >= 3)
  {
    if (millis() - plusTime > 1000)
    {
      //tcpClient.stop();
      cmdMode = true;
      sendResult(R_OK_STAT);
      plusCount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((!tcpClient.connected()) && (cmdMode == false) && callConnected == true)
  {
    cmdMode = true;
    sendResult(R_NOCARRIER);
    connectTime = 0;
    callConnected = false;
    setCarrier(callConnected);
   
    msgFlag=true; //force full menu redraw
    modemConnected();
    //if (tcpServerPort > 0) tcpServer.begin();
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
  if (millis() - ledTime > LED_TIME) digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // toggle LED state
}

bool refreshDisplay(void *)
{
  if (speedDialShown) {
    display.clearDisplay();
    modemConnected();  //refresh the whole modem screen
    display.drawLine(0,0,0,63,SSD1306_WHITE);  //redraw the border
    display.drawLine(127,0,127,63,SSD1306_WHITE);
    display.drawLine(0,63,127,63,SSD1306_WHITE);
    speedDialShown=false;
  }

  if (sentChanged) {
    display.fillRect(39, 43, 88, 8, SSD1306_BLACK);
    display.setCursor(39, 43);
    display.print(String(bytesSent));
    display.print(" B");
  }
  if (recvChanged) {
    display.fillRect(39, 53, 88, 8, SSD1306_BLACK);
    display.setCursor(39, 53);
    display.print(String(bytesRecv));
    display.print(" B");
  }
 
  if (sentChanged||recvChanged) {
    sentChanged=false;
    recvChanged=false;
    display.display();
  }
 
  return true;
}


void displayChar(char chr, int dir) {
  if (WiFi.status() == WL_CONNECTED && callConnected==true) {
    if (dir==XFER_SEND) {
      bytesSent++;
      sentChanged=true;
    }
    if (dir==XFER_RECV) {
      bytesRecv++;
      recvChanged=true;
    }
  }
}

/*
void displayChar(char chr) {
  if (dispCharCnt>=168) {
    //only refresh the display for each new full screen of characters
    display.display();
    dispCharCnt=0;
    dispLineCnt=0;
    display.clearDisplay();
    display.setCursor(0, 0);     // Start at top-left corner
  }
  if (chr==10) {
    dispCharCnt+=21-dispLineCnt;
    dispLineCnt=0;
  } else if (chr==27) {
    //throw out the next 2 characters...this isn't right but i'll improve later
    dispAnsiCnt = 1;
    return;
  } else if (dispAnsiCnt>0 && dispAnsiCnt<=3) {
    dispAnsiCnt++;
    return;
  } else {
    dispAnsiCnt=0;
    dispCharCnt++;
    if (dispLineCnt>=21)
      dispLineCnt=0;
    dispLineCnt++;
  }
  display.write(chr);
}*/

void changeTerminalMode()
{
  if (terminalMode=="VT100")
    terminalMode="Viewpoint";
  else if (terminalMode=="Viewpoint")
    terminalMode="VT100";

  SerialPrintLn("Terminal Mode set to: " + terminalMode);
}

void clearScreen()
{
  if (terminalMode=="VT100")
  {
    SerialPrint((char)27);
    SerialPrint("[2J");  //clear screen
    SerialPrint((char)27);
    SerialPrint("[1;1H"); //cursor first row/col
  }
  else if (terminalMode=="Viewpoint")
  {
    SerialPrint((char)26); //clear screen
    SerialPrint((char)27);
    SerialPrint("="); //cursor first row/col
    SerialPrint((char)1);
    SerialPrint((char)1);
  }
}

bool displayFile(String filename, bool playback_mode, int begin_position)
{
  File myFile = SD.open(filename);
  clearScreen();
  int pos=0;
  if (myFile) {
    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      if (playback_mode && pos>begin_position)
      {
        char c = getKey();
        if (c==27||c==96)
        {
          myFile.close();
          return false;
        }
        else if (c==9)
        {
          myFile.close();
          return displayFile(filename, playback_mode, begin_position);
        }
      }
      char chr = myFile.read();
      if (chr=='^') //VT escape code
        SerialPrint((char)27);
      else if (chr=='`') //Playback escape code
      {
        chr = myFile.read(); //get the next char
        if (chr=='P') //continue playback
          playback_mode=false;
        else if (chr=='W') //wait for any key (pause playback)
          playback_mode=true;
        else if (chr=='D') //delay 1 second
          delay(1000);
        else if (chr=='E') //wait for enter key
        {
          waitKey(10,13);
          playback_mode=false;
        }
      }
      else
        SerialWrite(chr);
      pos++;
    }
    // close the file:
    myFile.close();
    return true;
  } else {
    // if the file didn't open, print an error:
    SerialPrintLn("error opening '" + filename + "'");
    return false;
  }
}

void evalKey()
{
  SerialPrint("Press Key (repeat same key 3 times to exit): ");
  SerialPrintLn("\n");
  SerialPrintLn("DEC\tHEX\tCHR");
  char lastKey;
  int lastCnt=0;
  while(1) {
    char c = getKey();
    SerialPrint(c, DEC);
    SerialPrint("\t");
    SerialPrint(c, HEX);
    SerialPrint("\t");
    SerialPrintLn(c);
    if (c==lastKey) {
      lastCnt++;
      if (lastCnt==2)
        break;
    } else {
      lastCnt=0;
      lastKey=c;
    }
  }
  playbackMenu(false);
}

char getKey()
{
  while (true)
  {
    if (SerialAvailable() > 0)
      return SerialRead();
  }
}

void waitKey(int key1, int key2)
{
  while (true)
  {
    if (SerialAvailable() > 0)
    {
      char c = SerialRead();
      if (c == key1 || c == key2)
        break;
    }
  }
}

String getLine()
{
  String line="";
  char c;
  while (true)
  {
    if (checkCancel())
      return "";
    if (SerialAvailable() > 0)
    {
      c = SerialRead();
      if (c==13)
        return line;
      else if (c==8 || c==127) {
        if (line.length()>0) {
          SerialPrint(c);
          line = line.substring(0, line.length()-1);
        }
      }
      else
      {
        SerialPrint(c);
        line += c;
      }
    }
  }
}
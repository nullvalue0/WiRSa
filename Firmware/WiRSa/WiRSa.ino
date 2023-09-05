/**************************************************************************
   RetroDisks WiRSa v2 - Wifi RS232 Serial Modem Adapter with File Transfer features
   Copyright (C) 2022 Aron Hoekstra <nullvalue@gmail.com>

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
 

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <LinkedList.h>
#include <SPI.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SD.h"
#include "CRC16.h"
#include "CRC.h"
#include "arduino-timer.h"

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
#define LAST_ADDRESS    800

#define SWITCH_PIN 0       // GPIO0 (programmind mode pin)
#define LED_PIN LED_BUILTIN          // Status LED
#define DCD_PIN 2          // DCD Carrier Status
#define RTS_PIN 4         // RTS Request to Send, connect to host's CTS pin
#define CTS_PIN 5         // CTS Clear to Send, connect to host's RTS pin

#define SW1_PIN A0
#define SW2_PIN D0
#define SW3_PIN D3
#define SW4_PIN D4

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


// Global variables
String build = "v2.09b";
String cmd = "";              // Gather a new AT command to this string from serial
bool cmdMode = true;          // Are we in AT command mode or connected mode
bool callConnected = false;   // Are we currently in a call
bool telnet = false;          // Is telnet control code handling enabled
bool verboseResults = false;
//#define DEBUG 1             // Print additional debug information to serial channel
#undef DEBUG
#define LISTEN_PORT 6400      // Listen to this if not connected. Set to zero to disable.
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
const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
String baudDisp[] = { "300", "1200", "2400", "4800", "9600", "19.2k", "38.4k", "57.6k", "115k" };
String mainMenuDisp[] = { "MODEM Mode", "File Transfer", "Playback Text", "Settings" };
String settingsMenuDisp[] = { "MAIN", "Baud Rate", "Screen", "Default Menu", "Factory Reset", "Reboot", "About" };
String orientationMenuDisp[] = { "Normal", "Flipped" };
String playbackMenuDisp[] = { "MAIN", "List Files", "Display File", "Playback File", "Evaluate Key", "Terminal Mode" };
String fileMenuDisp[] = { "MAIN", "List Files on SD", "Send (from SD)", "Recieve (to SD)" };
String protocolMenuDisp[] = { "BACK", "Raw", "XModem", "YModem", "ZModem", "Kermit" };
String defaultModeDisp[] = { "Main Menu", "MODEM Mode" };
                          
byte serialspeed;
bool echo = true;
bool autoAnswer = false;
String ssid, password, busyMsg;
byte ringCount = 0;
String resultCodes[] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE", "BUSY", "NO ANSWER" };
enum resultCodes_t { R_OK, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
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
enum defaultMode_t { D_MAINMENU, D_MODEMMENU }; // Normal or Flipped
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

//file transfer related variables
const int chipSelect = D8;
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
String terminalMode = "VT100";
String fileName = "";
String files[100];
String waitTime;
bool msgFlag=false;
bool SW1=false;
bool SW2=false;
bool SW3=false;
bool SW4=false;

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClient tcpClient;
WiFiServer tcpServer(0);        // port will be set via .begin(port)
ESP8266WebServer webServer(80);
MDNSResponder mdns;

void(* resetFunc) (void) = 0; //declare reset function @ address 0

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

  EEPROM.write(BAUD_ADDRESS, serialspeed);
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
  
  for (int i = 0; i < 10; i++) {
    setEEPROM(speedDials[i], speedDialAddresses[i], 50);
  }
  EEPROM.commit();
}

void readSettings() {
  echo = EEPROM.read(ECHO_ADDRESS);
  autoAnswer = EEPROM.read(AUTO_ANSWER_ADDRESS);
  // serialspeed = EEPROM.read(BAUD_ADDRESS);

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
    //Serial.print(char(EEPROM.read(i)));
  }
  //Serial.println();
  return myString;
}

void setEEPROM(String inString, int startAddress, int maxLen) {
  for (int i = startAddress; i < inString.length() + startAddress; i++) {
    EEPROM.write(i, inString[i - startAddress]);
    //Serial.print(i, DEC); Serial.print(": "); Serial.println(inString[i - startAddress]);
    //if (EEPROM.read(i) != inString[i - startAddress]) { Serial.print(" (!)"); }
    //Serial.println();
  }
  // null pad the remainder of the memory space
  for (int i = inString.length() + startAddress; i < maxLen + startAddress; i++) {
    EEPROM.write(i, 0x00);
    //Serial.print(i, DEC); Serial.println(": 0x00");
  }
}

void sendResult(int resultCode) {
  Serial.print("\r\n");
  if (verboseResults == 0) {  
    Serial.println(resultCode);
    return;
  }
  if (resultCode == R_CONNECT) {
    Serial.print(String(resultCodes[R_CONNECT]) + " " + String(bauds[serialspeed]));
  } else if (resultCode == R_NOCARRIER) {
    Serial.print(String(resultCodes[R_NOCARRIER]) + " (" + connectTimeString() + ")");
  } else {
    Serial.print(String(resultCodes[resultCode]));
  }
  Serial.print("\r\n");
}

void sendString(String msg) {
  Serial.print("\r\n");
  Serial.print(msg);
  Serial.print("\r\n");
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
    Serial.flush();
    Serial.end();
    serialspeed = 0;
    delay(100);
    Serial.begin(bauds[serialspeed]);
    sendResult(R_OK);
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
    Serial.println("CONFIGURE SSID AND PASSWORD. TYPE AT? FOR HELP.");
    showMessage("CONFIGURE SSID\nAND PASSWORD\nOVER SERIAL");
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("\nCONNECTING TO SSID "); Serial.print(ssid);
  showMessage("* PLEASE WAIT *\n CONNECTING TO\n" + ssid);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    digitalWrite(LED_PIN, LOW);
    delay(250);
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (i == 21) {
    Serial.print("COULD NOT CONNECT TO "); Serial.println(ssid);
    WiFi.disconnect();
    updateLed();
  } else {
    Serial.print("CONNECTED TO "); Serial.println(WiFi.SSID());
    Serial.print("IP ADDRESS: "); Serial.println(WiFi.localIP());
    updateLed();
    modemMenuMsg();
    firmwareCheck();
  }
}

void updateLed() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);  // on
  } else {
    digitalWrite(LED_PIN, HIGH); //off
  }
}

void disconnectWiFi() {
  WiFi.disconnect();
  updateLed();
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
  if (foundBaud == serialspeed) {
    sendResult(R_OK);
    return;
  }
  Serial.print("SWITCHING SERIAL PORT TO ");
  Serial.print(inSpeed);
  Serial.println(" IN 5 SECONDS");
  delay(5000);
  Serial.end();
  delay(200);
  Serial.begin(bauds[foundBaud]);
  serialspeed = foundBaud;
  delay(200);
  sendResult(R_OK);
}

void setCarrier(byte carrier) {
  if (pinPolarity == P_NORMAL) carrier = !carrier;
  digitalWrite(DCD_PIN, carrier);
}

void displayNetworkStatus() {
  Serial.print("WIFI STATUS: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("CONNECTED");
  }
  if (WiFi.status() == WL_IDLE_STATUS) {
    Serial.println("OFFLINE");
  }
  if (WiFi.status() == WL_CONNECT_FAILED) {
    Serial.println("CONNECT FAILED");
  }
  if (WiFi.status() == WL_NO_SSID_AVAIL) {
    Serial.println("SSID UNAVAILABLE");
  }
  if (WiFi.status() == WL_CONNECTION_LOST) {
    Serial.println("CONNECTION LOST");
  }
  if (WiFi.status() == WL_DISCONNECTED) {
    Serial.println("DISCONNECTED");
  }
  if (WiFi.status() == WL_SCAN_COMPLETED) {
    Serial.println("SCAN COMPLETED");
  }
  yield();

  Serial.print("SSID.......: "); Serial.println(WiFi.SSID());

  //  Serial.print("ENCRYPTION: ");
  //  switch(WiFi.encryptionType()) {
  //    case 2:
  //      Serial.println("TKIP (WPA)");
  //      break;
  //    case 5:
  //      Serial.println("WEP");
  //      break;
  //    case 4:
  //      Serial.println("CCMP (WPA)");
  //      break;
  //    case 7:
  //      Serial.println("NONE");
  //      break;
  //    case 8:
  //      Serial.println("AUTO");
  //      break;
  //    default:
  //      Serial.println("UNKNOWN");
  //      break;
  //  }

  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC ADDRESS: ");
  Serial.print(mac[0], HEX);
  Serial.print(":");
  Serial.print(mac[1], HEX);
  Serial.print(":");
  Serial.print(mac[2], HEX);
  Serial.print(":");
  Serial.print(mac[3], HEX);
  Serial.print(":");
  Serial.print(mac[4], HEX);
  Serial.print(":");
  Serial.println(mac[5], HEX);
  yield();

  Serial.print("IP ADDRESS.: "); Serial.println(WiFi.localIP()); yield();
  Serial.print("GATEWAY....: "); Serial.println(WiFi.gatewayIP()); yield();
  Serial.print("SUBNET MASK: "); Serial.println(WiFi.subnetMask()); yield();
  Serial.print("SERVER PORT: "); Serial.println(tcpServerPort); yield();
  Serial.print("WEB CONFIG.: HTTP://"); Serial.println(WiFi.localIP()); yield();
  Serial.print("CALL STATUS: "); yield();
  if (callConnected) {
    Serial.print("CONNECTED TO "); Serial.println(ipToString(tcpClient.remoteIP())); yield();
    Serial.print("CALL LENGTH: "); Serial.println(connectTimeString()); yield();
  } else {
  }
}

void displayCurrentSettings() {
  Serial.println("ACTIVE PROFILE:"); yield();
  Serial.print("BAUD: "); Serial.println(bauds[serialspeed]); yield();
  Serial.print("SSID: "); Serial.println(ssid); yield();
  Serial.print("PASS: "); Serial.println(password); yield();
  //Serial.print("SERVER TCP PORT: "); Serial.println(tcpServerPort); yield();
  Serial.print("BUSY MSG: "); Serial.println(busyMsg); yield();
  Serial.print("E"); Serial.print(echo); Serial.print(" "); yield();
  Serial.print("V"); Serial.print(verboseResults); Serial.print(" "); yield();
  Serial.print("&K"); Serial.print(flowControl); Serial.print(" "); yield();
  Serial.print("&P"); Serial.print(pinPolarity); Serial.print(" "); yield();
  Serial.print("NET"); Serial.print(telnet); Serial.print(" "); yield();
  Serial.print("PET"); Serial.print(petTranslate); Serial.print(" "); yield();
  Serial.print("S0:"); Serial.print(autoAnswer); Serial.print(" "); yield();
  Serial.print("ORIENT:"); Serial.print(dispOrientation); Serial.print(" "); yield();
  Serial.print("DFLTMENU:"); Serial.print(defaultMode); Serial.print(" "); yield();
  Serial.println(); yield();

  Serial.println("SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial.print(i); Serial.print(": "); Serial.println(speedDials[i]);
    yield();
  }
  Serial.println();
}

void displayStoredSettings() {
  Serial.println("STORED PROFILE:"); yield();
  Serial.print("BAUD: "); Serial.println(bauds[EEPROM.read(BAUD_ADDRESS)]); yield();
  Serial.print("SSID: "); Serial.println(getEEPROM(SSID_ADDRESS, SSID_LEN)); yield();
  Serial.print("PASS: "); Serial.println(getEEPROM(PASS_ADDRESS, PASS_LEN)); yield();
  //Serial.print("SERVER TCP PORT: "); Serial.println(word(EEPROM.read(SERVER_PORT_ADDRESS), EEPROM.read(SERVER_PORT_ADDRESS+1))); yield();
  Serial.print("BUSY MSG: "); Serial.println(getEEPROM(BUSY_MSG_ADDRESS, BUSY_MSG_LEN)); yield();
  Serial.print("E"); Serial.print(EEPROM.read(ECHO_ADDRESS)); Serial.print(" "); yield();
  Serial.print("V"); Serial.print(EEPROM.read(VERBOSE_ADDRESS)); Serial.print(" "); yield();
  Serial.print("&K"); Serial.print(EEPROM.read(FLOW_CONTROL_ADDRESS)); Serial.print(" "); yield();
  Serial.print("&P"); Serial.print(EEPROM.read(PIN_POLARITY_ADDRESS)); Serial.print(" "); yield();
  Serial.print("NET"); Serial.print(EEPROM.read(TELNET_ADDRESS)); Serial.print(" "); yield();
  Serial.print("PET"); Serial.print(EEPROM.read(PET_TRANSLATE_ADDRESS)); Serial.print(" "); yield();
  Serial.print("S0:"); Serial.print(EEPROM.read(AUTO_ANSWER_ADDRESS)); Serial.print(" "); yield();
  Serial.print("ORIENT:"); Serial.print(EEPROM.read(ORIENTATION_ADDRESS)); Serial.print(" "); yield();
  Serial.println(); yield();

  Serial.println("STORED SPEED DIAL:");
  for (int i = 0; i < 10; i++) {
    Serial.print(i); Serial.print(": "); Serial.println(getEEPROM(speedDialAddresses[i], 50));
    yield();
  }
  Serial.println();
}

void waitForSpace() {
  Serial.print("PRESS SPACE");
  char c = 0;
  while (c != 0x20) {
    if (Serial.available() > 0) {
      c = Serial.read();
      if (petTranslate == true){
        if (c > 127) c-= 128;
      }
    }
  }
  Serial.print("\r");
}

void waitForEnter() {
  Serial.print("PRESS ENTER");
  char c = 0;
  while (c != 10 && c != 13) {
    if (Serial.available() > 0) {
      c = Serial.read();
      if (petTranslate == true){
        if (c > 127) c-= 128;
      }
    }
  }
  Serial.print("\r");
}

void displayHelp() {
  
  Serial.println("AT COMMAND SUMMARY:"); yield();
  Serial.println("DIAL HOST......: ATDTHOST:PORT"); yield();
  Serial.println("                 ATDTNNNNNNN (N=0-9)"); yield();
  Serial.println("SPEED DIAL.....: ATDSN (N=0-9)"); yield();
  Serial.println("SET SPEED DIAL.: AT&ZN=HOST:PORT (N=0-9)"); yield();
  Serial.println("HANDLE TELNET..: ATNETN (N=0,1)"); yield();
  Serial.println("PET MCTERM TR..: ATPETN (N=0,1)"); yield();
  Serial.println("NETWORK INFO...: ATI"); yield();
  Serial.println("HTTP GET.......: ATGET<URL>"); yield();
  //Serial.println("SERVER PORT....: AT$SP=N (N=1-65535)"); yield();
  Serial.println("AUTO ANSWER....: ATS0=N (N=0,1)"); yield();
  Serial.println("SET BUSY MSG...: AT$BM=YOUR BUSY MESSAGE"); yield();
  Serial.println("LOAD NVRAM.....: ATZ"); yield();
  Serial.println("SAVE TO NVRAM..: AT&W"); yield();
  Serial.println("SHOW SETTINGS..: AT&V"); yield();
  Serial.println("FACT. DEFAULTS.: AT&F"); yield();
  Serial.println("PIN POLARITY...: AT&PN (N=0/INV,1/NORM)"); yield();
  Serial.println("ECHO OFF/ON....: ATE0 / ATE1"); yield();
  Serial.println("VERBOSE OFF/ON.: ATV0 / ATV1"); yield();
  Serial.println("SET SSID.......: AT$SSID=WIFISSID"); yield();
  Serial.println("SET PASSWORD...: AT$PASS=WIFIPASSWORD"); yield();
  waitForSpace();
  Serial.println("SET BAUD RATE..: AT$SB=N (3,12,24,48,96"); yield();
  Serial.println("                 192,384,576,1152)*100"); yield();
  Serial.println("SET PORT.......: AT$SP=PORT"); yield();
  Serial.println("FLOW CONTROL...: AT&KN (N=0/N,1/HW,2/SW)"); yield();
  Serial.println("WIFI OFF/ON....: ATC0 / ATC1"); yield();
  Serial.println("HANGUP.........: ATH"); yield();
  Serial.println("ENTER CMD MODE.: +++"); yield();
  Serial.println("EXIT CMD MODE..: ATO"); yield();
  Serial.println("FIRMWARE CHECK.: ATFC"); yield();
  Serial.println("FIRMWARE UPDATE: ATFU"); yield();
  Serial.println("EXIT MODEM MODE: ATX"); yield();
  Serial.println("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //Serial.print("STORED "); Serial.print(num); Serial.print(": "); Serial.println(location);
}


String padLeft(String s, int len) {
  while (s.length()<len)
  {
    s = " " + s;
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
    Serial.println();
    Serial.println();
    Serial.println("-=-=- " + menu + " MENU -=-=-");
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
  } else {
    //just clear the selection area
    display.fillRect(6, 8, 8, 52, SSD1306_BLACK);
  }
 
  display.fillRect(0, 0, 127, 9, SSD1306_WHITE);
  display.setCursor(1, 1);     // Start at top-left corner
  display.setTextColor(SSD1306_BLACK); // Draw white text
  String line = "WiRSa " + menu;
  display.print(line);  
  String bps = baudDisp[serialspeed];
  display.println(padLeft(bps,21-line.length()));
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 13);     // Start at top-left corner
  display.display();

  return setDef;
}

void showMessage(String message) {
  display.fillRect(1, 10, 125, 53, SSD1306_BLACK);
  display.drawRect(16, 12, 97, 40, SSD1306_WHITE);
  display.fillRect(18, 14, 93, 36, SSD1306_WHITE);
  int x = 20;
  int y = 16;
  int c= 0;
  display.setCursor(x,y);
  display.setTextColor(SSD1306_BLACK);
  
  for (int i = 0; i < message.length(); i++) {
    if (message[i] == '\n') {
      x = 20; y += 8; c = 0;
      display.setCursor(x, y);
    } else if (c == 15) {
      x = 20; y += 8; c = 0;
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

  if (menuCnt<=6)
    menuEnd=menuCnt;
  else {
    //clear out menu area
    display.fillRect(2, 9, 123, 54, SSD1306_BLACK);
    
    menuStart=menuIdx;
    if (menuIdx<6)
      menuStart=0;
    else
      menuStart=menuIdx-5;
    
    menuEnd=menuStart+6;
    if (menuEnd>menuCnt)
      menuEnd=menuCnt; 
  }
  
  for (int i=menuStart;i<menuEnd;i++) {
    menuOption(i, options[i]);
  }

  if (dispMode!=MENU_DISP) {
    for (int i=0;i<menuCnt;i++) {
      if (dispMode==MENU_NUM)
        Serial.println(" [" + String(i) + "] " + options[i]);
      else //MENU_BOTH
        Serial.println(" [" + options[i].substring(0,1) + "]" + options[i].substring(1));
    }
  }
  
  if (menuStart>0) {
    display.setCursor(120, 13);
    display.write(0x1E);
  }

  if (menuEnd<menuCnt) {
    display.setCursor(120, 54);
    display.write(0x1F);
  }

  display.display();

  if (dispMode!=MENU_DISP) {
    Serial.println();
    Serial.print("> ");
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
  digitalWrite(LED_PIN, HIGH); // off
  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH);
  //pinMode(DCD_PIN, OUTPUT);
  //pinMode(RTS_PIN, OUTPUT);
  //digitalWrite(RTS_PIN, HIGH); // ready to receive data
  //pinMode(CTS_PIN, INPUT);
  //digitalWrite(CTS_PIN, HIGH); // pull up
  setCarrier(false);

  EEPROM.begin(LAST_ADDRESS + 1);
  delay(10);

  if (EEPROM.read(VERSION_ADDRESS) != VERSIONA || EEPROM.read(VERSION_ADDRESS + 1) != VERSIONB) {
    defaultEEPROM();
  }

  readSettings();
  // Fetch baud rate from EEPROM
  serialspeed = EEPROM.read(BAUD_ADDRESS);
  // Check if it's out of bounds-- we have to be able to talk
  if (serialspeed < 0 || serialspeed > sizeof(bauds)) {
    serialspeed = 0;
  }

  if (dispOrientation==D_NORMAL)
    display.setRotation(0);
  else
    display.setRotation(2);

  
  Serial.begin(bauds[serialspeed]);
  pinMode(SW1_PIN, INPUT);
  pinMode(SW2_PIN, INPUT);
  pinMode(SW3_PIN, INPUT);
  pinMode(SW4_PIN, INPUT);
  Serial.println("");
  Serial.println("-= RetroDisks  WiRSa =-");

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
  if (dispOrientation==D_NORMAL) {
    SW1 = (analogRead(A0) < 100); //must read a0 as an analog, < 100 = pushed
    delay(3);   // required after analogRead to avoid WiFi disconnects (known ESP8266 issue)
    SW2 = !digitalRead(D0); 
  } else {
    SW2 = (analogRead(A0) < 100); //must read a0 as an analog, < 100 = pushed
    delay(3);   // required after analogRead to avoid WiFi disconnects (known ESP8266 issue)
    SW1 = !digitalRead(D0);     
  }
  SW3 = !digitalRead(D3);
  SW4 = !digitalRead(D4); 
}

void readBackSwitch()
{ //not sure why but when in a call, SW1 analog read hangs the connection [obsolete: delay(3) workaround resolves]
  //only concerned about 'back' button push anyways, hence this function
  SW1 = LOW; //must read a0 as an analog, < 100 = pushed
  SW2 = LOW; 
  SW3 = LOW;
  SW4 = !digitalRead(D4); 
}

void waitSwitches()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(SW1||SW2||SW3||SW4) {
      delay(10);
      readSwitches();
    }
  }
}

void waitBackSwitch()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(SW1||SW2||SW3||SW4) {
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
  Serial.flush();
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
  Serial.print("DIALING "); Serial.print(host); Serial.print(":"); Serial.println(port);
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
    Serial.flush();
    callConnected = true;
    setCarrier(callConnected);
    display.clearDisplay();
    display.setCursor(1, 1); 
    display.display();
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
  Serial.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  /**** Just AT ****/
  if (upCmd == "AT") sendResult(R_OK);

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0) || (upCmd.indexOf("ATDS") == 0))
  {
    dialOut(upCmd);
  }

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    sendResult(R_OK);
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    sendResult(R_OK);
  }

  else if (upCmd == "ATNET?") {
    Serial.println(String(telnet));
    sendResult(R_OK);
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient()) {
    answerCall();
  }

  /**** Display Help ****/
  else if (upCmd == "AT?" || upCmd == "ATHELP") {
    displayHelp();
    sendResult(R_OK);
  }

  /**** Reset, reload settings from EEPROM ****/
  else if (upCmd == "ATZ") {
    readSettings();
    sendResult(R_OK);
  }

  /**** Disconnect WiFi ****/
  else if (upCmd == "ATC0") {
    disconnectWiFi();
    sendResult(R_OK);
  }

  /**** Connect WiFi ****/
  else if (upCmd == "ATC1") {
    connectWiFi();
    sendResult(R_OK);
  }

  /**** Control local echo in command mode ****/
  else if (upCmd.indexOf("ATE") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(echo));
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      echo = 0;
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      echo = 1;
      sendResult(R_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control verbosity ****/
  else if (upCmd.indexOf("ATV") == 0) {
    if (upCmd.substring(3, 4) == "?") {
      sendString(String(verboseResults));
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "0") {
      verboseResults = 0;
      sendResult(R_OK);
    }
    else if (upCmd.substring(3, 4) == "1") {
      verboseResults = 1;
      sendResult(R_OK);
    }
    else {
      sendResult(R_ERROR);
    }
  }

  /**** Control pin polarity of CTS, RTS, DCD ****/
  else if (upCmd.indexOf("AT&P") == 0) {
    if (upCmd.substring(4, 5) == "?") {
      sendString(String(pinPolarity));
      sendResult(R_OK);
    }
    else if (upCmd.substring(4, 5) == "0") {
      pinPolarity = P_INVERTED;
      sendResult(R_OK);
      setCarrier(callConnected);
    }
    else if (upCmd.substring(4, 5) == "1") {
      pinPolarity = P_NORMAL;
      sendResult(R_OK);
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
      sendResult(R_OK);
    }
    else if (upCmd.substring(4, 5) == "0") {
      flowControl = 0;
      sendResult(R_OK);
    }
    else if (upCmd.substring(4, 5) == "1") {
      flowControl = 1;
      sendResult(R_OK);
    }
    else if (upCmd.substring(4, 5) == "2") {
      flowControl = 2;
      sendResult(R_OK);
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
    sendString(String(bauds[serialspeed]));;
  }

  /**** Set busy message ****/
  else if (upCmd.indexOf("AT$BM=") == 0) {
    busyMsg = cmd.substring(6);
    sendResult(R_OK);
  }

  /**** Display busy message ****/
  else if (upCmd.indexOf("AT$BM?") == 0) {
    sendString(busyMsg);
    sendResult(R_OK);
  }

  /**** Display Network settings ****/
  else if (upCmd == "ATI") {
    displayNetworkStatus();
    sendResult(R_OK);
  }

  /**** Display profile settings ****/
  else if (upCmd == "AT&V") {
    displayCurrentSettings();
    waitForSpace();
    displayStoredSettings();
    sendResult(R_OK);
  }

  /**** Save (write) current settings to EEPROM ****/
  else if (upCmd == "AT&W") {
    writeSettings();
    sendResult(R_OK);
  }

  /**** Set or display a speed dial number ****/
  else if (upCmd.indexOf("AT&Z") == 0) {
    byte speedNum = upCmd.substring(4, 5).toInt();
    if (speedNum >= 0 && speedNum <= 9) {
      if (upCmd.substring(5, 6) == "=") {
        String speedDial = cmd;
        storeSpeedDial(speedNum, speedDial.substring(6));
        sendResult(R_OK);
      }
      if (upCmd.substring(5, 6) == "?") {
        sendString(speedDials[speedNum]);
        sendResult(R_OK);
      }
    } else {
      sendResult(R_ERROR);
    }
  }

  /**** Set WiFi SSID ****/
  else if (upCmd.indexOf("AT$SSID=") == 0) {
    ssid = cmd.substring(8);
    sendResult(R_OK);
  }

  /**** Display WiFi SSID ****/
  else if (upCmd == "AT$SSID?") {
    sendString(ssid);
    sendResult(R_OK);
  }

  /**** Set WiFi Password ****/
  else if (upCmd.indexOf("AT$PASS=") == 0) {
    password = cmd.substring(8);
    sendResult(R_OK);
  }

  /**** Display WiFi Password ****/
  else if (upCmd == "AT$PASS?") {
    sendString(password);
    sendResult(R_OK);
  }

  /**** Reset EEPROM and current settings to factory defaults ****/
  else if (upCmd == "AT&F") {
    defaultEEPROM();
    readSettings();
    sendResult(R_OK);
  }

  /**** Set auto answer off ****/
  else if (upCmd == "ATS0=0") {
    autoAnswer = false;
    sendResult(R_OK);
  }

  /**** Set auto answer on ****/
  else if (upCmd == "ATS0=1") {
    autoAnswer = true;
    sendResult(R_OK);
  }

  /**** Display auto answer setting ****/
  else if (upCmd == "ATS0?") {
    sendString(String(autoAnswer));
    sendResult(R_OK);
  }

  /**** Set PET MCTerm Translate On ****/
  else if (upCmd == "ATPET=1") {
    petTranslate = true;
    sendResult(R_OK);
  }

  /**** Set PET MCTerm Translate Off ****/
  else if (upCmd == "ATPET=0") {
    petTranslate = false;
    sendResult(R_OK);
  }

  /**** Display PET MCTerm Translate Setting ****/
  else if (upCmd == "ATPET?") {
    sendString(String(petTranslate));
    sendResult(R_OK);
  }

  /**** Set HEX Translate On ****/
  else if (upCmd == "ATHEX=1") {
    bHex = true;
    sendResult(R_OK);
  }

  /**** Set HEX Translate Off ****/
  else if (upCmd == "ATHEX=0") {
    bHex = false;
    sendResult(R_OK);
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("ATH") == 0) {
    hangUp();
  }

  /**** Hang up a call ****/
  else if (upCmd.indexOf("AT$RB") == 0) {
    sendResult(R_OK);
    Serial.flush();
    delay(500);
    ESP.reset();
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
    sendResult(R_OK);
  }

  /**** Display icoming TCP server port ****/
  else if (upCmd == "AT$SP?") {
    sendString(String(tcpServerPort));
    sendResult(R_OK);
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP?")
  {
    Serial.println(WiFi.localIP());
    sendResult(R_OK);
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
    Serial.println("\r\nEntering MODEM Mode...");
    modemMenuMsg();
    if (tcpServerPort > 0) tcpServer.begin(tcpServerPort);
  
    WiFi.mode(WIFI_STA);
    connectWiFi();
    sendResult(R_OK);
    //tcpServer(tcpServerPort); // can't start tcpServer inside a function-- must live outside
  
    digitalWrite(LED_PIN, LOW); // on
  
    webServer.on("/", handleRoot);
    webServer.on("/ath", handleWebHangUp);
    webServer.begin();
    mdns.begin("C64WiFi", WiFi.localIP());
}

void mainLoop()
{
    int menuSel=-1;
    readSwitches();
    if (SW1) {  //UP
        menuIdx--;
        if (menuIdx<0)
          menuIdx=menuCnt-1;
        mainMenu(true);
    }
    else if (SW2) { //DOWN
      menuIdx++;
      if (menuIdx>(menuCnt-1))
        menuIdx=0;
      mainMenu(true);
    }
    else if (SW3) { //ENTER
      menuSel = menuIdx;
    }
    else if (SW4) { //BACK
    }
    waitSwitches();

    int serAvl = Serial.available();
    char chr;
    if (serAvl>0) {
      chr = Serial.read();
      Serial.print(chr);
    }

    if (serAvl>0 || menuSel>-1)
    {
      if (chr=='M'||chr=='m'||menuSel==0) //enter modem mode
      {
        enterModemMode();
      }
      else if (chr=='F'||chr=='f'||menuSel==1)
      {
        Serial.println("");
        Serial.println("Initializing SD card...");
        if (!SD.begin(chipSelect)) {
          Serial.println("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        Serial.println("Initialization Complete.");
        fileMenu(false);
      }
      else if (chr=='P'||chr=='p'||menuSel==2)
      {
        Serial.println("");
        Serial.println("Initializing SD card...");
        if (!SD.begin(chipSelect)) {
          Serial.println("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        Serial.println("Initialization Complete.");        
        playbackMenu(false);
      }
      else if (chr=='S'||chr=='s'||menuSel==3)
      {
        settingsMenu(false);
      }
      else if (serAvl>0)
      { //redisplay the menu 
        mainMenu(false);
      }
    }
}

void baudLoop()
{
  int menuSel=-1;
  readSwitches();
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      baudMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    baudMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    settingsMenu(false);
  }
  waitSwitches();

  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
  }
  
  if (menuSel>-1)
  {
    serialspeed = menuIdx;
    writeSettings();
    Serial.end();
    delay(200);
    Serial.begin(bauds[serialspeed]);
    settingsMenu(false);
  } else if (serAvl>0) {
    if (chr>=48 && chr <=57) //between 0-9
    {
      serialspeed = chr-48;
      writeSettings();
      Serial.end();
      delay(200);
      Serial.begin(bauds[serialspeed]);
      settingsMenu(false);
    } else
      baudMenu(false);
  }
}

void orientationLoop()
{
  int menuSel=-1;
  readSwitches();
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      orientationMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    orientationMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    settingsMenu(false);
  }
  waitSwitches();
  
  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
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
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      defaultModeMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    defaultModeMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    settingsMenu(false);
  }
  waitSwitches();
  
  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
  }

  if (serAvl>0 || menuSel>-1)
  {
    if (chr=='0')
      menuSel=0;
    else if (chr=='1')
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
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      settingsMenu(true);
  } else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    settingsMenu(true);
  } else if (SW3) { //ENTER
    menuSel = menuIdx;
  } else if (SW4) { //BACK
    mainMenu(false);
  }
  waitSwitches();
    
  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
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
      orientationMenu(false);
    }
    else if (chr=='D'||chr=='d'||menuSel==3)
    {
      defaultModeMenu(false);
    }
    else if (chr=='F'||chr=='f'||menuSel==4)
    {
       Serial.println("** PLEASE WAIT: FACTORY RESET **");
       showMessage("***************\n* PLEASE WAIT *\n*FACTORY RESET*\n***************");
       defaultEEPROM();
       resetFunc();
    }
    else if (chr=='R'||chr=='r'||menuSel==5)
    {
      Serial.println("** PLEASE WAIT: REBOOTING **");
      showMessage("***************\n* PLEASE WAIT *\n*  REBOOTING  *\n***************");
      resetFunc();
    }    
    else if (chr=='A'||chr=='a'||menuSel==6)
    {
      Serial.println("** WiRSa BUILD:   " + build + " **");
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
  showMenu("SETTINGS", settingsMenuDisp, 7, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void orientationMenu(bool arrow) {
  menuMode = MODE_ORIENTATION;
  showMenu("SCREEN", orientationMenuDisp, 2, (arrow?MENU_DISP:MENU_BOTH), dispOrientation);
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
  showMenu("SET BAUD", baudDisp, 9, (arrow?MENU_DISP:MENU_NUM), serialspeed);
}

void fileMenu(bool arrow) {
  menuMode = MODE_FILE;
  showMenu("FILE XFER", fileMenuDisp, 4, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void fileLoop()
{
  int menuSel=-1;
  readSwitches();
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      fileMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    fileMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    mainMenu(false);
  }
  waitSwitches();

  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
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
      Serial.println("\r\n\r\nTransfer Log:");
      logFile = SD.open("logfile.txt");
      if (logFile) {
        while (logFile.available()) {
          Serial.write(logFile.read());
        }
        logFile.close();
      } else
        Serial.println("Unable to open log file");
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
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      protocolMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    protocolMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    fileMenu(false);
  }
  waitSwitches();

  int serAvl = Serial.available();
  char chr;
  if (serAvl>0) {
    chr = Serial.read();
    Serial.print(chr);
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
      Serial.println("NOT YET IMPLEMENTED");
      /*if (xferMode == XFER_SEND)
        sendFileZMODEM();
      else if (xferMode == XFER_RECV)
        receiveFileZMODEM();*/
    }
    else if (chr=='K'||chr=='k'||menuSel==5) //KERMIT
    {
      showMessage("NOT YET\nIMPLEMENTED");
      Serial.println("NOT YET IMPLEMENTED");
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
  bool cncl = SW4;
  waitBackSwitch();
  return cncl;
}

void listFilesLoop()
{
  int menuSel=-1;
  readSwitches();
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      listFilesMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    listFilesMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    fileMenu(false);
  }
  waitSwitches();
  
  if (Serial.available() || menuSel>=-1)
  {
    char chr = Serial.read();
    Serial.print(chr);

    if (menuSel>-1){
      fileName = files[menuSel];
      fileMenu(false);
      Serial.println("Chosen file: " + fileName);
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

void serialWrite(char c, String log)
{
  if (log!="")
    addLog("> [" + log + "] 0x" + String(c, HEX) + " " + String(c) + "\r\n");
  updateXferMessage();
  Serial.write(c); 
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
  Serial.println("");
  Serial.println("");
  Serial.print(msg);
  if (dflt!="")
    Serial.print("[" + dflt + "] ");
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
  Serial.println("Sending " + fileName);
  Serial.println("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //Serial.println("Couldn't open log file");
  File dataFile = SD.open(fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING XMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopXMODEM(dataFile, fileName, fileSize);
    Serial.println("Download Complete");
    dataFile.close();
  } else {
    Serial.println("Invalid File");
  }
}

void sendFileYMODEM()
{
  resetGlobals();
  fileName=prompt("Please enter the filename to send: ");
  Serial.println("Sending " + fileName);
  Serial.println("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //Serial.println("Couldn't open log file");
  File dataFile = SD.open(fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING YMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopYMODEM(dataFile, fileName, fileSize);
    Serial.println("Download Complete");
    dataFile.close();
  } else {
    Serial.println("Invalid File");
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
    Serial.println("\r\nStarting transfer in " + waitTime + " seconds...");
    digitalWrite(LED_PIN, LOW);
    delay(waitTime.toInt()*1000);
    digitalWrite(LED_PIN, HIGH);
    int c=0;
    for (int i=0; i<fileSize.toInt(); i++)
    {
      if (dataFile.available())
      {
        Serial.write(dataFile.read());
        if (c==512)
        { //flush the buffer every so often to avoid a WDT reset
          Serial.flush();
          led_on();
          yield();
          c=0;
        }
        else
          c++;
      }
    }
    Serial.flush();
    Serial.write((char)26); //DOS EOF
    Serial.flush();
    yield();
    digitalWrite(LED_PIN, LOW);
    dataFile.close();
  } else {
    Serial.println("\r\nInvalid File");
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
      if (Serial.available() > 0) {
        char c = Serial.read();
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
    Serial.println("Transfer Cancelled");
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
    if (Serial.available() > 0 || forceStart) {
      char c = Serial.read();
      if (forceStart)
        c=0x15; //NAK will start the transfer
        
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          if (lastPacket)
          {
            serialWrite(0x04, "EOT after last packet " + String(eotSent)); //EOT
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
          //Serial.println("NAK");
          if (lastPacket)
          {
              serialWrite(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //Serial.println("EOT");
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
    if (Serial.available() > 0) {
      char c = Serial.read();
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          //Serial.println("ACK");
          if (lastPacket && eotSent<2)
          {
            serialWrite(0x04, "EOT after last packet " + String(eotSent)); //EOT
            //Serial.println("EOT");
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
          //Serial.println("NAK");
          finalFileSent = false;
          if (lastPacket && eotSent<2)
          {
              serialWrite(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //Serial.println("EOT");
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
      serialWrite(0xFF, "escaping FF");
      
    serialWrite(packet[i], logmsg);
  }
  Serial.flush();
}

void receiveFileXMODEM()
{
  SD.remove("logfile.txt");
  Serial.println("");
  Serial.println("");
  fileName=prompt("Please enter the filename to receive: ");

  SD.remove(fileName);
  xferFile = SD.open(fileName, FILE_WRITE);
  if (xferFile) {
    xferMsg = "RECEIVE XMODEM\nBytes Recevied:\n";
    waitTime = prompt("Start time in seconds: ", "30");  
    Serial.println("\r\nStarting transfer in " + waitTime + " seconds...");
    receiveLoopXMODEM();
    
    xferFile.flush();
    xferFile.close();
    clearInputBuffer();
    Serial.println("Download Complete");
  } else
    Serial.println("Transfer Cancelled");
}

void receiveFileYMODEM()
{
  SD.remove("logfile.txt");
  xferMsg = "RECEIVE YMODEM\nBytes Recevied:\n";
  Serial.println("");
  Serial.println("");
  waitTime = prompt("Start time in seconds: ", "30");  
  Serial.println("\r\nStarting transfer in " + waitTime + " seconds...");
  receiveLoopYMODEM();
  xferFile.flush();
  xferFile.close();
  clearInputBuffer();
  Serial.println("Download Complete");
}

void clearInputBuffer()
{
  while(Serial.available()>0)
  {
    char discard = Serial.read();
  }
}

bool sendStartByte(void *)
{
  //if (packetNumber>0)
  if (packetNumber>1)
    return false;
  else
  {
    serialWrite(0x43, "Sending Start C from Timer"); //letter C
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

    if (Serial.available() > 0) {
      char c = Serial.read();

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
        serialWrite(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          serialWrite(0x06, "ACK after EOT"); //ACK              
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
                serialWrite(0x06, "ACK after good packet"); //ACK
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

    if (Serial.available() > 0) {
      char c = Serial.read();

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
        serialWrite(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        updateXferMessage();
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          serialWrite(0x06, "ACK after EOT"); //ACK              
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
                serialWrite(0x06, "ACK after good packet"); //ACK
                if (zeroPacket)
                {
                  serialWrite(0x43, "send C after first ACK"); //'C'  //also send a "C" after the first ACK so actual transfer begins
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
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      playbackMenu(true);
  }
  else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    playbackMenu(true);
  }
  else if (SW3) { //ENTER
    menuSel = menuIdx;
  }
  else if (SW4) { //BACK
    mainMenu(false);
  }
  waitSwitches();

  if (Serial.available() || menuSel>-1)
  {
    char chr = Serial.read();
    Serial.println(chr);
    
    if (chr == 'L' || chr == 'l') {
      listFiles();
    } else if (chr == 'T' || chr == 't') {
      changeTerminalMode();
    } else if (chr == 'E' || chr == 'e') {
      evalKey();
    } else if (chr == 'D' || chr == 'd') {
      Serial.print("Enter Filename: ");
      String fileName = getLine();
      if (fileName!="") {
        if (displayFile(fileName, false, 0))
          waitKey(27,96);
        Serial.println("");
        playbackMenu(false);
      }
    } else if (chr == 'P' || chr == 'p') {
      Serial.print("Enter Filename: ");
      fileName = getLine();
      if (fileName!="") {
        Serial.println("");
        Serial.print("Begin at Position: ");
        String pos = getLine();
        if (displayFile(fileName, true, pos.toInt()))
          waitKey(27,96);
        Serial.println("");
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
   Serial.println("");
   Serial.println("listing files on SD card..");
   Serial.println("");
   File root = SD.open("/");
   Serial.println("FILENAME            SIZE");
   Serial.println("--------            ----");
   while(true) {
     File entry =  root.openNextFile();
     if (! entry) {
       // no more files
       break;
     }
     if (!entry.isDirectory()) {
         String fn = entry.name();
         Serial.print(fn);
         for(int i=0; i<20-fn.length(); i++)
         {
            Serial.print(" ");
         }
         Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
   root.close();
   Serial.println("");
   Serial.print("> ");
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
      //Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
  root.close();

  showMenu("FILE LIST", files, c, (arrow?MENU_DISP:MENU_NUM), 0);
}

void modemMenu() {
  menuMode = MODE_MODEM;
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

void firmwareCheck() {
  //http://update.retrodisks.com/wirsa-v2.php
  //This works by checking the latest release tag on the hosted github page
  //I have to call the github API page at https://api.github.com/repos/nullvalue0/WiRSa/releases/latest
  //However the problem is this only works over SSL and returns a large result as JSON. Hitting SSL 
  //pages from the ESP8266 is a lot of work, so to get around it a built a very simple PHP script that 
  //I host on a plain HTTP site. It hits the github API endpoint over HTTPS, parses the JSON and just 
  //returns the latest version string over HTTP as plain text - this way the ESP8266 can easily check 
  //the latest version.

  Serial.println("\nChecking firmware version..."); yield();
  WiFiClient client;
  HTTPClient http;
  if (http.begin(client, "http://update.retrodisks.com/wirsa-v2.php")) {
      int httpCode = http.GET();
      if (httpCode == 200) {
        http.getString();
        String version = http.getString();
        Serial.println("Latest Version: " + version + ", Device Version: " + build);
        if (build!=version)
          Serial.println("WiRSa firmware update available, download the latest release at https://github.com/nullvalue0/WiRSa or use commmand ATFU to apply updates now.");
        else
          Serial.println("Your WiRSa is running the latest firmware version.");
      }
      else
        Serial.println("Firmware version check failed.");
  }
}

void firmwareUpdate() {
  //Similar to the firmware check, I have a proxy php script calls the github, gets the latest release 
  //url, downloads the binary and forwards it over plain HTTP so the ESP8266 can download it and use
  //it for OTA updates. These php scripts are in the github repository under the firmware folder.

  WiFiClient client;

  ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
  ESPhttpUpdate.onStart(firmwareUpdateStarted);
  ESPhttpUpdate.onEnd(firmwareUpdateFinished);
  ESPhttpUpdate.onProgress(firmwareUpdateProgress);
  ESPhttpUpdate.onError(firmwareUpdateError);

  t_httpUpdate_return ret = ESPhttpUpdate.update(client, "http://update.retrodisks.com/wirsa-bin-v2.php");
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK");
      break;
  }
}

void firmwareUpdateStarted() {
  Serial.println("HTTP update process started");
}

void firmwareUpdateFinished() {
  Serial.println("HTTP update process finished...\r\n\r\nPLEASE WAIT, APPLYING UPDATE - DEVICE WILL REBOOT ON IT'S OWN WHEN COMPLETE IN ABOUT 10 SECONDS\r\n\r\n");
}

void firmwareUpdateProgress(int cur, int total) {
  Serial.printf("HTTP update process at %d of %d bytes...\r\n", cur, total);
}

void firmwareUpdateError(int err) {
  Serial.printf("HTTP update fatal error code %\r\n", err);
}


void modemLoop()
{  
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
  if (SW1) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      modemMenu();
  } else if (SW2) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    modemMenu();
  } else if (SW3) { //ENTER
    dialOut("ATDS" + String(menuIdx));
  } else if (SW4) { //BACK
    //if in a call, first back push ends call, 2nd exits modem mode
    if (cmdMode==true)
      mainMenu(false);
    else {
      hangUp();
      cmdMode = true;  
      msgFlag=true; //force full menu redraw
      modemMenuMsg();
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
    if (Serial.available())
    {
      char chr = Serial.read();

      if (petTranslate == true)
        // Fix PET MCTerm 1.26C Pet->ASCII encoding to actual ASCII
        if (chr > 127) chr-= 128;
      else
        // Convert uppercase PETSCII to lowercase ASCII (C64) in command mode only
        if ((chr >= 193) && (chr <= 218)) chr-= 96;

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        displayChar('\n');
        display.display();
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          Serial.write(chr);
          displayChar(chr);
          display.display();
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          Serial.write(chr);
          displayChar(chr);
        }
        if (bHex) {
          Serial.print(chr, HEX);
        }
      }
    }
  }
  /**** Connected mode ****/
  else
  {
    // Transmit from terminal to TCP
    if (Serial.available())
    {
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
      size_t len = std::min(Serial.available(), max_buf_size);
      len = Serial.readBytes(&txBuf[0], len);

      if (len > 0) displayChar('[');
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
        displayChar(txBuf[i]);
      }
      if (len > 0)
      {
        displayChar(']');
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
        Serial.print("<t>");
#endif
        rxByte = tcpClient.read();
        if (rxByte == 0xff)
        {
          // 2 times 0xff is just an escaped real 0xff
          Serial.write(0xff); Serial.flush();
        }
        else
        {
          // rxByte has now the first byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte);
          Serial.print(",");
#endif
          uint8_t cmdByte1 = rxByte;
          rxByte = tcpClient.read();
          uint8_t cmdByte2 = rxByte;
          // rxByte has now the second byte of the actual non-escaped control code
#ifdef DEBUG
          Serial.print(rxByte); Serial.flush();
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
        Serial.print("</t>");
#endif
      }
      else
      {
        // Non-control codes pass through freely
        Serial.write(rxByte); 
        displayChar(rxByte);
        Serial.flush(); yield();
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
      sendResult(R_OK);
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
    modemMenuMsg();
    //if (tcpServerPort > 0) tcpServer.begin();
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
  if (millis() - ledTime > LED_TIME) digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // toggle LED state
}

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
}

void changeTerminalMode()
{
  if (terminalMode=="VT100")
    terminalMode="Viewpoint";
  else if (terminalMode=="Viewpoint")
    terminalMode="VT100";

  Serial.println("Terminal Mode set to: " + terminalMode);
}

void clearScreen()
{
  if (terminalMode=="VT100")
  {
    Serial.print((char)27);
    Serial.print("[2J");  //clear screen
    Serial.print((char)27);
    Serial.print("[1;1H"); //cursor first row/col
  }
  else if (terminalMode=="Viewpoint")
  {
    Serial.print((char)26); //clear screen
    Serial.print((char)27);
    Serial.print("="); //cursor first row/col
    Serial.print((char)1);
    Serial.print((char)1);
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
        Serial.print((char)27);
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
        Serial.write(chr);
      pos++;
    }
    // close the file:
    myFile.close();
    return true;
  } else {
    // if the file didn't open, print an error:
    Serial.println("error opening '" + filename + "'");
    return false;
  }
}

void evalKey()
{
  Serial.print("Press Key (repeat same key 3 times to exit): ");
  Serial.println("\n");
  Serial.println("DEC\tHEX\tCHR");
  char lastKey;
  int lastCnt=0;
  while(1) {
    char c = getKey();
    Serial.print(c, DEC);
    Serial.print("\t");
    Serial.print(c, HEX);
    Serial.print("\t");
    Serial.println(c);
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
    if (Serial.available() > 0)
      return Serial.read();
  }
}

void waitKey(int key1, int key2)
{
  while (true)
  {
    if (Serial.available() > 0)
    {
      char c = Serial.read();
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
    if (Serial.available() > 0)
    {
      c = Serial.read();
      if (c==13)
        return line;
      else if (c==8 || c==127) {
        if (line.length()>0) {
          Serial.print(c);
          line = line.substring(0, line.length()-1);
        }
      }
      else
      {
        Serial.print(c);
        line += c;
      }
    }
  }
}

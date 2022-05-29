/*
   RetroDisks WiRSa - Wifi RS232 Serial Modem Adapter with File Transfer features
   Copyright (C) 2022 Aron Hoekstra <nullvalue@gmail.com>

   based on:
     ESP8266 based virtual modem
     Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>
     https://github.com/jsalin/esp8266_modem
   
   and the improvements added by:
     WiFi SIXFOUR - A virtual WiFi modem based on the ESP 8266 chipset
     Copyright (C) 2016 Paul Rickards <rickards@gmail.com>
   
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
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <LinkedList.h>
#include <SPI.h>
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
#define LAST_ADDRESS    780

#define SWITCH_PIN 0       // GPIO0 (programmind mode pin)
#define LED_PIN 12          // Status LED
#define DCD_PIN 2          // DCD Carrier Status
#define RTS_PIN 4         // RTS Request to Send, connect to host's CTS pin
#define CTS_PIN 5         // CTS Clear to Send, connect to host's RTS pin

#define MODE_MAIN 0
#define MODE_MODEM 1
#define MODE_FILE 2
#define MODE_PLAYBACK 3

// Global variables
String build = "20160621182048";
String cmd = "";           // Gather a new AT command to this string from serial
bool cmdMode = true;       // Are we in AT command mode or connected mode
bool callConnected = false;// Are we currently in a call
bool telnet = false;       // Is telnet control code handling enabled
bool verboseResults = false;
//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG
#define LISTEN_PORT 6400   // Listen to this if not connected. Set to zero to disable.
int tcpServerPort = LISTEN_PORT;
#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
//long myBps;                // What is the current BPS setting
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
char plusCount = 0;        // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;// When did we last receive a "+++" sequence
#define LED_TIME 15         // How many ms to keep LED on at activity
unsigned long ledTime = 0;
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];
const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];
const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
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

int menuMode = MODE_MAIN;

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

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClient tcpClient;
WiFiServer tcpServer(tcpServerPort);
ESP8266WebServer webServer(80);
MDNSResponder mdns;

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

  EEPROM.write(BAUD_ADDRESS, 0x00);
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
  setEEPROM("heatwavebbs.com:9640", speedDialAddresses[5], 50);

  for (int i = 5; i < 10; i++) {
    setEEPROM("", speedDialAddresses[i], 50);
  }

  setEEPROM("SORRY, SYSTEM IS CURRENTLY BUSY. PLEASE TRY AGAIN LATER.", BUSY_MSG_ADDRESS, BUSY_MSG_LEN);
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
    return;
  }
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("\nCONNECTING TO SSID "); Serial.print(ssid);
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
    Serial.print("COULD NOT CONNET TO "); Serial.println(ssid);
    WiFi.disconnect();
    updateLed();
  } else {
    Serial.print("CONNECTED TO "); Serial.println(WiFi.SSID());
    Serial.print("IP ADDRESS: "); Serial.println(WiFi.localIP());
    updateLed();
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
  mainMenu();
  Serial.println("AT COMMAND SUMMARY:"); yield();
  Serial.println("DIAL HOST......: ATDTHOST:PORT"); yield();
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
  Serial.println("SET BAUD RATE..: AT$SB=N (3,12,24,48,96"); yield();
  Serial.println("                 192,384,576,1152)*100"); yield();
  waitForSpace();
  Serial.println("FLOW CONTROL...: AT&KN (N=0/N,1/HW,2/SW)"); yield();
  Serial.println("WIFI OFF/ON....: ATC0 / ATC1"); yield();
  Serial.println("HANGUP.........: ATH"); yield();
  Serial.println("ENTER CMD MODE.: +++"); yield();
  Serial.println("EXIT CMD MODE..: ATO"); yield();
  Serial.println("EXIT MODEM MODE: ATX"); yield();
  Serial.println("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //Serial.print("STORED "); Serial.print(num); Serial.print(": "); Serial.println(location);
}

void mainMenu() {
  Serial.println();
  Serial.println();
  Serial.println("-= RetroDisks  WiRSa =-");
  Serial.println("-=-=-  MAIN MENU  -=-=-");
  Serial.println(" [M]ODEM Mode");
  Serial.println(" [F]ile Transfer");
  Serial.println(" [P]layback Text");
  Serial.println();
  Serial.print("> ");
}

/**
   Arduino main init function
*/
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // off
  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH);
  pinMode(DCD_PIN, OUTPUT);
  pinMode(RTS_PIN, OUTPUT);
  digitalWrite(RTS_PIN, HIGH); // ready to receive data
  pinMode(CTS_PIN, INPUT);
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

  Serial.begin(bauds[serialspeed]);

  char c;
  while (c != 0x0a && c != 0x0d) {
    if (Serial.available() > 0) {
      c = Serial.read();
      if (petTranslate == true){
        if (c > 127) c-= 128;
      }
    }
    if (checkButton() == 1) {
      break; // button pressed, we're setting to 300 baud and moving on
    }
    yield();
  }

  mainMenu();
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
    WiFiClient tempClient = tcpServer.available(); // this is the key to keeping the connection open
    tcpClient = tempClient; // hand over the new connection to the global client
    tempClient.stop();   // stop the temporary one
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

  else if (upCmd == "ATX") {
    menuMode=MODE_MAIN;
    mainMenu();
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
}

void mainLoop()
{
    if (Serial.available())
    {
      char chr = Serial.read();
      Serial.print(chr);
      if (chr=='M'||chr=='m') //enter modem mode
      {
        Serial.println("\r\nEntering MODEM Mode...");
        if (tcpServerPort > 0) tcpServer.begin();
      
        WiFi.mode(WIFI_STA);
        connectWiFi();
        sendResult(R_OK);
        //tcpServer(tcpServerPort); // can't start tcpServer inside a function-- must live outside
      
        digitalWrite(LED_PIN, LOW); // on
      
        webServer.on("/", handleRoot);
        webServer.on("/ath", handleWebHangUp);
        webServer.begin();
        mdns.begin("C64WiFi", WiFi.localIP());
        menuMode = MODE_MODEM;
      }
      else if (chr=='F'||chr=='f')
      {
        Serial.println("");
        Serial.println("Initializing SD card...");
        if (!SD.begin(chipSelect)) {
          Serial.println("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          return;
        }
        Serial.println("Initialization Complete.");
        menuMode = MODE_FILE;
        fileMenu();
      }
      else if (chr=='P'||chr=='p')
      {
        Serial.println("");
        Serial.println("Initializing SD card...");
        if (!SD.begin(chipSelect)) {
          Serial.println("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          return;
        }
        Serial.println("Initialization Complete.");
        menuMode = MODE_PLAYBACK;
        playbackMenu();
      }
      else
      {
        mainMenu();
      }
    }
}

void playbackMenu() {
  Serial.println();
  Serial.println();
  Serial.println(" -=- TEXT  PLAYBACK -=-");
  Serial.println(" [L]ist Files");
  Serial.println(" [D]isplay File");
  Serial.println(" [P]layback File");
  Serial.println(" [E]valuate Key");
  Serial.println(" [T]erminal Mode");
  Serial.println(" [M]ain Menu");
  Serial.println();
  Serial.print("> ");
}

void fileMenu() {
  Serial.println();
  Serial.println();
  Serial.println(" -=- FILE  TRANSFER -=-");
  Serial.println(" [U]pload File (to SD)");
  Serial.println(" [D]ownload File (from SD)");
  Serial.println(" [L]ist Files on SD");
  Serial.println(" [M]ain Menu");
  Serial.println();
  Serial.print("> ");
}

void fileLoop()
{
  if (Serial.available())
  {
    char chr = Serial.read();
    Serial.print(chr);
    if (chr=='M'||chr=='m') //main menu
    {
      menuMode = MODE_MAIN;
      mainMenu();
    }
    else if (chr=='U'||chr=='u') //upload file
    {
      uploadFile();
    }
    else if (chr=='D'||chr=='d') //download file
    {
      downloadFile();
    }
    else if (chr=='L'||chr=='l') //list files
    {
      listFiles();
    }
    else if (chr=='G'||chr=='g') //list files
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
    else
    {
      fileMenu();
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
}

void serialWrite(char c, String log)
{
  if (log!="")
    addLog("> [" + log + "] 0x" + String(c, HEX) + " " + String(c) + "\r\n");
  Serial.write(c);
}

void addLog(String logmsg)
{
  //logFile = SD.open("logfile.txt", FILE_WRITE);
  //logFile.println(logmsg);
  //logFile.flush();
  //logFile.close();
}

String chooseFile()
{
  Serial.println("");
  Serial.println("");
  Serial.print("Please enter the filename: ");
  String fileName = getLine();
  clearInputBuffer();
  return fileName;
}

void downloadFile()
{
  resetGlobals();
  String fileName=chooseFile();
  Serial.println("Downloading " + fileName);
  Serial.println("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //Serial.println("Couldn't open log file");
  File dataFile = SD.open(fileName);
    
  if (dataFile) { //make sure the file is valid
    String fileSize = getFileSize(fileName);
    downloadLoop(dataFile, fileName, fileSize);
    Serial.println("Download Complete");
    dataFile.close();
  } else {
    Serial.println("Invalid File");
  }
}

void downloadLoop(File dataFile, String fileName, String fileSize)
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
    logmsg = "P#" + String(packetNumber) + " b#" + String(i);
    
    if (packet[i] == 0xFF && EscapeTelnet)
      serialWrite(0xFF, "escaping FF");
      
    serialWrite(packet[i], logmsg);
  }
  Serial.flush();
}

void uploadFile()
{
  SD.remove("logfile.txt");

  //logFile = SD.open("logfile.txt", FILE_WRITE);
  //logFile.flush();
  //logFile.close();
  Serial.println("");
  Serial.println("");
  Serial.println("TRANSFER WILL BEGIN IN 20 SECONDS OR ON ANY KEY...");
  uploadLoop();
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
  if (packetNumber>0)
    return false;
  else
  {
    serialWrite(0x43, "Sending Start C from Timer"); //letter C
    readyToReceive = true;
    return true;
  }
}

void uploadLoop()
{
  LinkedList<char> buffer = LinkedList<char>();  
  bool lastByteIAC = false;
  resetGlobals();
  String fileName="";
  String fileSize="";
  unsigned int fileSizeNum=0;
  unsigned int bytesWritten=0;

  timer.every(20000, sendStartByte);

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
  char cmd = getKey();
  Serial.println(cmd);
  if (cmd == 'L' || cmd == 'l') {
    listFiles();
  } else if (cmd == 'T' || cmd == 't') {
    changeTerminalMode();
  } else if (cmd == 'E' || cmd == 'e') {
    evalKey();
  } else if (cmd == 'D' || cmd == 'd') {
    Serial.print("Enter Filename: ");
    String filename = getLine();
    if (filename!="") {
      if (displayFile(filename, false, 0))
        getKey();
      Serial.println("");
      playbackMenu();
    }
  } else if (cmd == 'P' || cmd == 'p') {
    Serial.print("Enter Filename: ");
    String filename = getLine();
    if (filename!="") {
      Serial.println("");
      Serial.print("Begin at Position: ");
      String pos = getLine();
      if (displayFile(filename, true, pos.toInt()))
        waitKey(27);
      Serial.println("");
      playbackMenu();
    }
  } else if (cmd == 'M' || cmd == 'm') {
    menuMode = MODE_MAIN;
    mainMenu();   
  } else {
    playbackMenu();
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
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          Serial.write(chr);
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          Serial.write(chr);
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
      Serial.readBytes(&txBuf[0], len);

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
        Serial.write(rxByte); yield(); Serial.flush(); yield();
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
    //if (tcpServerPort > 0) tcpServer.begin();
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
  if (millis() - ledTime > LED_TIME) digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // toggle LED state
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
        if (c==27)
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
      Serial.write(myFile.read());
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
  Serial.print("Press Key: ");
  char c = getKey();
  Serial.print("CODE=");
  Serial.println(c, DEC);
}

char getKey()
{
  while (true)
  {
    if (Serial.available() > 0)
      return Serial.read();
  }
}

char waitKey(int key)
{
  while (true)
  {
    if (Serial.available() > 0)
    {
      char c = Serial.read();
      if (c == key)
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

#include "settings.h"
#include <EEPROM.h>

// Pin definitions and addresses (from globals.h concepts)
#define VERSIONA 0
#define VERSIONB 1
#define VERSION_ADDRESS 0
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define SERVER_PORT_ADDRESS 113
#define AUTO_ANSWER_ADDRESS 115
#define TELNET_ADDRESS  116
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
#define USB_DEBUG_ADDRESS 792
#define LISTEN_PORT 23

// Global variables (defined in main.cpp)
extern String ssid, password, busyMsg;
extern byte serialSpeed, serialConfig;
extern bool echo, autoAnswer, telnet, verboseResults, petTranslate;
extern int tcpServerPort;
extern byte flowControl, pinPolarity, dispOrientation, defaultMode;
extern bool usbDebug;
extern String speedDials[10];
extern const int speedDialAddresses[];

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
  EEPROM.write(USB_DEBUG_ADDRESS, byte(usbDebug));

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
  usbDebug = EEPROM.read(USB_DEBUG_ADDRESS);

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
  EEPROM.write(USB_DEBUG_ADDRESS, 0x01);  // USB debug enabled by default

  // SLIP Gateway defaults (addresses 800-880)
  // Gateway IP: 192.168.7.1
  EEPROM.write(800, 192);
  EEPROM.write(801, 168);
  EEPROM.write(802, 7);
  EEPROM.write(803, 1);
  // Client IP: 192.168.7.2
  EEPROM.write(804, 192);
  EEPROM.write(805, 168);
  EEPROM.write(806, 7);
  EEPROM.write(807, 2);
  // Subnet: 255.255.255.0
  EEPROM.write(808, 255);
  EEPROM.write(809, 255);
  EEPROM.write(810, 255);
  EEPROM.write(811, 0);
  // DNS: 8.8.8.8
  EEPROM.write(812, 8);
  EEPROM.write(813, 8);
  EEPROM.write(814, 8);
  EEPROM.write(815, 8);
  // Clear port forward entries
  for (int i = 816; i < 880; i++) {
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
}

String getEEPROM(int startAddress, int len) {
  String myString;

  for (int i = startAddress; i < startAddress + len; i++) {
    if (EEPROM.read(i) == 0x00) {
      break;
    }
    myString += char(EEPROM.read(i));
  }
  return myString;
}

void setEEPROM(String inString, int startAddress, int maxLen) {
  for (unsigned int i = startAddress; i < inString.length() + startAddress; i++) {
    EEPROM.write(i, inString[i - startAddress]);
  }
  // null pad the remainder of the memory space
  for (unsigned int i = inString.length() + startAddress; i < maxLen + startAddress; i++) {
    EEPROM.write(i, 0x00);
  }
}

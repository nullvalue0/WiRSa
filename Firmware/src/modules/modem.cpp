// Modem Module
// Handles AT commands, TCP connections, and modem emulation

#include "Arduino.h"
#include "modem.h"
#include "globals.h"
#include "serial_io.h"
#include "network.h"
#include "settings.h"
#include "display_menu.h"
#include "file_transfer.h"
#include "playback.h"
#include "firmware.h"
#include "slip_mode.h"
#include "ppp_mode.h"
#include "wifi_setup.h"
#include "diagnostics.h"
#include "web_ui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <arduino-timer.h>
#include "SD.h"

// External global variables
extern String cmd;
extern bool cmdMode;
extern bool callConnected;
extern bool telnet;
extern bool verboseResults;
extern bool echo;
extern bool autoAnswer;
extern bool usbDebug;
extern int tcpServerPort;
extern unsigned long connectTime;
extern unsigned long lastRingMs;
extern String busyMsg;
extern WiFiClient tcpClient;
extern WiFiClient consoleClient;
extern bool consoleConnected;
extern WiFiServer tcpServer;
extern String speedDials[];
extern byte serialSpeed;
extern byte serialConfig;
extern String resultCodes[];
extern HardwareSerial PhysicalSerial;
extern int menuMode;
extern WebServer webServer;
extern bool msgFlag;
extern int dispCharCnt;
extern int dispLineCnt;
extern Adafruit_SSD1306 display;
extern byte dispOrientation;
extern byte defaultMode;
extern String ssid;
extern String password;
extern String build;
extern String fileName;
extern String files[];

enum resultCodes_t { R_OK_STAT, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
enum dispOrientation_t { D_NORMAL, D_FLIPPED }; // Normal or Flipped
enum flowControl_t { F_NONE, F_HARDWARE, F_SOFTWARE };
enum pinPolarity_t { P_INVERTED, P_NORMAL }; // Is LOW (0) or HIGH (1) active?

// Array sizes (since we can't use sizeof on extern arrays)
#define BAUDS_COUNT 9
#define BITS_COUNT 24

// Function to format connection time as HH:MM:SS
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

// Send an AT result code to the serial port
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

// Send a string message to the serial port
void sendString(String msg) {
  SerialPrint("\r\n");
  SerialPrint(msg);
  SerialPrint("\r\n");
}

// Note: clearInputBuffer() is defined in file_transfer.cpp

// Read a line of text from serial input
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

// Prompt user for input with optional default value
String prompt(String msg, String dflt)
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

// Additional external declarations for modem functions
extern byte ringCount;
extern unsigned long bytesSent;
extern unsigned long bytesRecv;
extern bool sentChanged;
extern bool recvChanged;
extern bool speedDialShown;
extern bool bHex;
extern int xferMode;
extern File logFile;

// Wait for space key press
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

// Wait for enter key press
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

// Display help information for AT commands
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
  SerialPrintLn("CONSOLE STATUS.: AT$CON?"); yield();
  SerialPrintLn("CONSOLE DROP...: AT$CONDROP"); yield();
  SerialPrintLn("QUERY MOST COMMANDS FOLLOWED BY '?'"); yield();
}

// Display current active settings
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

// Display stored EEPROM settings
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

// Store a speed dial entry
void storeSpeedDial(byte num, String location) {
  //if (num < 0 || num > 9) { return; }
  speedDials[num] = location;
  //SerialPrint("STORED "); SerialPrint(num); SerialPrint(": "); SerialPrintLn(location);
}

// Turn on the LED and store the time
void led_on()
{
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  ledTime = millis();
}

// Answer an incoming call
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

// Handle incoming TCP connection
void handleIncomingConnection() {
  // If we already have an active call, reject with busy message
  if (callConnected == 1) {
    ringCount = lastRingMs = 0;
    WiFiClient anotherClient = tcpServer.available();
    anotherClient.print(busyMsg);
    anotherClient.print("\r\n");
    anotherClient.print("CURRENT CALL LENGTH: ");
    anotherClient.print(connectTimeString());
    anotherClient.print("\r\n\r\n");
    anotherClient.flush();
    anotherClient.stop();
    return;
  }

  // Check if this should become a console connection
  // First telnet connection without a console becomes the management console
  if (!consoleConnected) {
    // Accept as console client - stays in command mode
    consoleClient = tcpServer.available();
    consoleClient.setNoDelay(true);
    consoleConnected = true;

    // Send welcome message
    consoleClient.print("\r\n");
    consoleClient.print("=================================\r\n");
    consoleClient.print("   WiRSa Telnet Console " + build + "\r\n");
    consoleClient.print("=================================\r\n");
    consoleClient.print("Type AT commands or ATDT to dial\r\n");
    consoleClient.print("\r\n");

    // Basic telnet negotiation - tell client we'll handle echo
    uint8_t telnetInit[] = {
      0xFF, 0xFB, 0x01,  // IAC WILL ECHO
      0xFF, 0xFB, 0x03,  // IAC WILL SUPPRESS-GO-AHEAD
      0xFF, 0xFD, 0x03   // IAC DO SUPPRESS-GO-AHEAD
    };
    consoleClient.write(telnetInit, sizeof(telnetInit));

    SerialPrintLn("CONSOLE CONNECTED FROM " + ipToString(consoleClient.remoteIP()));
    return;
  }

  // Console already connected - handle as traditional call connection
  // (ring/answer behavior for secondary connections)
  if (autoAnswer == false && ringCount > 3) {
    // Didn't answer after three rings - reject
    ringCount = lastRingMs = 0;
    WiFiClient anotherClient = tcpServer.available();
    anotherClient.print("BUSY - Console in use\r\n");
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

  // Auto-answer secondary connections as calls
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

// Check and handle console disconnection
void checkConsoleConnection() {
  if (consoleConnected && !consoleClient.connected()) {
    consoleConnected = false;
    Serial.println("CONSOLE DISCONNECTED");
    PhysicalSerial.println("CONSOLE DISCONNECTED");
  }
}

// Hang up current connection
void hangUp() {
  tcpClient.stop();
  callConnected = false;
  setCarrier(callConnected);
  sendResult(R_NOCARRIER);
  connectTime = 0;
}

// Note: Web handlers moved to webserver.cpp module

// Dial out to a host
void dialOut(String upCmd) {
  // Can't place a call while in a call
  if (callConnected) {
    sendResult(R_ERROR);
    return;
  }

  // Extract the dial string (after ATDT, ATDP, or ATDI)
  String dialStr = upCmd.substring(4);
  dialStr.trim();
  dialStr.toUpperCase();

  // Check for special dial strings for SLIP mode
  // Dial "SLIP", "7547" (phone keypad), or "*75"
  if (dialStr == "SLIP" || dialStr == "7547" || dialStr == "*75" ||
      dialStr == "*SLIP" || dialStr == "S") {
    // Check WiFi first
    if (WiFi.status() != WL_CONNECTED) {
      SerialPrintLn("\r\nWiFi not connected");
      sendResult(R_NOCARRIER);
      return;
    }
    // Send CONNECT and enter SLIP mode
    SerialPrintLn("\r\nCONNECT SLIP");
    SerialPrint("Gateway: "); SerialPrintLn(ipToString(IPAddress(192,168,7,1)));
    SerialPrint("Client:  "); SerialPrintLn(ipToString(IPAddress(192,168,7,2)));
    SerialPrintLn("");
    delay(100);
    enterSlipMode();
    return;
  }

  // Check for special dial strings for PPP mode
  // Dial "PPP", "777", or "*77"
  if (dialStr == "PPP" || dialStr == "777" || dialStr == "*77" ||
      dialStr == "*PPP" || dialStr == "P") {
    // Check WiFi first
    if (WiFi.status() != WL_CONNECTED) {
      SerialPrintLn("\r\nWiFi not connected");
      sendResult(R_NOCARRIER);
      return;
    }
    // Send CONNECT and enter PPP mode
    SerialPrintLn("\r\nCONNECT PPP");
    delay(100);
    enterPppMode();
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

// Process AT commands
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

  /**** Console status ****/
  else if (upCmd == "AT$CON?") {
    if (consoleConnected) {
      sendString("CONSOLE CONNECTED FROM " + ipToString(consoleClient.remoteIP()));
    } else {
      sendString("CONSOLE NOT CONNECTED");
    }
    sendResult(R_OK_STAT);
  }

  /**** Disconnect console client ****/
  else if (upCmd == "AT$CONDROP") {
    if (consoleConnected) {
      consoleClient.print("\r\nCONSOLE DISCONNECTED BY HOST\r\n");
      consoleClient.flush();
      consoleClient.stop();
      consoleConnected = false;
      sendString("CONSOLE DISCONNECTED");
    } else {
      sendString("NO CONSOLE CONNECTED");
    }
    sendResult(R_OK_STAT);
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP?")
  {
    SerialPrintLn(ipToString(WiFi.localIP()));
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

  /**** SLIP Gateway Commands ****/
  else if (handleSlipCommand(cmd, upCmd)) {
    // Command was handled by SLIP module
    sendResult(R_OK_STAT);
  }

  /**** PPP Gateway Commands ****/
  else if (handlePppCommand(cmd, upCmd)) {
    // Command was handled by PPP module
    sendResult(R_OK_STAT);
  }

  /**** Diagnostics Commands ****/
  else if (handleDiagnosticsCommand(cmd, upCmd)) {
    // Command was handled by Diagnostics module
    sendResult(R_OK_STAT);
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

// Enter modem mode
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

    // Check if WiFi is already connected - don't reconnect if so
    if (WiFi.status() == WL_CONNECTED) {
        SerialPrintLn("WiFi already connected to " + WiFi.SSID());
        SerialPrint("IP: "); SerialPrintLn(ipToString(WiFi.localIP()));
        if (consoleConnected) {
            SerialPrintLn("Console session preserved");
        }
    } else {
        WiFi.mode(WIFI_STA);
        connectWiFi();
    }
    sendResult(R_OK_STAT);

    digitalWrite(LED_PIN, LOW); // on

    // Setup web server routes (SPA with API endpoints)
    setupWebServer();
    webServer.begin();

    mdns.begin("WiRSa"); // Set the network hostname to "wirsa.local"
}

// Check boot button for baud rate reset
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

// Main menu loop
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
        //if (!SD.begin(SD_CS)) {
        if (!SD.begin()) {
          SerialPrintLn("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        SerialPrintLn("Initialization Complete.");
        fileMenu(false);
      }
      else if (chr=='T'||chr=='t'||menuSel==2)
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        SerialPrintLn("");
        SerialPrintLn("Initializing SD card...");
        //if (!SD.begin(SD_CS)) {
        if (!SD.begin()) {
          SerialPrintLn("Initialization failed! Please check that SD card is inserted and formatted as FAT16 or FAT32.");
          showMessage("\nPLEASE INSERT\n   SD CARD");
          return;
        }
        SerialPrintLn("Initialization Complete.");
        playbackMenu(false);
      }
      else if (chr=='P'||chr=='p'||menuSel==3) // PPP Gateway
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        pppMenu(false);
      }
      else if (chr=='U'||chr=='u'||menuSel==4) // Utilities
      {
        SerialPrint(chr); //echo it back if it was a valid entry
        diagnosticsInit();
        diagnosticsMenu(false);
      }
      else if (chr=='C'||chr=='c'||menuSel==5)
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

// Baud rate selection loop
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
    if (chr>=65 && chr <= 65+BAUDS_COUNT-1 || chr>=97 && chr <= 97+BAUDS_COUNT-1)
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

// Serial config loop
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
    if (chr>=65 && chr <= 65+BITS_COUNT-1 || chr>=97 && chr <= 97+BITS_COUNT-1)
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

// Orientation selection loop
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

// Default mode selection loop
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

// Settings menu loop
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
    else if (chr=='W'||chr=='w'||menuSel==1) // WiFi Setup
    {
      wifiSetupInit();
      wifiSetupLoop();
    }
    else if (chr=='B'||chr=='b'||menuSel==2)
    {
      baudMenu(false);
    }
    else if (chr=='S'||chr=='s'||menuSel==3)
    {
      serialMenu(false);
    }
    else if (chr=='O'||chr=='o'||menuSel==4)
    {
      orientationMenu(false);
    }
    else if (chr=='D'||chr=='d'||menuSel==5)
    {
      defaultModeMenu(false);
    }
    else if (chr=='U'||chr=='u'||menuSel==6)
    {
      usbDebug = !usbDebug;
      writeSettings();
      SerialPrintLn(usbDebug ? "USB Debug: ENABLED" : "USB Debug: DISABLED");
      showMessage(usbDebug ? "USB Debug\n\nENABLED" : "USB Debug\n\nDISABLED");
      delay(1500);  // Pause so the message can be read on the display
      settingsMenu(false);
    }
    else if (chr=='F'||chr=='f'||menuSel==7)
    {
       SerialPrintLn("** PLEASE WAIT: FACTORY RESET **");
       showMessage("***************\n* PLEASE WAIT *\n*FACTORY RESET*\n***************");
       defaultEEPROM();
       ESP.restart();
    }
    else if (chr=='R'||chr=='r'||menuSel==8)
    {
      SerialPrintLn("** PLEASE WAIT: REBOOTING **");
      showMessage("***************\n* PLEASE WAIT *\n*  REBOOTING  *\n***************");
      ESP.restart();
    }
    else if (chr=='A'||chr=='a'||menuSel==9)
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

// File menu loop
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
      logFile = SD.open("/logfile.txt");
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

// Protocol selection loop
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
     if (xferMode == XFER_SEND)
       sendFileZMODEM();
     else if (xferMode == XFER_RECV)
       receiveFileZMODEM();
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

// List files menu loop
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

// Main modem loop - handles AT commands and TCP communication
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

  // Check if console client disconnected
  checkConsoleConnection();

  // Handle telnet IAC sequences from console in command mode
  if (consoleConnected && consoleClient.available() && cmdMode) {
    // Peek at the byte - if it's IAC (0xFF), handle telnet protocol
    int peek = consoleClient.peek();
    if (peek == 0xFF) {
      // Read and handle IAC sequence
      consoleClient.read();  // consume IAC
      if (consoleClient.available()) {
        uint8_t cmd = consoleClient.read();
        if (cmd == 0xFF) {
          // Escaped 0xFF - put it back for normal processing (can't really, so skip)
        } else if (cmd == 0xFB || cmd == 0xFC || cmd == 0xFD || cmd == 0xFE) {
          // WILL/WONT/DO/DONT - read option byte and respond
          if (consoleClient.available()) {
            uint8_t opt = consoleClient.read();
            if (cmd == 0xFD) {  // DO - respond WONT
              uint8_t resp[] = { 0xFF, 0xFC, opt };
              consoleClient.write(resp, 3);
            } else if (cmd == 0xFB) {  // WILL - respond DO
              uint8_t resp[] = { 0xFF, 0xFD, opt };
              consoleClient.write(resp, 3);
            }
          }
        }
      }
    }
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

      // Read from console client (telnet), filtering out IAC sequences
      if (consoleConnected && consoleClient.available()) {
        size_t consoleLen = 0;
        while (consoleClient.available() && consoleLen < (size_t)max_buf_size) {
          int peek = consoleClient.peek();
          if (peek == 0xFF) {
            // Handle telnet IAC sequence - don't send to remote
            consoleClient.read();  // consume IAC
            if (consoleClient.available()) {
              uint8_t iacCmd = consoleClient.read();
              if (iacCmd == 0xFF) {
                // Escaped 0xFF - this is actual data
                txBuf[consoleLen++] = 0xFF;
              } else if (iacCmd == 0xFB || iacCmd == 0xFC || iacCmd == 0xFD || iacCmd == 0xFE) {
                // WILL/WONT/DO/DONT - read and discard option byte
                if (consoleClient.available()) consoleClient.read();
              }
              // Other IAC commands are just discarded
            }
          } else {
            txBuf[consoleLen++] = consoleClient.read();
          }
        }
        if (consoleLen > 0) {
          len = consoleLen;
          // Enter command mode with "+++" sequence from console too
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

// Refresh display timer callback
bool refreshDisplay(void *)
{
  static uint8_t wifiUpdateCounter = 0;

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

  // Update WiFi signal strength icon every ~2 seconds (8 x 250ms)
  wifiUpdateCounter++;
  if (wifiUpdateCounter >= 8) {
    wifiUpdateCounter = 0;
    showWifiIcon();  // This will update the signal bars based on current RSSI
  }

  if (sentChanged||recvChanged) {
    sentChanged=false;
    recvChanged=false;
    display.display();
  }

  return true;
}

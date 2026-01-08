#include "network.h"
#include "serial_io.h"
#include <Adafruit_SSD1306.h>

// Pin definitions
#define LED_PIN 2
#define DCD_PIN 33
#define RTS_PIN 15
#define CTS_PIN 27
#define RXD2 16
#define TXD2 17

// Result codes (avoid R_OK conflict with system header)
#define R_MODEM_OK 0
#define R_ERROR 4
#define R_OK_STAT 200

// Pin polarity
enum pinPolarity_t { P_INVERTED, P_NORMAL };

// Forward declarations for functions still in main.cpp
extern void showMessage(String message);
extern void modemMenuMsg();
extern void modemConnected();
extern void firmwareCheck();
extern void sendResult(int resultCode);
extern String connectTimeString();

// WiFi symbols for display
extern const uint8_t PROGMEM wifi_symbol[];
extern const uint8_t PROGMEM phon_symbol[];

// Global variables (defined in main.cpp)
extern Adafruit_SSD1306 display;
extern WiFiServer tcpServer;
extern int tcpServerPort;
extern String ssid, password;
extern byte serialSpeed, serialConfig;
extern const int bauds[];
extern const int bits[];
extern HardwareSerial PhysicalSerial;
extern byte pinPolarity;
extern bool callConnected;
extern WiFiClient tcpClient;

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
    SerialPrint("IP ADDRESS: "); SerialPrintLn(ipToString(WiFi.localIP()));
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
  // bauds array has 9 elements: 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200
  for (unsigned int i = 0; i < 9; i++) {
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

  SerialPrint("IP ADDRESS.: "); SerialPrintLn(ipToString(WiFi.localIP())); yield();
  SerialPrint("GATEWAY....: "); SerialPrintLn(ipToString(WiFi.gatewayIP())); yield();
  SerialPrint("SUBNET MASK: "); SerialPrintLn(ipToString(WiFi.subnetMask())); yield();
  SerialPrint("SERVER PORT: "); SerialPrintLn(tcpServerPort); yield();
  SerialPrint("WEB CONFIG.: HTTP://"); SerialPrintLn(ipToString(WiFi.localIP())); yield();
  SerialPrint("CALL STATUS: "); yield();
  if (callConnected) {
    SerialPrint("CONNECTED TO "); SerialPrintLn(ipToString(tcpClient.remoteIP())); yield();
    SerialPrint("CALL LENGTH: "); SerialPrintLn(connectTimeString()); yield();
  } else {
  }
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

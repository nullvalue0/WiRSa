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
extern WiFiClient consoleClient;
extern bool consoleConnected;

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
      SerialPrint("TELNET LISTENING ON PORT "); SerialPrintLn(String(tcpServerPort));
      tcpServer.begin();
    }
    SerialPrintLn("HTTP CONFIG ON PORT 80 (http://wirsa.local)");
    modemConnected();
    showWifiIcon();
    firmwareCheck();
  }
}

// Get signal strength level (0-4) from RSSI
// Returns: 0 = no signal, 1 = weak, 2 = fair, 3 = good, 4 = excellent
int getSignalBars() {
  if (WiFi.status() != WL_CONNECTED) return 0;

  int rssi = WiFi.RSSI();
  if (rssi > -50) return 4;       // Excellent
  else if (rssi > -60) return 3;  // Good
  else if (rssi > -70) return 2;  // Fair
  else if (rssi > -80) return 1;  // Weak
  else return 0;                   // Very weak / no signal
}

// Draw a dithered (gray) bar by alternating pixels in a checkerboard pattern
void drawDitheredBar(int x, int y, int w, int h) {
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      // Checkerboard pattern: draw pixel if (px + py) is even
      if ((px + py) % 2 == 0) {
        display.drawPixel(px, py, SSD1306_BLACK);
      }
    }
  }
}

void showWifiIcon() {
  // Icon position and size (in header area which has white background)
  const int iconX = 72;
  const int iconY = 4;
  const int iconW = 11;
  const int iconH = 8;

  // Clear the icon area first (fill with white since header is white)
  display.fillRect(iconX, iconY, iconW, iconH, SSD1306_WHITE);

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);  // LED on

    int bars = getSignalBars();

    // Draw signal bars (drawing in BLACK on white header background)
    // 4 bars, each 2 pixels wide with 1 pixel gap
    // Heights: bar1=2px, bar2=4px, bar3=6px, bar4=8px (from bottom at iconY+iconH)

    int barWidth = 2;
    int gap = 1;
    int baseY = iconY + iconH;  // Bottom of icon area

    // Bar 1 (shortest, leftmost) - height 2
    if (bars >= 1) {
      display.fillRect(iconX, baseY - 2, barWidth, 2, SSD1306_BLACK);
    } else {
      drawDitheredBar(iconX, baseY - 2, barWidth, 2);
    }

    // Bar 2 - height 4
    if (bars >= 2) {
      display.fillRect(iconX + barWidth + gap, baseY - 4, barWidth, 4, SSD1306_BLACK);
    } else {
      drawDitheredBar(iconX + barWidth + gap, baseY - 4, barWidth, 4);
    }

    // Bar 3 - height 6
    if (bars >= 3) {
      display.fillRect(iconX + 2*(barWidth + gap), baseY - 6, barWidth, 6, SSD1306_BLACK);
    } else {
      drawDitheredBar(iconX + 2*(barWidth + gap), baseY - 6, barWidth, 6);
    }

    // Bar 4 (tallest, rightmost) - height 8
    if (bars >= 4) {
      display.fillRect(iconX + 3*(barWidth + gap), baseY - 8, barWidth, 8, SSD1306_BLACK);
    } else {
      drawDitheredBar(iconX + 3*(barWidth + gap), baseY - 8, barWidth, 8);
    }

  } else {
    digitalWrite(LED_PIN, HIGH); // LED off
    // Draw X for no connection
    display.drawLine(iconX + 1, iconY + 1, iconX + iconW - 2, iconY + iconH - 2, SSD1306_BLACK);
    display.drawLine(iconX + iconW - 2, iconY + 1, iconX + 1, iconY + iconH - 2, SSD1306_BLACK);
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
    SerialPrintLn("NOT CONNECTED"); yield();
  }
  SerialPrint("CONSOLE....: "); yield();
  if (consoleConnected) {
    SerialPrint("CONNECTED FROM "); SerialPrintLn(ipToString(consoleClient.remoteIP())); yield();
  } else {
    SerialPrintLn("NOT CONNECTED"); yield();
  }
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

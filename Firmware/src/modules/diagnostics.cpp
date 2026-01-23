// Diagnostics Module
// Serial diagnostics and troubleshooting tools for WiRSa

#include "Arduino.h"
#include "diagnostics.h"
#include "globals.h"
#include "display_menu.h"
#include "serial_io.h"
#include <WiFi.h>
#include <Adafruit_SSD1306.h>

// External globals
extern Adafruit_SSD1306 display;
extern int menuMode;
extern int menuIdx;
extern int menuCnt;
extern int lastMenu;
extern bool msgFlag;
extern byte serialSpeed;
extern byte serialConfig;
extern String baudDisp[];
extern const int bauds[];
extern const int bits[];
extern HardwareSerial PhysicalSerial;
extern String build;
extern long bytesSent;
extern long bytesRecv;

// Button state
extern bool BTNUP;
extern bool BTNDN;
extern bool BTNEN;
extern bool BTNBK;

// Menu display array (defined in main.cpp)
extern String diagnosticsMenuDisp[];

// Diagnostics context
DiagnosticsContext diagCtx;

void diagnosticsInit() {
  diagCtx.hexDumpEnabled = false;
  diagCtx.signalMonitorActive = false;
  diagCtx.loopbackSent = 0;
  diagCtx.loopbackRecv = 0;
  diagCtx.loopbackErrors = 0;
  diagCtx.connectionCount = 0;
  diagCtx.totalBytesSent = 0;
  diagCtx.totalBytesRecv = 0;
}

// ============================================================================
// Signal Monitor
// ============================================================================

void showSignalStates() {
  // Read pin states
  int dcd = digitalRead(DCD_PIN);
  int rts = digitalRead(RTS_PIN);
  int cts = digitalRead(CTS_PIN);
  int dtr = digitalRead(DTR_PIN);
  int dsr = digitalRead(DSR_PIN);
  int ri  = digitalRead(RI_PIN);

  SerialPrintLn("\r\n===== Signal States =====");
  SerialPrintLn("DCD (GPIO33): " + String(dcd ? "HIGH" : "LOW"));
  SerialPrintLn("RTS (GPIO15): " + String(rts ? "HIGH" : "LOW"));
  SerialPrintLn("CTS (GPIO27): " + String(cts ? "HIGH" : "LOW"));
  SerialPrintLn("DTR (GPIO4):  " + String(dtr ? "HIGH" : "LOW"));
  SerialPrintLn("DSR (GPIO26): " + String(dsr ? "HIGH" : "LOW"));
  SerialPrintLn("RI  (GPIO25): " + String(ri  ? "HIGH" : "LOW"));
  SerialPrintLn("=========================");
}

void signalMonitorDisplay() {
  // Read pin states
  int dcd = digitalRead(DCD_PIN);
  int rts = digitalRead(RTS_PIN);
  int cts = digitalRead(CTS_PIN);
  int dtr = digitalRead(DTR_PIN);
  int dsr = digitalRead(DSR_PIN);
  int ri  = digitalRead(RI_PIN);

  // Update OLED
  display.fillRect(1, 17, 125, 46, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(3, 19);
  display.print("DCD:");
  display.print(dcd ? "HI" : "LO");
  display.setCursor(64, 19);
  display.print("RTS:");
  display.print(rts ? "HI" : "LO");

  display.setCursor(3, 29);
  display.print("CTS:");
  display.print(cts ? "HI" : "LO");
  display.setCursor(64, 29);
  display.print("DTR:");
  display.print(dtr ? "HI" : "LO");

  display.setCursor(3, 39);
  display.print("DSR:");
  display.print(dsr ? "HI" : "LO");
  display.setCursor(64, 39);
  display.print("RI :");
  display.print(ri ? "HI" : "LO");

  display.drawLine(1, 50, 126, 50, SSD1306_WHITE);
  display.setCursor(3, 53);
  display.print("BACK to exit");

  display.display();
}

void signalMonitor() {
  diagCtx.signalMonitorActive = true;

  SerialPrintLn("\r\n===== Signal Monitor =====");
  SerialPrintLn("Press BACK or ESC to exit");
  SerialPrintLn("--------------------------");

  // Show header
  showHeader("SIGNALS", MENU_DISP);

  unsigned long lastUpdate = 0;
  int lastDcd = -1, lastRts = -1, lastCts = -1;
  int lastDtr = -1, lastDsr = -1, lastRi = -1;

  while (diagCtx.signalMonitorActive) {
    readSwitches();

    // Check for exit
    if (BTNBK) {
      waitSwitches();
      break;
    }

    // Check for ESC on serial
    if (SerialAvailable() > 0) {
      char c = SerialRead();
      if (c == 0x1B) { // ESC
        break;
      }
    }

    // Update display every 100ms
    if (millis() - lastUpdate > 100) {
      lastUpdate = millis();

      int dcd = digitalRead(DCD_PIN);
      int rts = digitalRead(RTS_PIN);
      int cts = digitalRead(CTS_PIN);
      int dtr = digitalRead(DTR_PIN);
      int dsr = digitalRead(DSR_PIN);
      int ri  = digitalRead(RI_PIN);

      // Update serial output only on change
      if (dcd != lastDcd || rts != lastRts || cts != lastCts ||
          dtr != lastDtr || dsr != lastDsr || ri != lastRi) {
        SerialPrint("\rDCD:");
        SerialPrint(dcd ? "HI" : "LO");
        SerialPrint(" RTS:");
        SerialPrint(rts ? "HI" : "LO");
        SerialPrint(" CTS:");
        SerialPrint(cts ? "HI" : "LO");
        SerialPrint(" DTR:");
        SerialPrint(dtr ? "HI" : "LO");
        SerialPrint(" DSR:");
        SerialPrint(dsr ? "HI" : "LO");
        SerialPrint(" RI:");
        SerialPrint(ri ? "HI" : "LO");
        SerialPrint("   ");

        lastDcd = dcd;
        lastRts = rts;
        lastCts = cts;
        lastDtr = dtr;
        lastDsr = dsr;
        lastRi = ri;
      }

      // Always update OLED
      signalMonitorDisplay();
    }

    delay(10);
  }

  diagCtx.signalMonitorActive = false;
  SerialPrintLn("\r\n--------------------------");
  SerialPrintLn("Signal monitor stopped.");
}

// ============================================================================
// Loopback Test
// ============================================================================

void showLoopbackInstructions() {
  SerialPrintLn("\r\n============================================");
  SerialPrintLn("           SERIAL LOOPBACK TEST");
  SerialPrintLn("============================================");
  SerialPrintLn("");
  SerialPrintLn("PURPOSE:");
  SerialPrintLn("  Tests your serial cable and RS232 interface");
  SerialPrintLn("  by sending data out TX and receiving it on RX.");
  SerialPrintLn("  If data returns correctly, your cable and");
  SerialPrintLn("  serial port are working properly.");
  SerialPrintLn("");
  SerialPrintLn("HOW TO CREATE A LOOPBACK:");
  SerialPrintLn("");
  SerialPrintLn("  DE-9 (9-pin) Connector:");
  SerialPrintLn("  +-----------------------+");
  SerialPrintLn("  | Connect Pin 2 to Pin 3 |");
  SerialPrintLn("  |    (RX to TX)          |");
  SerialPrintLn("  +-----------------------+");
  SerialPrintLn("  Use a jumper wire or paperclip between");
  SerialPrintLn("  pins 2 and 3 on your DE-9 connector.");
  SerialPrintLn("");
  SerialPrintLn("  DB-25 (25-pin) Connector:");
  SerialPrintLn("  +-----------------------+");
  SerialPrintLn("  | Connect Pin 2 to Pin 3 |");
  SerialPrintLn("  |    (TX to RX)          |");
  SerialPrintLn("  +-----------------------+");
  SerialPrintLn("  Use a jumper wire between pins 2 and 3");
  SerialPrintLn("  on your DB-25 connector.");
  SerialPrintLn("");
  SerialPrintLn("  NOTE: The pin labels may be swapped");
  SerialPrintLn("  depending on DTE/DCE configuration.");
  SerialPrintLn("  If test fails, try swapping the pins.");
  SerialPrintLn("");
  SerialPrintLn("--------------------------------------------");
}

void loopbackTest() {
  // Show detailed instructions on serial port
  showLoopbackInstructions();

  SerialPrintLn("Press ENTER or any key when loopback is connected...");

  // OLED display - brief instructions
  showHeader("LOOPBACK", MENU_DISP);
  display.fillRect(1, 17, 125, 46, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(3, 19);
  display.print("Connect TX-RX:");
  display.setCursor(3, 29);
  display.print("DE9: Pin 2-3");
  display.setCursor(3, 39);
  display.print("DB25: Pin 2-3");
  display.setCursor(3, 53);
  display.print("ENTER to start");
  display.display();

  // Wait for user to connect loopback and press a key/button
  while (true) {
    readSwitches();
    if (BTNEN || BTNBK) {
      waitSwitches();
      if (BTNBK) {
        // User cancelled
        SerialPrintLn("\r\nTest cancelled.");
        return;
      }
      break;
    }
    if (SerialAvailable() > 0) {
      char c = SerialRead();
      if (c == 0x1B) { // ESC to cancel
        SerialPrintLn("\r\nTest cancelled.");
        return;
      }
      break; // Any other key starts the test
    }
    delay(50);
  }

  // Clear any remaining serial input before test
  while (SerialAvailable()) SerialRead();

  SerialPrintLn("");
  SerialPrintLn("Testing at " + String(bauds[serialSpeed]) + " baud...");
  SerialPrintLn("Sending 256-byte test pattern (0x00-0xFF)...");
  SerialPrintLn("");

  // Update OLED to show testing
  display.fillRect(3, 53, 120, 10, SSD1306_BLACK);
  display.setCursor(3, 53);
  display.print("Testing...");
  display.display();

  diagCtx.loopbackSent = 0;
  diagCtx.loopbackRecv = 0;
  diagCtx.loopbackErrors = 0;

  // Flush any pending data
  while (PhysicalSerial.available()) {
    PhysicalSerial.read();
  }
  delay(100);

  // Send test pattern (0x00-0xFF)
  for (int i = 0; i < 256; i++) {
    uint8_t testByte = (uint8_t)i;
    PhysicalSerial.write(testByte);
    diagCtx.loopbackSent++;

    // Wait for response with timeout
    unsigned long start = millis();
    while (!PhysicalSerial.available() && (millis() - start < 100)) {
      delay(1);
    }

    if (PhysicalSerial.available()) {
      uint8_t recvByte = PhysicalSerial.read();
      diagCtx.loopbackRecv++;
      if (recvByte != testByte) {
        diagCtx.loopbackErrors++;
      }
    }
  }

  // Wait a bit more for any delayed bytes
  delay(200);
  while (PhysicalSerial.available()) {
    PhysicalSerial.read();
    diagCtx.loopbackRecv++;
  }

  // Display results
  SerialPrintLn("============================================");
  SerialPrintLn("             TEST RESULTS");
  SerialPrintLn("============================================");
  SerialPrintLn("  Bytes Sent:     " + String(diagCtx.loopbackSent));
  SerialPrintLn("  Bytes Received: " + String(diagCtx.loopbackRecv));
  SerialPrintLn("  Data Errors:    " + String(diagCtx.loopbackErrors));
  SerialPrintLn("--------------------------------------------");

  bool passed = (diagCtx.loopbackRecv == 256 && diagCtx.loopbackErrors == 0);

  if (passed) {
    SerialPrintLn("           *** TEST PASSED ***");
    SerialPrintLn("");
    SerialPrintLn("  Your serial connection is working");
    SerialPrintLn("  correctly at " + String(bauds[serialSpeed]) + " baud.");

    // Update OLED with results
    display.fillRect(1, 17, 125, 46, SSD1306_BLACK);
    display.setCursor(3, 19);
    display.print("Sent:  256 bytes");
    display.setCursor(3, 29);
    display.print("Recv:  256 bytes");
    display.setCursor(3, 39);
    display.print("Errors: 0");
    display.setCursor(20, 53);
    display.print("** PASSED **");
    display.display();
  } else {
    SerialPrintLn("           *** TEST FAILED ***");
    SerialPrintLn("");
    if (diagCtx.loopbackRecv == 0) {
      SerialPrintLn("  No data received! Check:");
      SerialPrintLn("  - Loopback jumper is connected");
      SerialPrintLn("  - Correct pins (try swapping 2/3)");
      SerialPrintLn("  - Cable is good");
    } else if (diagCtx.loopbackErrors > 0) {
      SerialPrintLn("  Data corruption detected! Check:");
      SerialPrintLn("  - Baud rate matches remote device");
      SerialPrintLn("  - Cable quality/shielding");
      SerialPrintLn("  - Try lower baud rate");
    } else {
      SerialPrintLn("  Incomplete transfer! Check:");
      SerialPrintLn("  - Cable connections");
      SerialPrintLn("  - Flow control settings");
    }

    // Update OLED with failure
    display.fillRect(1, 17, 125, 46, SSD1306_BLACK);
    display.setCursor(3, 19);
    display.print("Sent:  " + String(diagCtx.loopbackSent));
    display.setCursor(3, 29);
    display.print("Recv:  " + String(diagCtx.loopbackRecv));
    display.setCursor(3, 39);
    display.print("Errors: " + String(diagCtx.loopbackErrors));
    display.setCursor(20, 53);
    display.print("** FAILED **");
    display.display();
  }

  SerialPrintLn("============================================");
  SerialPrintLn("");
  SerialPrintLn("Press ESC or BACK button to continue...");

  // Wait for ESC key or BACK button
  while (true) {
    readSwitches();
    if (BTNBK) {
      waitSwitches();
      break;
    }
    if (SerialAvailable() > 0) {
      char c = SerialRead();
      if (c == 0x1B) { // ESC key
        break;
      }
    }
    delay(50);
  }

  // Clear any remaining serial input
  while (SerialAvailable()) SerialRead();
}

// ============================================================================
// Hex Dump Mode
// ============================================================================

void setHexDumpMode(bool enabled) {
  diagCtx.hexDumpEnabled = enabled;
  if (enabled) {
    SerialPrintLn("\r\nHex dump mode ENABLED");
    SerialPrintLn("All serial data will be shown in hex format.");
  } else {
    SerialPrintLn("\r\nHex dump mode DISABLED");
  }
}

bool isHexDumpEnabled() {
  return diagCtx.hexDumpEnabled;
}

void hexDumpByte(char direction, uint8_t byte) {
  if (!diagCtx.hexDumpEnabled) return;

  // Format: [T] 41 'A'  or  [R] 0D <CR>
  Serial.print("[");
  Serial.print(direction);
  Serial.print("] ");

  if (byte < 0x10) Serial.print("0");
  Serial.print(byte, HEX);
  Serial.print(" ");

  // Show ASCII or control char name
  if (byte >= 0x20 && byte < 0x7F) {
    Serial.print("'");
    Serial.print((char)byte);
    Serial.println("'");
  } else if (byte == 0x0D) {
    Serial.println("<CR>");
  } else if (byte == 0x0A) {
    Serial.println("<LF>");
  } else if (byte == 0x1B) {
    Serial.println("<ESC>");
  } else if (byte == 0x08) {
    Serial.println("<BS>");
  } else if (byte == 0x09) {
    Serial.println("<TAB>");
  } else if (byte == 0x00) {
    Serial.println("<NUL>");
  } else if (byte == 0x7F) {
    Serial.println("<DEL>");
  } else {
    Serial.print("<0x");
    if (byte < 0x10) Serial.print("0");
    Serial.print(byte, HEX);
    Serial.println(">");
  }
}

// ============================================================================
// Baud Rate Auto-Detect
// ============================================================================

void baudAutoDetect() {
  SerialPrintLn("\r\n===== Baud Rate Auto-Detect =====");
  SerialPrintLn("Press a key on the connected device...");

  showMessage("Baud Detect\n\nPress key on\nconnected\ndevice...");

  // Save current baud rate
  byte originalSpeed = serialSpeed;

  // Array of baud rates to try (most common first)
  int testBauds[] = {9600, 115200, 57600, 38400, 19200, 4800, 2400, 1200, 300};
  int numBauds = 9;

  bool detected = false;
  int detectedBaud = 0;

  for (int i = 0; i < numBauds && !detected; i++) {
    // Find index in bauds array
    int baudIdx = -1;
    for (int j = 0; j < 9; j++) {
      if (bauds[j] == testBauds[i]) {
        baudIdx = j;
        break;
      }
    }
    if (baudIdx < 0) continue;

    SerialPrint("\rTesting ");
    SerialPrint(String(testBauds[i]));
    SerialPrint(" baud...   ");

    // Update OLED
    showMessage("Baud Detect\n\nTesting:\n" + String(testBauds[i]));

    // Switch baud rate
    PhysicalSerial.end();
    delay(50);
    PhysicalSerial.begin(testBauds[i], (SerialConfig)bits[serialConfig], RXD2, TXD2);
    delay(100);

    // Flush buffer
    while (PhysicalSerial.available()) {
      PhysicalSerial.read();
    }

    // Wait for data with timeout
    unsigned long start = millis();
    int validCount = 0;
    int invalidCount = 0;

    while (millis() - start < 2000) {  // 2 second timeout per baud rate
      if (PhysicalSerial.available()) {
        uint8_t c = PhysicalSerial.read();

        // Check if it's a valid ASCII character
        if ((c >= 0x20 && c < 0x7F) || c == 0x0D || c == 0x0A || c == 0x08 || c == 0x09) {
          validCount++;
        } else if (c != 0x00 && c != 0xFF) {
          // Garbage character (not null or all-ones which could be line noise)
          invalidCount++;
        }

        // If we get 3+ valid chars with no garbage, we found it
        if (validCount >= 3 && invalidCount == 0) {
          detected = true;
          detectedBaud = testBauds[i];
          serialSpeed = baudIdx;
          break;
        }

        // If too much garbage, try next baud rate
        if (invalidCount > 5) {
          break;
        }
      }

      // Check for abort (BACK button or ESC)
      readSwitches();
      if (BTNBK) {
        waitSwitches();
        SerialPrintLn("\r\nAborted.");
        // Restore original baud rate
        PhysicalSerial.end();
        delay(50);
        PhysicalSerial.begin(bauds[originalSpeed], (SerialConfig)bits[serialConfig], RXD2, TXD2);
        serialSpeed = originalSpeed;
        return;
      }

      delay(10);
    }
  }

  if (detected) {
    SerialPrintLn("\r\n----------------------------------");
    SerialPrintLn("DETECTED: " + String(detectedBaud) + " baud");
    SerialPrintLn("Baud rate has been set.");
    SerialPrintLn("==================================");
    showMessage("Baud Detect\n\nDETECTED:\n" + String(detectedBaud) + " baud\n\nRate set!");
  } else {
    SerialPrintLn("\r\n----------------------------------");
    SerialPrintLn("Detection FAILED - no valid data");
    SerialPrintLn("==================================");
    // Restore original baud rate
    PhysicalSerial.end();
    delay(50);
    PhysicalSerial.begin(bauds[originalSpeed], (SerialConfig)bits[serialConfig], RXD2, TXD2);
    serialSpeed = originalSpeed;
    showMessage("Baud Detect\n\nFAILED\n\nNo valid data\ndetected");
  }
}

// ============================================================================
// System Info
// ============================================================================

void showSystemInfo() {
  SerialPrintLn("\r\n===== System Information =====");
  SerialPrintLn("Firmware:   " + build);
  SerialPrintLn("Free Heap:  " + String(ESP.getFreeHeap()) + " bytes");
  SerialPrintLn("Heap Size:  " + String(ESP.getHeapSize()) + " bytes");
  SerialPrintLn("CPU Freq:   " + String(ESP.getCpuFreqMHz()) + " MHz");
  SerialPrintLn("Chip Model: " + String(ESP.getChipModel()));
  SerialPrintLn("SDK:        " + String(ESP.getSdkVersion()));

  // Uptime
  unsigned long uptime = millis() / 1000;
  int days = uptime / 86400;
  int hours = (uptime % 86400) / 3600;
  int mins = (uptime % 3600) / 60;
  int secs = uptime % 60;
  SerialPrint("Uptime:     ");
  if (days > 0) SerialPrint(String(days) + "d ");
  SerialPrint(String(hours) + "h ");
  SerialPrint(String(mins) + "m ");
  SerialPrintLn(String(secs) + "s");

  // WiFi info
  if (WiFi.status() == WL_CONNECTED) {
    SerialPrintLn("WiFi SSID:  " + WiFi.SSID());
    SerialPrintLn("WiFi RSSI:  " + String(WiFi.RSSI()) + " dBm");
    SerialPrintLn("IP Address: " + WiFi.localIP().toString());
  } else {
    SerialPrintLn("WiFi:       Not connected");
  }

  // Serial config
  SerialPrintLn("Baud Rate:  " + String(bauds[serialSpeed]));
  SerialPrintLn("==============================");

  // OLED display
  showMessage("System Info\n\nHeap:" + String(ESP.getFreeHeap()/1024) + "KB\n" +
              "CPU:" + String(ESP.getCpuFreqMHz()) + "MHz\n" +
              "RSSI:" + String(WiFi.RSSI()) + "dBm");
}

void showConnectionStats() {
  SerialPrintLn("\r\n===== Connection Statistics =====");
  SerialPrintLn("Session Sent:     " + String(bytesSent) + " bytes");
  SerialPrintLn("Session Received: " + String(bytesRecv) + " bytes");
  SerialPrintLn("Hex Dump Mode:    " + String(diagCtx.hexDumpEnabled ? "ON" : "OFF"));
  SerialPrintLn("=================================");

  showMessage("Statistics\n\nSent: " + String(bytesSent) + "\nRecv: " + String(bytesRecv));
}

// ============================================================================
// Diagnostics Menu and Loop
// ============================================================================

void diagnosticsMenu(bool arrow) {
  menuMode = MODE_DIAGNOSTICS;
  showMenu("DIAG", diagnosticsMenuDisp, 6, (arrow ? MENU_DISP : MENU_BOTH), 0);
}

void diagnosticsLoop() {
  int menuSel = -1;
  readSwitches();

  if (BTNUP) {
    menuIdx--;
    if (menuIdx < 0) menuIdx = menuCnt - 1;
    diagnosticsMenu(true);
  }
  else if (BTNDN) {
    menuIdx++;
    if (menuIdx > (menuCnt - 1)) menuIdx = 0;
    diagnosticsMenu(true);
  }
  else if (BTNEN) {
    menuSel = menuIdx;
  }
  else if (BTNBK) {
    mainMenu(false);
    return;
  }
  waitSwitches();

  // Serial input
  int serAvl = SerialAvailable();
  char chr = 0;
  if (serAvl > 0) {
    chr = SerialRead();
    SerialPrint(chr);
  }

  // Handle selection
  if (serAvl > 0 || menuSel > -1) {
    if (chr == 'B' || chr == 'b' || menuSel == 0) {
      // BACK
      mainMenu(false);
    }
    else if (chr == 'A' || chr == 'a' || menuSel == 1) {
      // Auto-Detect Baud
      baudAutoDetect();
      // Wait for keypress
      SerialPrintLn("\r\nPress any key to continue...");
      while (!BTNEN && !BTNBK && SerialAvailable() == 0) {
        readSwitches();
        delay(50);
      }
      waitSwitches();
      while (SerialAvailable()) SerialRead();
      diagnosticsMenu(false);
    }
    else if (chr == 'H' || chr == 'h' || menuSel == 2) {
      // Hex Dump Mode - toggle
      setHexDumpMode(!diagCtx.hexDumpEnabled);
      delay(500);
      diagnosticsMenu(false);
    }
    else if (chr == 'L' || chr == 'l' || menuSel == 3) {
      // Loopback Test - function handles ESC/BACK wait internally
      loopbackTest();
      diagnosticsMenu(false);
    }
    else if (chr == 'S' || chr == 's' || menuSel == 4) {
      // Signal Monitor
      signalMonitor();
      diagnosticsMenu(false);
    }
    else if (chr == 'I' || chr == 'i' || menuSel == 5) {
      // System Info
      showSystemInfo();
      // Wait for keypress
      SerialPrintLn("\r\nPress any key to continue...");
      while (!BTNEN && !BTNBK && SerialAvailable() == 0) {
        readSwitches();
        delay(50);
      }
      waitSwitches();
      while (SerialAvailable()) SerialRead();
      diagnosticsMenu(false);
    }
    else if (serAvl > 0) {
      diagnosticsMenu(false);
    }
  }
}

// ============================================================================
// AT Command Handler
// ============================================================================

bool handleDiagnosticsCommand(String& cmd, String& upCmd) {
  if (upCmd.indexOf("AT$SIG") == 0) {
    showSignalStates();
    return true;
  }
  else if (upCmd.indexOf("AT$LOOP") == 0) {
    loopbackTest();
    return true;
  }
  else if (upCmd.indexOf("AT$HEX") == 0) {
    if (upCmd.substring(6, 7) == "?") {
      SerialPrintLn(String(diagCtx.hexDumpEnabled ? "1" : "0"));
    }
    else if (upCmd.substring(6, 8) == "=1") {
      setHexDumpMode(true);
    }
    else if (upCmd.substring(6, 8) == "=0") {
      setHexDumpMode(false);
    }
    else {
      return false;
    }
    return true;
  }
  else if (upCmd.indexOf("AT$BAUD?") == 0) {
    baudAutoDetect();
    return true;
  }
  else if (upCmd.indexOf("AT$SYS") == 0) {
    showSystemInfo();
    return true;
  }
  else if (upCmd.indexOf("AT$STAT") == 0) {
    showConnectionStats();
    return true;
  }

  return false;
}

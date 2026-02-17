// WiFi Setup Wizard
// Provides user-friendly WiFi network configuration

#include "Arduino.h"
#include "wifi_setup.h"
#include "globals.h"
#include "display_menu.h"
#include "serial_io.h"
#include "settings.h"
#include <WiFi.h>
#include <EEPROM.h>
#include <Adafruit_SSD1306.h>

// External globals
extern Adafruit_SSD1306 display;
extern int menuMode;
extern int menuIdx;
extern int menuCnt;
extern int lastMenu;
extern bool msgFlag;
extern String ssid;
extern String password;
extern byte serialSpeed;
extern String baudDisp[];

// Button state
extern bool BTNUP;
extern bool BTNDN;
extern bool BTNEN;
extern bool BTNBK;

// WiFi setup context
WifiSetupContext wifiSetupCtx;

// Network list storage
static String networkList[WIFI_MAX_NETWORKS];

// Character set for password entry (printable ASCII)
static const char CHAR_SET[] = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
static const int CHAR_SET_LEN = sizeof(CHAR_SET) - 1;

void wifiSetupInit() {
  wifiSetupCtx.state = WIFI_STATE_SCAN;
  wifiSetupCtx.networkCount = 0;
  wifiSetupCtx.selectedNetwork = 0;
  wifiSetupCtx.passwordPos = 0;
  wifiSetupCtx.currentChar = 'a';
  wifiSetupCtx.selectedSSID = "";
  memset(wifiSetupCtx.tempPassword, 0, sizeof(wifiSetupCtx.tempPassword));
}

// Display detailed network information after successful connection
void wifiShowConnectionInfo() {
  // Serial output - detailed info
  SerialPrintLn("\r\n===== WiFi Connected =====");
  SerialPrintLn("SSID:    " + WiFi.SSID());
  SerialPrintLn("IP:      " + WiFi.localIP().toString());
  SerialPrintLn("Gateway: " + WiFi.gatewayIP().toString());
  SerialPrintLn("Subnet:  " + WiFi.subnetMask().toString());
  SerialPrintLn("DNS:     " + WiFi.dnsIP().toString());
  SerialPrintLn("RSSI:    " + String(WiFi.RSSI()) + " dBm");
  SerialPrintLn("Channel: " + String(WiFi.channel()));
  SerialPrintLn("MAC:     " + WiFi.macAddress());
  SerialPrintLn("==========================");

  // OLED display - show key info (limited space)
  display.fillRect(1, 17, 125, 46, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  display.setCursor(3, 18);
  display.print("Connected!");

  display.setCursor(3, 28);
  display.print("IP: ");
  display.print(WiFi.localIP().toString());

  display.setCursor(3, 38);
  display.print("GW: ");
  display.print(WiFi.gatewayIP().toString());

  display.setCursor(3, 48);
  display.print("RSSI: ");
  display.print(WiFi.RSSI());
  display.print("dBm Ch:");
  display.print(WiFi.channel());

  display.setCursor(3, 57);
  display.print("Settings saved!");

  display.setTextWrap(true);
  display.display();
}

void wifiScanNetworks() {
  SerialPrintLn("\r\nScanning for WiFi networks...");
  showMessage("Scanning\nfor WiFi\nnetworks...");

  // Disconnect from current network for clean scan
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks();

  if (n == 0) {
    SerialPrintLn("No networks found.");
    showMessage("No networks\nfound.\n\nPress BACK");
    wifiSetupCtx.networkCount = 0;
  } else {
    wifiSetupCtx.networkCount = min(n, WIFI_MAX_NETWORKS);

    SerialPrintLn("\r\n" + String(wifiSetupCtx.networkCount) + " networks found:\r\n");

    for (int i = 0; i < wifiSetupCtx.networkCount; i++) {
      // Build display string with signal strength indicator
      String rssiStr;
      int rssi = WiFi.RSSI(i);
      if (rssi > -50) rssiStr = "****";
      else if (rssi > -60) rssiStr = "*** ";
      else if (rssi > -70) rssiStr = "**  ";
      else rssiStr = "*   ";

      // Truncate SSID for display (max 12 chars to leave room for signal)
      String ssidName = WiFi.SSID(i);
      if (ssidName.length() > 18) {
        ssidName = ssidName.substring(0, 17) + "~";
      }

      networkList[i] = ssidName + " " + rssiStr;

      // Serial output with full info
      SerialPrint(String(i + 1) + ". ");
      SerialPrint(WiFi.SSID(i));
      SerialPrint(" (");
      SerialPrint(String(rssi));
      SerialPrint(" dBm) ");
      SerialPrintLn(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "[Open]" : "[Secured]");
    }
    SerialPrintLn();
  }

  wifiSetupCtx.state = WIFI_STATE_SELECT;
}

void wifiSetupMenu(bool arrow) {
  menuMode = MODE_WIFI_SETUP;

  if (wifiSetupCtx.networkCount == 0) {
    // No networks - show rescan option
    static String noNetOptions[] = { "Rescan", "Back" };
    showMenu("WIFI", noNetOptions, 2, (arrow ? MENU_DISP : MENU_BOTH), 0);
  } else {
    showMenu("WIFI", networkList, wifiSetupCtx.networkCount, (arrow ? MENU_DISP : MENU_NUM), 0);
  }
}

void wifiPasswordMenu() {
  menuMode = MODE_WIFI_PASSWORD;

  // Clear display area
  display.fillRect(1, 17, 125, 46, SSD1306_BLACK);

  // Draw header showing selected network
  display.setCursor(3, 19);
  display.setTextColor(SSD1306_WHITE);
  display.print("Password for:");

  display.setCursor(3, 28);
  String dispSSID = wifiSetupCtx.selectedSSID;
  if (dispSSID.length() > 20) {
    dispSSID = dispSSID.substring(0, 19) + "~";
  }
  display.print(dispSSID);

  // Draw password entry area
  display.drawLine(1, 38, 126, 38, SSD1306_WHITE);

  // Show current password with cursor
  display.setCursor(3, 41);

  // Display password (show last 17 chars if longer)
  String pwdDisplay = String(wifiSetupCtx.tempPassword);
  int startPos = 0;
  if (pwdDisplay.length() > 16) {
    startPos = pwdDisplay.length() - 16;
    display.print("<");
  }
  display.print(pwdDisplay.substring(startPos));

  // Show current character being selected (blinking cursor position)
  if (wifiSetupCtx.passwordPos < WIFI_PASSWORD_MAX) {
    display.print(wifiSetupCtx.currentChar);
    display.print("_");
  }

  // Instructions at bottom
  display.setCursor(3, 49);
  display.setTextSize(1);
  display.print("UD:chr EN:add BK:del");
  display.setCursor(3, 57);
  display.print("Hold EN to connect");

  display.display();
}

void wifiSetupLoop() {
  // Handle initial scan
  if (wifiSetupCtx.state == WIFI_STATE_SCAN) {
    wifiScanNetworks();
    wifiSetupMenu(false);
    return;
  }

  int menuSel = -1;
  readSwitches();

  if (BTNUP) {
    menuIdx--;
    if (menuIdx < 0)
      menuIdx = menuCnt - 1;
    wifiSetupMenu(true);
  }
  else if (BTNDN) {
    menuIdx++;
    if (menuIdx > (menuCnt - 1))
      menuIdx = 0;
    wifiSetupMenu(true);
  }
  else if (BTNEN) {
    menuSel = menuIdx;
  }
  else if (BTNBK) {
    // Reconnect to original network if we have credentials
    if (ssid.length() > 0) {
      WiFi.begin(ssid.c_str(), password.c_str());
    }
    settingsMenu(false);
    return;
  }
  waitSwitches();

  // Serial input
  int serAvl = SerialAvailable();
  char chr = 0;
  if (serAvl > 0) {
    chr = SerialRead();
    SerialPrint(chr);

    // Handle letter selection (A-T for networks)
    if (wifiSetupCtx.networkCount > 0) {
      if ((chr >= 'A' && chr < 'A' + wifiSetupCtx.networkCount) ||
          (chr >= 'a' && chr < 'a' + wifiSetupCtx.networkCount)) {
        if (chr >= 'a') chr -= 32; // To uppercase
        menuSel = chr - 'A';
      }
    } else {
      // No networks - handle rescan/back
      if (chr == 'R' || chr == 'r') {
        wifiSetupCtx.state = WIFI_STATE_SCAN;
        wifiSetupLoop();
        return;
      }
      if (chr == 'B' || chr == 'b') {
        if (ssid.length() > 0) {
          WiFi.begin(ssid.c_str(), password.c_str());
        }
        settingsMenu(false);
        return;
      }
    }
  }

  // Handle selection
  if (menuSel >= 0) {
    if (wifiSetupCtx.networkCount == 0) {
      // No networks menu
      if (menuSel == 0) {
        // Rescan
        wifiSetupCtx.state = WIFI_STATE_SCAN;
        wifiSetupLoop();
      } else {
        // Back
        if (ssid.length() > 0) {
          WiFi.begin(ssid.c_str(), password.c_str());
        }
        settingsMenu(false);
      }
    } else {
      // Network selected - get the actual SSID (not the display string)
      wifiSetupCtx.selectedNetwork = menuSel;
      wifiSetupCtx.selectedSSID = WiFi.SSID(menuSel);

      // Check if open network
      if (WiFi.encryptionType(menuSel) == WIFI_AUTH_OPEN) {
        // Open network - connect directly
        SerialPrintLn("\r\nConnecting to open network: " + wifiSetupCtx.selectedSSID);
        showMessage("Connecting\nto open\nnetwork...");

        if (wifiTestConnection(wifiSetupCtx.selectedSSID.c_str(), "")) {
          wifiSaveCredentials(wifiSetupCtx.selectedSSID.c_str(), "");
          wifiShowConnectionInfo();
          SerialPrintLn("\r\nPress any key to continue...");
        } else {
          showMessage("Connection\nfailed!\n\nPress any key");
          SerialPrintLn("\r\nConnection failed.");
        }

        // Wait for keypress then return to settings
        while (!BTNEN && !BTNBK && SerialAvailable() == 0) {
          readSwitches();
          delay(50);
        }
        waitSwitches();
        while (SerialAvailable()) SerialRead();
        settingsMenu(false);
      } else {
        // Secured network - enter password mode
        wifiSetupCtx.state = WIFI_STATE_PASSWORD;
        wifiSetupCtx.passwordPos = 0;
        wifiSetupCtx.currentChar = 'a';
        memset(wifiSetupCtx.tempPassword, 0, sizeof(wifiSetupCtx.tempPassword));

        SerialPrintLn("\r\nEnter password for: " + wifiSetupCtx.selectedSSID);
        SerialPrintLn("Type password and press ENTER, or use UP/DN to select chars");
        SerialPrint("\r\nPassword: ");

        wifiPasswordMenu();
      }
    }
  }
}

// Auto-repeat timing
static unsigned long btnHoldStart = 0;
static unsigned long lastRepeat = 0;
static bool btnWasPressed = false;

// Enter long-press timing
static unsigned long enterHoldStart = 0;
static bool enterWasPressed = false;
static unsigned long enterReleaseTime = 0; // Debounce: when ENTER was last released

// Auto-repeat constants
#define REPEAT_DELAY 400    // Initial delay before repeat starts (ms)
#define REPEAT_RATE 80      // Rate of repeat while held (ms)
#define LONG_PRESS_MS 1000  // Hold ENTER for 1 second to submit password
#define ENTER_DEBOUNCE_MS 150 // Debounce time after ENTER release

void wifiPasswordLoop() {
  readSwitches();

  bool upDown = BTNUP || BTNDN;

  if (upDown) {
    unsigned long now = millis();
    bool shouldUpdate = false;

    if (!btnWasPressed) {
      // First press - always update
      btnHoldStart = now;
      lastRepeat = now;
      btnWasPressed = true;
      shouldUpdate = true;
    } else {
      // Button held - check for auto-repeat
      if (now - btnHoldStart > REPEAT_DELAY) {
        // Past initial delay, check repeat rate
        if (now - lastRepeat > REPEAT_RATE) {
          lastRepeat = now;
          shouldUpdate = true;
        }
      }
    }

    if (shouldUpdate) {
      if (BTNUP) {
        // Previous character in set
        const char* pos = strchr(CHAR_SET, wifiSetupCtx.currentChar);
        if (pos) {
          int idx = pos - CHAR_SET;
          idx = (idx - 1 + CHAR_SET_LEN) % CHAR_SET_LEN;
          wifiSetupCtx.currentChar = CHAR_SET[idx];
        } else {
          wifiSetupCtx.currentChar = 'a';
        }
      } else {
        // Next character in set
        const char* pos = strchr(CHAR_SET, wifiSetupCtx.currentChar);
        if (pos) {
          int idx = pos - CHAR_SET;
          idx = (idx + 1) % CHAR_SET_LEN;
          wifiSetupCtx.currentChar = CHAR_SET[idx];
        } else {
          wifiSetupCtx.currentChar = 'a';
        }
      }
      wifiPasswordMenu();
    }
  } else {
    // No up/down pressed - reset hold state
    btnWasPressed = false;
  }

  if (BTNEN) {
    if (!enterWasPressed) {
      // First detection of ENTER press - start timing (with debounce check)
      if (millis() - enterReleaseTime >= ENTER_DEBOUNCE_MS) {
        enterHoldStart = millis();
        enterWasPressed = true;
      }
    } else if (millis() - enterHoldStart >= LONG_PRESS_MS) {
      // Long press threshold reached - submit password immediately
      enterWasPressed = false;

      // Include the currently displayed character in the password
      if (wifiSetupCtx.passwordPos < WIFI_PASSWORD_MAX) {
        wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = wifiSetupCtx.currentChar;
        wifiSetupCtx.passwordPos++;
        wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = '\0';
      }

      SerialPrintLn("\r\n\r\nConnecting to: " + wifiSetupCtx.selectedSSID);
      showMessage("Connecting\nto network\n\nPlease wait...");

      if (wifiTestConnection(wifiSetupCtx.selectedSSID.c_str(), wifiSetupCtx.tempPassword)) {
        wifiSaveCredentials(wifiSetupCtx.selectedSSID.c_str(), wifiSetupCtx.tempPassword);
        wifiShowConnectionInfo();
        SerialPrintLn("\r\nPress any key to continue...");
      } else {
        showMessage("Connection\nfailed!\n\nWrong password?");
        SerialPrintLn("\r\nConnection failed. Wrong password?");
      }

      delay(200);
      waitSwitches(); // Clear any held buttons before waiting for keypress
      while (!BTNEN && !BTNBK && SerialAvailable() == 0) {
        readSwitches();
        delay(50);
      }
      waitSwitches();
      while (SerialAvailable()) SerialRead();
      settingsMenu(false);
      return;
    } else if (millis() - enterHoldStart > 300) {
      // Visual feedback - show they're holding
      display.fillRect(1, 49, 125, 14, SSD1306_BLACK);
      display.setCursor(3, 53);
      display.print("Connecting...");
      display.display();
    }
  }
  else if (enterWasPressed) {
    // ENTER released before long press - short press, add character
    enterWasPressed = false;
    enterReleaseTime = millis(); // Record release time for debounce
    if (wifiSetupCtx.passwordPos < WIFI_PASSWORD_MAX) {
      wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = wifiSetupCtx.currentChar;
      wifiSetupCtx.passwordPos++;
      wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = '\0';
      SerialPrint(String(wifiSetupCtx.currentChar));
    }
    wifiPasswordMenu();
  }
  else if (BTNBK) {
    // Delete last character or go back
    if (wifiSetupCtx.passwordPos > 0) {
      wifiSetupCtx.passwordPos--;
      wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = '\0';
      // Erase character on serial (backspace, space, backspace)
      SerialPrint("\b \b");
      wifiPasswordMenu();
    } else {
      // Go back to network selection
      wifiSetupCtx.state = WIFI_STATE_SELECT;
      wifiSetupMenu(false);
      return;
    }
    waitSwitches();
  }

  // Serial input - direct password typing
  int serAvl = SerialAvailable();
  if (serAvl > 0) {
    char chr = SerialRead();

    if (chr == '\r' || chr == '\n') {
      // Submit password
      SerialPrintLn("\r\n\r\nConnecting to: " + wifiSetupCtx.selectedSSID);
      showMessage("Connecting\nto network\n\nPlease wait...");

      if (wifiTestConnection(wifiSetupCtx.selectedSSID.c_str(), wifiSetupCtx.tempPassword)) {
        wifiSaveCredentials(wifiSetupCtx.selectedSSID.c_str(), wifiSetupCtx.tempPassword);
        wifiShowConnectionInfo();
        SerialPrintLn("\r\nPress any key to continue...");
      } else {
        showMessage("Connection\nfailed!\n\nWrong password?");
        SerialPrintLn("\r\nConnection failed. Wrong password?");
      }

      // Wait for keypress
      delay(200);
      while (!BTNEN && !BTNBK && SerialAvailable() == 0) {
        readSwitches();
        delay(50);
      }
      waitSwitches();
      while (SerialAvailable()) SerialRead();
      settingsMenu(false);
    }
    else if (chr == 0x08 || chr == 0x7F) {
      // Backspace/Delete
      if (wifiSetupCtx.passwordPos > 0) {
        wifiSetupCtx.passwordPos--;
        wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = '\0';
        // Echo backspace
        SerialPrint("\b \b");
      }
      wifiPasswordMenu();
    }
    else if (chr == 0x1B) {
      // Escape - go back
      wifiSetupCtx.state = WIFI_STATE_SELECT;
      wifiSetupMenu(false);
    }
    else if (chr >= 32 && chr < 127) {
      // Printable character - add to password
      if (wifiSetupCtx.passwordPos < WIFI_PASSWORD_MAX) {
        wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = chr;
        wifiSetupCtx.passwordPos++;
        wifiSetupCtx.tempPassword[wifiSetupCtx.passwordPos] = '\0';
        wifiSetupCtx.currentChar = chr; // Update current char to last typed
        // Echo the character
        SerialPrint(String(chr));
      }
      wifiPasswordMenu();
    }
  }
}

bool wifiTestConnection(const char* testSsid, const char* testPassword) {
  WiFi.disconnect();
  delay(100);

  WiFi.begin(testSsid, testPassword);

  // Wait up to 15 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    SerialPrint(".");
    attempts++;
  }
  SerialPrintLn();

  return WiFi.status() == WL_CONNECTED;
}

void wifiSaveCredentials(const char* newSsid, const char* newPassword) {
  // Update global variables
  ssid = String(newSsid);
  password = String(newPassword);

  // Save to EEPROM
  for (int i = 0; i < 32; i++) {
    if (i < strlen(newSsid))
      EEPROM.write(SSID_ADDRESS + i, newSsid[i]);
    else
      EEPROM.write(SSID_ADDRESS + i, 0);
  }

  for (int i = 0; i < 63; i++) {
    if (i < strlen(newPassword))
      EEPROM.write(PASS_ADDRESS + i, newPassword[i]);
    else
      EEPROM.write(PASS_ADDRESS + i, 0);
  }

  EEPROM.commit();

  SerialPrintLn("WiFi credentials saved to EEPROM.");
}

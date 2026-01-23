// WiFi Setup Wizard
// Provides user-friendly WiFi network configuration

#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

// Maximum networks to display
#define WIFI_MAX_NETWORKS 20

// Password entry constants
#define WIFI_PASSWORD_MAX 63

// WiFi setup state
enum WifiSetupState {
  WIFI_STATE_SCAN,      // Scanning for networks
  WIFI_STATE_SELECT,    // Selecting a network
  WIFI_STATE_PASSWORD,  // Entering password
  WIFI_STATE_CONNECT,   // Attempting connection
  WIFI_STATE_SUCCESS,   // Connection successful
  WIFI_STATE_FAILED     // Connection failed
};

// Context for WiFi setup wizard
struct WifiSetupContext {
  WifiSetupState state;
  int networkCount;
  int selectedNetwork;
  char tempPassword[WIFI_PASSWORD_MAX + 1];
  int passwordPos;
  char currentChar;
  String selectedSSID;
};

extern WifiSetupContext wifiSetupCtx;

// Initialize WiFi setup wizard
void wifiSetupInit();

// Display network list menu
void wifiSetupMenu(bool arrow);

// Display password entry screen
void wifiPasswordMenu();

// Main loop for WiFi setup mode
void wifiSetupLoop();

// Main loop for password entry mode
void wifiPasswordLoop();

// Scan for WiFi networks
void wifiScanNetworks();

// Test WiFi connection
bool wifiTestConnection(const char* ssid, const char* password);

// Save WiFi credentials to EEPROM
void wifiSaveCredentials(const char* ssid, const char* password);

#endif // WIFI_SETUP_H

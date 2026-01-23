// ============================================================================
// PPP Gateway Mode Integration
// ============================================================================
// Ties together PPP framing, LCP, IPCP, and NAT
// Provides menu, AT commands, and EEPROM configuration
// ============================================================================

#ifndef PPP_MODE_H
#define PPP_MODE_H

#include <Arduino.h>
#include "ppp.h"
#include "ppp_lcp.h"
#include "ppp_ipcp.h"
#include "ppp_nat.h"

// ============================================================================
// PPP Mode States
// ============================================================================

enum PppModeState {
    PPP_MODE_IDLE,      // Not active
    PPP_MODE_MENU,      // In PPP menu
    PPP_MODE_STARTING,  // Waiting for first PPP frame
    PPP_MODE_LCP,       // LCP negotiation in progress
    PPP_MODE_IPCP,      // IPCP negotiation in progress
    PPP_MODE_ACTIVE,    // PPP gateway running
    PPP_MODE_CLOSING,   // Shutting down
    PPP_MODE_ERROR      // Error state
};

// ============================================================================
// PPP Configuration (saved to EEPROM)
// ============================================================================

struct PppConfig {
    IPAddress gatewayIP;        // Gateway IP (ESP32 side)
    IPAddress poolStart;        // First IP in client pool
    IPAddress primaryDns;       // Primary DNS server
    IPAddress secondaryDns;     // Secondary DNS server
};

// ============================================================================
// PPP Mode Context
// ============================================================================

struct PppModeContext {
    PppModeState state;
    PppConfig config;
    bool configChanged;

    // Timing
    unsigned long lastCleanup;      // Last connection cleanup
    unsigned long lastStatusUpdate; // Last OLED status update
    unsigned long lastLcpTimeout;   // Last LCP timeout check
    unsigned long stateStartTime;   // When current state started

    // Previous mode to return to on disconnect
    int previousMenuMode;
};

// ============================================================================
// EEPROM Addresses for PPP Configuration
// ============================================================================

#define PPP_EEPROM_BASE             900
#define PPP_GATEWAY_IP_ADDRESS      900   // 4 bytes
#define PPP_POOL_START_ADDRESS      904   // 4 bytes
#define PPP_PRIMARY_DNS_ADDRESS     908   // 4 bytes
#define PPP_SECONDARY_DNS_ADDRESS   912   // 4 bytes
#define PPP_EEPROM_END              920

// ============================================================================
// Function Declarations - Mode Interface
// ============================================================================

// Menu function (called to enter/display PPP menu)
void pppMenu(bool arrow);

// Main loop function (called from main loop when in MODE_PPP)
void pppLoop();

// Enter PPP gateway mode (start PPP processing)
void enterPppMode();

// Exit PPP gateway mode (stop PPP, return to menu)
void exitPppMode();

// ============================================================================
// Function Declarations - Display
// ============================================================================

// Show PPP status on display
void pppShowStatus();

// Show PPP statistics
void pppShowStatistics();

// ============================================================================
// Function Declarations - Configuration
// ============================================================================

// Load PPP settings from EEPROM
void loadPppSettings();

// Save PPP settings to EEPROM
void savePppSettings();

// Set default PPP configuration
void pppDefaultConfig();

// ============================================================================
// Function Declarations - AT Commands
// ============================================================================

// Handle PPP-related AT commands
// Returns true if command was handled
bool handlePppCommand(String& cmd, String& upCmd);

// ============================================================================
// Global Contexts (defined in ppp_mode.cpp)
// ============================================================================

extern PppContext pppCtx;
extern LcpContext lcpCtx;
extern IpcpContext ipcpCtx;
extern PppNatContext pppNatCtx;
extern PppModeContext pppModeCtx;

#endif // PPP_MODE_H

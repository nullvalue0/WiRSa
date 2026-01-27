#ifndef SLIP_MODE_H
#define SLIP_MODE_H

#include <Arduino.h>
#include "slip.h"
#include "slip_nat.h"

// ============================================================================
// SLIP Mode States
// ============================================================================

enum SlipModeState {
    SLIP_MODE_IDLE,     // Not active
    SLIP_MODE_MENU,     // In SLIP menu
    SLIP_MODE_ACTIVE,   // SLIP gateway running
    SLIP_MODE_CONFIG,   // Configuring settings
    SLIP_MODE_ERROR     // Error state
};

// ============================================================================
// SLIP Configuration (saved to EEPROM)
// ============================================================================

struct SlipConfig {
    IPAddress slipIP;       // Gateway IP (ESP32 side)
    IPAddress clientIP;     // Client IP (vintage computer)
    IPAddress subnetMask;   // Subnet mask
    IPAddress dnsServer;    // DNS server
};

// ============================================================================
// SLIP Mode Context
// ============================================================================

struct SlipModeContext {
    SlipModeState state;
    SlipConfig config;
    bool configChanged;
    int previousMenuMode;  // Track previous mode for proper return
};

// ============================================================================
// EEPROM Addresses for SLIP Configuration
// ============================================================================

#define SLIP_EEPROM_BASE        800
#define SLIP_IP_ADDRESS         800   // 4 bytes - Gateway IP
#define SLIP_CLIENT_IP_ADDRESS  804   // 4 bytes - Client IP
#define SLIP_SUBNET_ADDRESS     808   // 4 bytes - Subnet mask
#define SLIP_DNS_ADDRESS        812   // 4 bytes - DNS server
#define SLIP_PORTFWD_BASE       816   // Port forwards start here
#define SLIP_PORTFWD_SIZE       8     // 8 bytes per entry (active, proto, extPort, intPort, intIP)
#define SLIP_PORTFWD_COUNT      8     // Max 8 port forwards
#define SLIP_EEPROM_END         880   // End of SLIP EEPROM area

// ============================================================================
// Function Declarations - Mode Interface
// ============================================================================

// Menu function (called to enter/display SLIP menu)
void slipMenu(bool arrow);

// Main loop function (called from main loop when in MODE_SLIP)
void slipLoop();

// Enter SLIP gateway mode (start SLIP processing)
void enterSlipMode();

// Exit SLIP gateway mode (stop SLIP, return to menu)
void exitSlipMode();

// ============================================================================
// Function Declarations - Display
// ============================================================================

// Show SLIP status on display
void slipShowStatus();

// Show SLIP statistics
void slipShowStatistics();

// ============================================================================
// Function Declarations - Configuration
// ============================================================================

// Load SLIP settings from EEPROM
void loadSlipSettings();

// Save SLIP settings to EEPROM
void saveSlipSettings();

// Set default SLIP configuration
void slipDefaultConfig();

// Configure IP addresses
void slipConfigureIP();

// Configure port forwards
void slipConfigurePortForward();

// ============================================================================
// Function Declarations - AT Commands
// ============================================================================

// Handle SLIP-related AT commands
// Returns true if command was handled
bool handleSlipCommand(String& cmd, String& upCmd);

// ============================================================================
// Global Contexts (defined in slip_mode.cpp)
// ============================================================================

extern SlipContext slipCtx;
extern NatContext natCtx;
extern SlipModeContext slipModeCtx;

#endif // SLIP_MODE_H

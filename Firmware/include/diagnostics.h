// Diagnostics Module
// Serial diagnostics and troubleshooting tools for WiRSa

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <Arduino.h>

// Diagnostic state context
struct DiagnosticsContext {
  bool hexDumpEnabled;       // Hex dump mode active
  bool signalMonitorActive;  // Signal monitor running
  uint32_t loopbackSent;     // Bytes sent in loopback test
  uint32_t loopbackRecv;     // Bytes received in loopback test
  uint32_t loopbackErrors;   // Loopback error count
  uint32_t connectionCount;  // Total connections made
  uint32_t totalBytesSent;   // Lifetime bytes sent
  uint32_t totalBytesRecv;   // Lifetime bytes received
};

extern DiagnosticsContext diagCtx;

// Initialization
void diagnosticsInit();

// Menu and navigation
void diagnosticsMenu(bool arrow);
void diagnosticsLoop();

// Signal Monitor
void signalMonitor();           // Continuous monitor (menu mode)
void showSignalStates();        // One-shot display (AT command)

// Loopback Test
void loopbackTest();            // Run loopback test

// Hex Dump Mode
void setHexDumpMode(bool enabled);
bool isHexDumpEnabled();
void hexDumpByte(char direction, uint8_t byte);  // 'T' for TX, 'R' for RX

// Baud Rate Auto-Detect
void baudAutoDetect();          // Run baud rate detection

// System Information
void showSystemInfo();          // Display system info
void showConnectionStats();     // Display connection statistics

// AT Command handler
bool handleDiagnosticsCommand(String& cmd, String& upCmd);

#endif // DIAGNOSTICS_H

// ============================================================================
// PPP Gateway Mode Implementation
// ============================================================================
// Mode handler for PPP gateway functionality
// ============================================================================

#include "ppp_mode.h"
#include "globals.h"
#include "serial_io.h"
#include "display_menu.h"
#include "network.h"
#include "modem.h"
#include <EEPROM.h>

extern bool usbDebug;

// ============================================================================
// Global Contexts
// ============================================================================

PppContext pppCtx;
LcpContext lcpCtx;
IpcpContext ipcpCtx;
PppNatContext pppNatCtx;
PppModeContext pppModeCtx;

// Menu definitions
static String pppMenuDisp[] = { "MAIN", "Start Gateway", "Configure IP", "View Stats" };
static String pppActiveMenuDisp[] = { "Exit Gateway", "Info", "Totals" };

// External functions from display_menu.cpp
extern void showMenu(String menuName, String options[], int sz, int dispMode, int defaultSel);
extern bool showHeader(String menu, int dispMode);
extern void showMessage(String message);
extern void readSwitches();
extern void waitSwitches();
extern void mainMenu(bool arrow);

// External globals
extern Adafruit_SSD1306 display;
extern int menuMode;
extern int menuIdx;
extern int menuCnt;
extern int lastMenu;
extern bool msgFlag;
extern bool BTNUP;
extern bool BTNDN;
extern bool BTNEN;
extern bool BTNBK;

// Forward declarations
static void pppMenuLoop();
static void pppActiveLoop();
static void pppUpdateActiveDisplay();
static void pppProcessFrame();
static bool parseIPAddress(const String& str, IPAddress& ip);
static void pppConfigureIP();

// External functions
extern String getLine();
extern String prompt(String msg, String dflt);
extern void clearInputBuffer();

// ============================================================================
// PPP Menu Function
// ============================================================================

void pppMenu(bool arrow) {
    menuMode = MODE_PPP;

    if (pppModeCtx.state == PPP_MODE_ACTIVE ||
        pppModeCtx.state == PPP_MODE_LCP ||
        pppModeCtx.state == PPP_MODE_IPCP ||
        pppModeCtx.state == PPP_MODE_STARTING) {
        showMenu("PPP", pppActiveMenuDisp, 3, (arrow ? MENU_DISP : MENU_BOTH), 0);
    } else {
        showMenu("PPP", pppMenuDisp, 4, (arrow ? MENU_DISP : MENU_BOTH), 0);
    }
}

// ============================================================================
// PPP Loop Function (Main Mode Handler)
// ============================================================================

void pppLoop() {
    if (pppModeCtx.state >= PPP_MODE_STARTING && pppModeCtx.state <= PPP_MODE_ACTIVE) {
        pppActiveLoop();
    } else {
        pppMenuLoop();
    }
}

// ============================================================================
// PPP IP Configuration (Interactive Prompts)
// ============================================================================

static void pppConfigureIP() {
    SerialPrintLn("\r\n===== PPP IP Configuration =====");
    SerialPrintLn("Enter new values or press ENTER to keep current setting.");
    SerialPrintLn("");

    // Gateway IP
    String gatewayStr = prompt("Gateway IP", ipToString(pppModeCtx.config.gatewayIP));
    if (gatewayStr.length() > 0) {
        IPAddress newIP;
        if (parseIPAddress(gatewayStr, newIP)) {
            pppModeCtx.config.gatewayIP = newIP;
            SerialPrintLn("Gateway IP set to " + ipToString(newIP));
        } else {
            SerialPrintLn("Invalid IP address, keeping current value");
        }
    }

    // Pool Start IP
    String poolStr = prompt("Pool Start IP", ipToString(pppModeCtx.config.poolStart));
    if (poolStr.length() > 0) {
        IPAddress newIP;
        if (parseIPAddress(poolStr, newIP)) {
            pppModeCtx.config.poolStart = newIP;
            SerialPrintLn("Pool Start set to " + ipToString(newIP));
        } else {
            SerialPrintLn("Invalid IP address, keeping current value");
        }
    }

    // Primary DNS
    String dns1Str = prompt("Primary DNS", ipToString(pppModeCtx.config.primaryDns));
    if (dns1Str.length() > 0) {
        IPAddress newIP;
        if (parseIPAddress(dns1Str, newIP)) {
            pppModeCtx.config.primaryDns = newIP;
            SerialPrintLn("Primary DNS set to " + ipToString(newIP));
        } else {
            SerialPrintLn("Invalid IP address, keeping current value");
        }
    }

    // Secondary DNS
    String dns2Str = prompt("Secondary DNS", ipToString(pppModeCtx.config.secondaryDns));
    if (dns2Str.length() > 0) {
        IPAddress newIP;
        if (parseIPAddress(dns2Str, newIP)) {
            pppModeCtx.config.secondaryDns = newIP;
            SerialPrintLn("Secondary DNS set to " + ipToString(newIP));
        } else {
            SerialPrintLn("Invalid IP address, keeping current value");
        }
    }

    // Save configuration
    savePppSettings();
    SerialPrintLn("\r\nConfiguration saved!");
    SerialPrintLn("");

    // Show final configuration
    SerialPrintLn("=== Current PPP Configuration ===");
    SerialPrintLn("Gateway IP:    " + ipToString(pppModeCtx.config.gatewayIP));
    SerialPrintLn("Pool Start:    " + ipToString(pppModeCtx.config.poolStart));
    SerialPrintLn("Primary DNS:   " + ipToString(pppModeCtx.config.primaryDns));
    SerialPrintLn("Secondary DNS: " + ipToString(pppModeCtx.config.secondaryDns));
    SerialPrintLn("=================================");

    delay(1000);
}

// ============================================================================
// PPP Menu Loop (When Not Active)
// ============================================================================

static void pppMenuLoop() {
    int menuSel = -1;

    readSwitches();

    if (BTNUP) {
        menuIdx--;
        if (menuIdx < 0)
            menuIdx = menuCnt - 1;
        pppMenu(true);
    }
    else if (BTNDN) {
        menuIdx++;
        if (menuIdx > (menuCnt - 1))
            menuIdx = 0;
        pppMenu(true);
    }
    else if (BTNEN) {
        menuSel = menuIdx;
    }
    else if (BTNBK) {
        mainMenu(false);
        return;
    }

    waitSwitches();

    // Read serial input
    int serAvl = SerialAvailable();
    char chr = 0;
    if (serAvl > 0) {
        chr = SerialRead();
        SerialPrint(chr);
    }

    // Process selection
    if (serAvl > 0 || menuSel > -1) {
        if (chr == 'M' || chr == 'm' || menuSel == 0) {
            mainMenu(false);
        }
        else if (chr == 'S' || chr == 's' || menuSel == 1) {
            enterPppMode();
        }
        else if (chr == 'C' || chr == 'c' || menuSel == 2) {
            // Configure IP - interactive prompts
            pppConfigureIP();
            pppMenu(false);
        }
        else if (chr == 'V' || chr == 'v' || menuSel == 3) {
            pppShowStatistics();
        }
        else {
            pppMenu(false);
        }
    }
}

// ============================================================================
// PPP Active Loop (When Gateway Running)
// ============================================================================

static void pppActiveLoop() {
    unsigned long now = millis();

    // Check for button input
    readSwitches();

    if (BTNBK) {
        waitSwitches();
        exitPppMode();
        pppMenu(false);
        return;
    }

    if (BTNUP || BTNDN) {
        waitSwitches();
        pppShowStatus();
    }

    if (BTNEN) {
        waitSwitches();
        pppMenu(true);
    }

    // Check for +++ escape sequence
    static char escapeBuffer[4] = {0};
    static int escapePos = 0;
    static unsigned long lastEscapeTime = 0;

    // Process incoming bytes from PhysicalSerial through PPP framing
    while (PhysicalSerial.available()) {
        uint8_t byte = PhysicalSerial.read();

        // Check for +++ escape (only check before PPP established)
        if (byte == '+' && pppModeCtx.state == PPP_MODE_STARTING) {
            if (now - lastEscapeTime > 1000) {
                escapePos = 0;
            }
            escapePos++;
            lastEscapeTime = now;

            if (escapePos >= 3) {
                SerialPrintLn("\r\nOK");
                exitPppMode();
                pppMenu(false);
                return;
            }
            continue;
        } else if (byte != '+') {
            escapePos = 0;
        }

        // Process byte through PPP framing
        int frameLen = pppReceiveByte(&pppCtx, byte);

        if (frameLen > 0) {
            // Complete PPP frame received
            pppProcessFrame();
        } else if (frameLen < 0) {
            // FCS error
            if (usbDebug) UsbDebugPrintLn("PPP: FCS error");
        }
    }

    // Periodic stats for debugging stalls (only if USB debug enabled)
    //commenting out for now
    // static unsigned long lastPppStats = 0;
    // static uint32_t lastBytesRecv = 0;
    // if (usbDebug && (now - lastPppStats > 5000)) {
    //     uint32_t bytesThisPeriod = pppCtx.bytesReceived - lastBytesRecv;
    //     int cts = digitalRead(CTS_PIN);
    //     UsbDebugPrint("");  // Print timestamp
    //     Serial.printf("PPP stats: rxBytes=%u (+%u), frames=%u, fcsErr=%u, avail=%d, CTS=%d\r\n",
    //                   pppCtx.bytesReceived, bytesThisPeriod,
    //                   pppCtx.framesReceived, pppCtx.fcsErrors,
    //                   PhysicalSerial.available(), cts);
    //     lastBytesRecv = pppCtx.bytesReceived;
    //     lastPppStats = now;
    // }

    // Handle LCP/IPCP timeouts
    if (now - pppModeCtx.lastLcpTimeout > 100) {
        pppModeCtx.lastLcpTimeout = now;

        if (pppModeCtx.state == PPP_MODE_LCP) {
            if (lcpTimeout(&lcpCtx, &pppCtx)) {
                // State changed
            }
            if (lcpIsOpened(&lcpCtx)) {
                // LCP opened - start IPCP
                pppModeCtx.state = PPP_MODE_IPCP;
                ipcpOpen(&ipcpCtx, &pppCtx);
                SerialPrintLn("LCP opened, starting IPCP...");
            }
        }
        else if (pppModeCtx.state == PPP_MODE_IPCP) {
            if (ipcpTimeout(&ipcpCtx, &pppCtx)) {
                // State changed
            }
            if (ipcpIsOpened(&ipcpCtx)) {
                // IPCP opened - gateway active
                pppModeCtx.state = PPP_MODE_ACTIVE;
                SerialPrintLn("PPP Gateway ACTIVE");
                SerialPrint("  Client IP: ");
                SerialPrintLn(ipToString(ipcpCtx.peerIP));

                // Configure NAT with assigned IP
                pppNatSetIPs(&pppNatCtx, ipcpCtx.ourIP, ipcpCtx.peerIP,
                             IPAddress(255, 255, 255, 0), ipcpCtx.primaryDns);
            }
        }

        // Check for link down during negotiation
        if (pppModeCtx.state == PPP_MODE_LCP &&
            lcpCtx.state == LCP_STATE_STOPPED) {
            SerialPrintLn("LCP failed - check connection");
            pppModeCtx.state = PPP_MODE_ERROR;
        }
    }

    // When active, poll NAT connections
    if (pppModeCtx.state == PPP_MODE_ACTIVE) {
        // Check if peer terminated the link
        if (lcpCtx.state == LCP_STATE_STOPPED) {
            SerialPrintLn("PPP: Peer disconnected");
            exitPppMode();
            // Return to previous mode (modem mode or PPP menu)
            if (pppModeCtx.previousMenuMode == MODE_MODEM) {
                menuMode = MODE_MODEM;
            } else {
                pppMenu(false);
            }
            return;
        }

        pppNatPollConnections(&pppNatCtx, &pppCtx);

        // Periodic cleanup
        if (now - pppModeCtx.lastCleanup > 10000) {
            pppNatCleanupExpired(&pppNatCtx);
            pppModeCtx.lastCleanup = now;
        }
    }

    // Periodic display update
    if (now - pppModeCtx.lastStatusUpdate > 1000) {
        pppUpdateActiveDisplay();
        pppModeCtx.lastStatusUpdate = now;
    }

    // Timeout waiting for PPP frames
    if (pppModeCtx.state == PPP_MODE_STARTING) {
        if (now - pppModeCtx.stateStartTime > 60000) {
            SerialPrintLn("Timeout waiting for PPP");
            pppModeCtx.state = PPP_MODE_ERROR;
        }
    }
}

// ============================================================================
// Process Complete PPP Frame
// ============================================================================

static void pppProcessFrame() {
    uint16_t protocol = pppGetProtocol(&pppCtx);
    uint16_t payloadLen;
    uint8_t* payload = pppGetPayload(&pppCtx, &payloadLen);

    if (!payload || payloadLen == 0) {
        return;
    }

    switch (protocol) {
        case PPP_PROTO_LCP:
            // LCP packet
            if (pppModeCtx.state == PPP_MODE_STARTING) {
                // First LCP packet - start negotiation
                pppModeCtx.state = PPP_MODE_LCP;
                lcpOpen(&lcpCtx, &pppCtx);
            }
            lcpProcessPacket(&lcpCtx, &pppCtx, payload, payloadLen);
            break;

        case PPP_PROTO_IPCP:
            // IPCP packet
            if (pppModeCtx.state == PPP_MODE_IPCP || pppModeCtx.state == PPP_MODE_ACTIVE) {
                ipcpProcessPacket(&ipcpCtx, &pppCtx, payload, payloadLen);
            }
            break;

        case PPP_PROTO_IP:
            // IP packet - route through NAT
            if (pppModeCtx.state == PPP_MODE_ACTIVE) {
                if (usbDebug) {
                    UsbDebugPrint("");
                    Serial.printf("IP packet received, len=%d\r\n", payloadLen);
                }
                pppNatProcessPacket(&pppNatCtx, &pppCtx, payload, payloadLen);
            } else {
                if (usbDebug) {
                    UsbDebugPrint("");
                    Serial.printf("IP packet dropped, state=%d\r\n", pppModeCtx.state);
                }
            }
            break;

        default:
            // Unknown protocol - send Protocol-Reject if LCP is open
            if (lcpIsOpened(&lcpCtx)) {
                uint8_t reject[256];
                uint16_t len = (payloadLen > 248) ? 248 : payloadLen;
                reject[0] = LCP_PROTOCOL_REJECT;
                reject[1] = lcpCtx.identifier + 1;
                uint16_t totalLen = 4 + 2 + len;
                reject[2] = (totalLen >> 8) & 0xFF;
                reject[3] = totalLen & 0xFF;
                reject[4] = (protocol >> 8) & 0xFF;
                reject[5] = protocol & 0xFF;
                memcpy(&reject[6], payload, len);
                pppSendFrame(&pppCtx, PPP_PROTO_LCP, reject, 6 + len);
            }
            break;
    }
}

// ============================================================================
// Enter PPP Gateway Mode
// ============================================================================

void enterPppMode() {
    // Save previous mode to return to on disconnect
    pppModeCtx.previousMenuMode = menuMode;

    // Switch to PPP mode - critical! Otherwise main loop keeps running modemLoop
    menuMode = MODE_PPP;

    // Enable binary mode - suppress text output to PhysicalSerial
    // PPP requires clean binary channel on RS232
    setBinaryMode(true);

    SerialPrintLn("\r\nEntering PPP Gateway Mode...");

    // Load settings
    loadPppSettings();

    // Initialize contexts
    pppInit(&pppCtx);
    lcpInit(&lcpCtx);
    ipcpInit(&ipcpCtx);
    pppNatInit(&pppNatCtx);

    // Apply configuration
    ipcpSetGatewayIP(&ipcpCtx, pppModeCtx.config.gatewayIP);
    ipcpSetPoolStart(&ipcpCtx, pppModeCtx.config.poolStart);
    ipcpSetDns(&ipcpCtx, pppModeCtx.config.primaryDns, pppModeCtx.config.secondaryDns);

    // Connect WiFi if needed
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            SerialPrintLn("WiFi connection required for PPP mode");
            showMessage("WiFi Required\nConnect First");
            delay(2000);
            return;
        }
    }

    pppModeCtx.stateStartTime = millis();
    pppModeCtx.lastCleanup = millis();
    pppModeCtx.lastStatusUpdate = millis();
    pppModeCtx.lastLcpTimeout = millis();

    SerialPrintLn("PPP Gateway Starting...");
    SerialPrint("  Gateway IP: ");
    SerialPrintLn(ipToString(pppModeCtx.config.gatewayIP));
    SerialPrint("  Pool Start: ");
    SerialPrintLn(ipToString(pppModeCtx.config.poolStart));
    SerialPrint("  WiFi IP:    ");
    SerialPrintLn(ipToString(WiFi.localIP()));
    SerialPrintLn("\r\nPress +++ or BACK button to exit");

    // Start LCP negotiation immediately - server initiates
    // Windows 98 and most dial-up clients expect the server to send first
    pppModeCtx.state = PPP_MODE_LCP;
    lcpOpen(&lcpCtx, &pppCtx);
    SerialPrintLn("LCP: Initiating negotiation (server-first mode)");

    pppShowStatus();
}

// ============================================================================
// Exit PPP Gateway Mode
// ============================================================================

void exitPppMode() {
    // Disable binary mode - restore text output to PhysicalSerial
    setBinaryMode(false);

    SerialPrintLn("\r\nExiting PPP Gateway Mode...");

    // Close IPCP first
    if (ipcpIsOpened(&ipcpCtx)) {
        ipcpClose(&ipcpCtx, &pppCtx);
    }

    // Close LCP
    if (lcpIsOpened(&lcpCtx)) {
        lcpClose(&lcpCtx, &pppCtx);
    }

    // Shutdown NAT
    pppNatShutdown(&pppNatCtx);

    // Reset contexts
    pppReset(&pppCtx);

    pppModeCtx.state = PPP_MODE_IDLE;

    SerialPrintLn("PPP Gateway stopped");
    showMessage("PPP Stopped");
    delay(1000);
}

// ============================================================================
// Update Active Display
// ============================================================================

static void pppUpdateActiveDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    const char* stateStr = "???";
    switch (pppModeCtx.state) {
        case PPP_MODE_STARTING: stateStr = "STARTING"; break;
        case PPP_MODE_LCP: stateStr = "LCP"; break;
        case PPP_MODE_IPCP: stateStr = "IPCP"; break;
        case PPP_MODE_ACTIVE: stateStr = "ACTIVE"; break;
        case PPP_MODE_ERROR: stateStr = "ERROR"; break;
        default: break;
    }

    display.print("PPP: ");
    display.println(stateStr);

    if (pppModeCtx.state == PPP_MODE_ACTIVE) {
        display.print("Client: ");
        display.println(ipToString(ipcpCtx.peerIP));

        display.print("TCP: ");
        display.print(pppNatGetActiveTcpCount(&pppNatCtx));
        display.print(" UDP: ");
        display.println(pppNatGetActiveUdpCount(&pppNatCtx));

        display.print("TX:");
        display.print(pppNatCtx.packetsToInternet);
        display.print(" RX:");
        display.println(pppNatCtx.packetsFromInternet);
    } else if (pppModeCtx.state == PPP_MODE_LCP) {
        display.print("LCP: ");
        display.println(lcpStateName(lcpCtx.state));
    } else if (pppModeCtx.state == PPP_MODE_IPCP) {
        display.print("IPCP: ");
        display.println(ipcpStateName(ipcpCtx.state));
    }

    display.println();
    display.println("BACK to exit");

    display.display();
}

// ============================================================================
// Show PPP Status
// ============================================================================

void pppShowStatus() {
    pppUpdateActiveDisplay();
}

// ============================================================================
// Show PPP Statistics
// ============================================================================

void pppShowStatistics() {
    SerialPrintLn("\r\n=== PPP Statistics ===");

    SerialPrint("PPP Frames RX: ");
    SerialPrintLn(String(pppCtx.framesReceived));
    SerialPrint("PPP Frames TX: ");
    SerialPrintLn(String(pppCtx.framesSent));
    SerialPrint("FCS Errors:    ");
    SerialPrintLn(String(pppCtx.fcsErrors));

    SerialPrintLn("");
    SerialPrint("Packets to Internet:   ");
    SerialPrintLn(String(pppNatCtx.packetsToInternet));
    SerialPrint("Packets from Internet: ");
    SerialPrintLn(String(pppNatCtx.packetsFromInternet));
    SerialPrint("Dropped Packets:       ");
    SerialPrintLn(String(pppNatCtx.droppedPackets));

    SerialPrintLn("");
    SerialPrint("TCP Connections: ");
    SerialPrint(String(pppNatGetActiveTcpCount(&pppNatCtx)));
    SerialPrint(" active, ");
    SerialPrint(String(pppNatCtx.tcpConnections));
    SerialPrintLn(" total");

    SerialPrint("UDP Sessions:    ");
    SerialPrint(String(pppNatGetActiveUdpCount(&pppNatCtx)));
    SerialPrint(" active, ");
    SerialPrint(String(pppNatCtx.udpSessions));
    SerialPrintLn(" total");

    SerialPrintLn("");

    showMessage("Stats shown\non serial");
    delay(1500);
    pppMenu(false);
}

// ============================================================================
// Load PPP Settings from EEPROM
// ============================================================================

void loadPppSettings() {
    // Check if PPP EEPROM area is initialized
    uint8_t byte0 = EEPROM.read(PPP_GATEWAY_IP_ADDRESS);
    uint8_t byte1 = EEPROM.read(PPP_GATEWAY_IP_ADDRESS + 1);

    if (byte0 == 0xFF && byte1 == 0xFF) {
        // Not initialized - use defaults
        pppDefaultConfig();
        savePppSettings();
        return;
    }

    // Load gateway IP
    pppModeCtx.config.gatewayIP = IPAddress(
        EEPROM.read(PPP_GATEWAY_IP_ADDRESS),
        EEPROM.read(PPP_GATEWAY_IP_ADDRESS + 1),
        EEPROM.read(PPP_GATEWAY_IP_ADDRESS + 2),
        EEPROM.read(PPP_GATEWAY_IP_ADDRESS + 3)
    );

    // Load pool start
    pppModeCtx.config.poolStart = IPAddress(
        EEPROM.read(PPP_POOL_START_ADDRESS),
        EEPROM.read(PPP_POOL_START_ADDRESS + 1),
        EEPROM.read(PPP_POOL_START_ADDRESS + 2),
        EEPROM.read(PPP_POOL_START_ADDRESS + 3)
    );

    // Load primary DNS
    pppModeCtx.config.primaryDns = IPAddress(
        EEPROM.read(PPP_PRIMARY_DNS_ADDRESS),
        EEPROM.read(PPP_PRIMARY_DNS_ADDRESS + 1),
        EEPROM.read(PPP_PRIMARY_DNS_ADDRESS + 2),
        EEPROM.read(PPP_PRIMARY_DNS_ADDRESS + 3)
    );

    // Load secondary DNS
    pppModeCtx.config.secondaryDns = IPAddress(
        EEPROM.read(PPP_SECONDARY_DNS_ADDRESS),
        EEPROM.read(PPP_SECONDARY_DNS_ADDRESS + 1),
        EEPROM.read(PPP_SECONDARY_DNS_ADDRESS + 2),
        EEPROM.read(PPP_SECONDARY_DNS_ADDRESS + 3)
    );

    // Validate loaded configuration
    // Check that pool start matches gateway IP's network (first 3 octets should be related)
    // and that IPs are in valid private ranges
    bool valid = true;

    // Gateway and pool should be on same subnet
    if (pppModeCtx.config.gatewayIP[0] != pppModeCtx.config.poolStart[0] ||
        pppModeCtx.config.gatewayIP[1] != pppModeCtx.config.poolStart[1] ||
        pppModeCtx.config.gatewayIP[2] != pppModeCtx.config.poolStart[2]) {
        valid = false;
    }

    // Gateway IP should be valid (not 0.0.0.0 or 255.x.x.x)
    if (pppModeCtx.config.gatewayIP[0] == 0 || pppModeCtx.config.gatewayIP[0] >= 240) {
        valid = false;
    }

    if (!valid) {
        SerialPrintLn("PPP: Invalid config detected, resetting to defaults");
        pppDefaultConfig();
        savePppSettings();
    } else {
        pppModeCtx.configChanged = false;
    }
}

// ============================================================================
// Save PPP Settings to EEPROM
// ============================================================================

void savePppSettings() {
    // Save gateway IP
    EEPROM.write(PPP_GATEWAY_IP_ADDRESS, pppModeCtx.config.gatewayIP[0]);
    EEPROM.write(PPP_GATEWAY_IP_ADDRESS + 1, pppModeCtx.config.gatewayIP[1]);
    EEPROM.write(PPP_GATEWAY_IP_ADDRESS + 2, pppModeCtx.config.gatewayIP[2]);
    EEPROM.write(PPP_GATEWAY_IP_ADDRESS + 3, pppModeCtx.config.gatewayIP[3]);

    // Save pool start
    EEPROM.write(PPP_POOL_START_ADDRESS, pppModeCtx.config.poolStart[0]);
    EEPROM.write(PPP_POOL_START_ADDRESS + 1, pppModeCtx.config.poolStart[1]);
    EEPROM.write(PPP_POOL_START_ADDRESS + 2, pppModeCtx.config.poolStart[2]);
    EEPROM.write(PPP_POOL_START_ADDRESS + 3, pppModeCtx.config.poolStart[3]);

    // Save primary DNS
    EEPROM.write(PPP_PRIMARY_DNS_ADDRESS, pppModeCtx.config.primaryDns[0]);
    EEPROM.write(PPP_PRIMARY_DNS_ADDRESS + 1, pppModeCtx.config.primaryDns[1]);
    EEPROM.write(PPP_PRIMARY_DNS_ADDRESS + 2, pppModeCtx.config.primaryDns[2]);
    EEPROM.write(PPP_PRIMARY_DNS_ADDRESS + 3, pppModeCtx.config.primaryDns[3]);

    // Save secondary DNS
    EEPROM.write(PPP_SECONDARY_DNS_ADDRESS, pppModeCtx.config.secondaryDns[0]);
    EEPROM.write(PPP_SECONDARY_DNS_ADDRESS + 1, pppModeCtx.config.secondaryDns[1]);
    EEPROM.write(PPP_SECONDARY_DNS_ADDRESS + 2, pppModeCtx.config.secondaryDns[2]);
    EEPROM.write(PPP_SECONDARY_DNS_ADDRESS + 3, pppModeCtx.config.secondaryDns[3]);

    EEPROM.commit();
    pppModeCtx.configChanged = false;
}

// ============================================================================
// Set Default PPP Configuration
// ============================================================================

void pppDefaultConfig() {
    pppModeCtx.config.gatewayIP = IPAddress(192, 168, 8, 1);
    pppModeCtx.config.poolStart = IPAddress(192, 168, 8, 2);
    pppModeCtx.config.primaryDns = IPAddress(8, 8, 8, 8);
    pppModeCtx.config.secondaryDns = IPAddress(8, 8, 4, 4);
    pppModeCtx.state = PPP_MODE_IDLE;
    pppModeCtx.configChanged = true;
}

// ============================================================================
// Parse IP Address String
// ============================================================================

static bool parseIPAddress(const String& str, IPAddress& ip) {
    int parts[4];
    int partIndex = 0;
    String current = "";

    for (unsigned int i = 0; i <= str.length(); i++) {
        char c = (i < str.length()) ? str[i] : '.';
        if (c == '.') {
            if (current.length() == 0 || partIndex >= 4) {
                return false;
            }
            parts[partIndex++] = current.toInt();
            current = "";
        } else if (c >= '0' && c <= '9') {
            current += c;
        } else {
            return false;
        }
    }

    if (partIndex != 4) {
        return false;
    }

    for (int i = 0; i < 4; i++) {
        if (parts[i] < 0 || parts[i] > 255) {
            return false;
        }
    }

    ip = IPAddress(parts[0], parts[1], parts[2], parts[3]);
    return true;
}

// ============================================================================
// Handle PPP AT Commands
// ============================================================================

bool handlePppCommand(String& cmd, String& upCmd) {
    // AT$PPP - Enter PPP mode
    if (upCmd == "AT$PPP" || upCmd == "ATPPP") {
        pppMenu(false);
        return true;
    }

    // AT$PPPSTAT - Show statistics
    if (upCmd == "AT$PPPSTAT") {
        pppShowStatistics();
        return true;
    }

    // AT$PPPSHOW - Show configuration
    if (upCmd == "AT$PPPSHOW") {
        loadPppSettings();
        SerialPrintLn("\r\n=== PPP Configuration ===");
        SerialPrint("Gateway IP:    ");
        SerialPrintLn(ipToString(pppModeCtx.config.gatewayIP));
        SerialPrint("Pool Start:    ");
        SerialPrintLn(ipToString(pppModeCtx.config.poolStart));
        SerialPrint("Primary DNS:   ");
        SerialPrintLn(ipToString(pppModeCtx.config.primaryDns));
        SerialPrint("Secondary DNS: ");
        SerialPrintLn(ipToString(pppModeCtx.config.secondaryDns));
        return true;
    }

    // AT$PPPGW=x.x.x.x - Set gateway IP
    if (upCmd.indexOf("AT$PPPGW=") == 0) {
        String ipStr = cmd.substring(9);
        IPAddress ip;
        if (parseIPAddress(ipStr, ip)) {
            pppModeCtx.config.gatewayIP = ip;
            savePppSettings();
            SerialPrint("Gateway IP set to ");
            SerialPrintLn(ipToString(ip));
            return true;
        } else {
            SerialPrintLn("Invalid IP address");
            return false;
        }
    }

    // AT$PPPPOOL=x.x.x.x - Set pool start IP
    if (upCmd.indexOf("AT$PPPPOOL=") == 0) {
        String ipStr = cmd.substring(11);
        IPAddress ip;
        if (parseIPAddress(ipStr, ip)) {
            pppModeCtx.config.poolStart = ip;
            savePppSettings();
            SerialPrint("Pool start set to ");
            SerialPrintLn(ipToString(ip));
            return true;
        } else {
            SerialPrintLn("Invalid IP address");
            return false;
        }
    }

    // AT$PPPDNS=x.x.x.x - Set primary DNS
    if (upCmd.indexOf("AT$PPPDNS=") == 0) {
        String ipStr = cmd.substring(10);
        IPAddress ip;
        if (parseIPAddress(ipStr, ip)) {
            pppModeCtx.config.primaryDns = ip;
            savePppSettings();
            SerialPrint("Primary DNS set to ");
            SerialPrintLn(ipToString(ip));
            return true;
        } else {
            SerialPrintLn("Invalid IP address");
            return false;
        }
    }

    // AT$PPPDNS2=x.x.x.x - Set secondary DNS
    if (upCmd.indexOf("AT$PPPDNS2=") == 0) {
        String ipStr = cmd.substring(11);
        IPAddress ip;
        if (parseIPAddress(ipStr, ip)) {
            pppModeCtx.config.secondaryDns = ip;
            savePppSettings();
            SerialPrint("Secondary DNS set to ");
            SerialPrintLn(ipToString(ip));
            return true;
        } else {
            SerialPrintLn("Invalid IP address");
            return false;
        }
    }

    return false;  // Command not handled
}

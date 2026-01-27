// ============================================================================
// SLIP Gateway Mode Implementation
// ============================================================================
// Mode handler for SLIP gateway functionality
// ============================================================================

#include "slip_mode.h"
#include "globals.h"
#include "serial_io.h"
#include "display_menu.h"
#include "network.h"
#include "modem.h"
#include <EEPROM.h>

// Result code for AT OK response (matches enum in modem.cpp)
#define R_OK_STAT 0

// Debug output support
extern bool usbDebug;

// ============================================================================
// Global Contexts
// ============================================================================

SlipContext slipCtx;
NatContext natCtx;
SlipModeContext slipModeCtx;

// Menu definitions
String slipMenuDisp[] = { "MAIN", "Start Gateway", "Configure IP", "Port Forwards", "View Stats" };
String slipActiveMenuDisp[] = { "Stop Gateway", "Show Status", "View Stats" };
String portForwardMenuDisp[] = { "Back", "Add Forward", "Remove Forward", "List Forwards" };

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

// Forward declarations for internal functions
static void slipMenuLoop();
static void slipActiveLoop();
static void slipUpdateActiveDisplay();
static bool parseIPAddress(const String& str, IPAddress& ip);
static void slipAddPortForward();
static void slipRemovePortForward();
static void slipListPortForwards();

// ============================================================================
// SLIP Menu Function
// ============================================================================

void slipMenu(bool arrow) {
    menuMode = MODE_SLIP;

    if (slipModeCtx.state == SLIP_MODE_ACTIVE) {
        // Show active gateway menu
        showMenu("SLIP", slipActiveMenuDisp, 3, (arrow ? MENU_DISP : MENU_BOTH), 0);
    } else {
        // Show configuration menu
        showMenu("SLIP", slipMenuDisp, 5, (arrow ? MENU_DISP : MENU_BOTH), 0);
    }
}

// ============================================================================
// SLIP Loop Function (Main Mode Handler)
// ============================================================================

void slipLoop() {
    if (slipModeCtx.state == SLIP_MODE_ACTIVE) {
        // Active SLIP gateway - process packets
        slipActiveLoop();
    } else {
        // Menu mode
        slipMenuLoop();
    }
}

// ============================================================================
// SLIP Menu Loop (When Not Active)
// ============================================================================

static void slipMenuLoop() {
    int menuSel = -1;

    readSwitches();

    if (BTNUP) {
        menuIdx--;
        if (menuIdx < 0)
            menuIdx = menuCnt - 1;
        slipMenu(true);
    }
    else if (BTNDN) {
        menuIdx++;
        if (menuIdx > (menuCnt - 1))
            menuIdx = 0;
        slipMenu(true);
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
            // Return to main menu
            mainMenu(false);
        }
        else if (chr == 'S' || chr == 's' || menuSel == 1) {
            // Start SLIP Gateway
            enterSlipMode();
        }
        else if (chr == 'C' || chr == 'c' || menuSel == 2) {
            // Configure IP
            slipConfigureIP();
        }
        else if (chr == 'P' || chr == 'p' || menuSel == 3) {
            // Port Forwards
            slipConfigurePortForward();
        }
        else if (chr == 'V' || chr == 'v' || menuSel == 4) {
            // Statistics
            slipShowStatistics();
        }
        else {
            slipMenu(false);
        }
    }
}

// ============================================================================
// SLIP Active Loop (When Gateway Running)
// ============================================================================

static void slipActiveLoop() {
    // Check for button input to exit
    readSwitches();

    if (BTNBK) {
        waitSwitches();
        exitSlipMode();
        // Return to appropriate mode
        if (slipModeCtx.previousMenuMode == MODE_MODEM) {
            menuMode = MODE_MODEM;
            sendResult(R_OK_STAT);  // AT OK response
        } else {
            slipMenu(false);
        }
        return;
    }

    if (BTNUP || BTNDN) {
        waitSwitches();
        // Toggle status display
        slipShowStatus();
    }

    if (BTNEN) {
        waitSwitches();
        // Show menu for active state
        slipMenu(true);
    }

    // Check for escape commands on USB Serial ONLY (not PhysicalSerial)
    // PhysicalSerial carries binary SLIP data that shouldn't be interpreted as commands
    static char escapeBuffer[4] = {0};
    static int escapePos = 0;
    static unsigned long lastEscapeTime = 0;

    while (Serial.available()) {
        char c = Serial.read();

        // Check for +++ escape sequence
        if (c == '+') {
            if (millis() - lastEscapeTime > 1000) {
                escapePos = 0;
            }
            escapeBuffer[escapePos++] = c;
            lastEscapeTime = millis();

            if (escapePos >= 3) {
                // Exit to command mode
                SerialPrintLn("\r\nOK");
                exitSlipMode();
                if (slipModeCtx.previousMenuMode == MODE_MODEM) {
                    menuMode = MODE_MODEM;
                } else {
                    slipMenu(false);
                }
                return;
            }
        } else {
            escapePos = 0;
        }
    }

    // Process incoming SLIP frames from PhysicalSerial only
    while (PhysicalSerial.available()) {
        uint8_t byte = PhysicalSerial.read();
        int frameLen = slipReceiveByte(&slipCtx, byte);

        if (frameLen > 0) {
            // Complete IP packet received - process through NAT
            if (usbDebug) {
                UsbDebugPrint("");
                Serial.printf("SLIP: Received frame, %d bytes\r\n", frameLen);
            }
            natProcessPacket(&natCtx, slipCtx.rxBuffer, frameLen);
        }
    }

    // Poll NAT connections for incoming data from internet
    natPollConnections(&natCtx);

    // Check port forwards for incoming connections
    natCheckPortForwards(&natCtx);

    // Periodically clean up expired connections
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 10000) {
        natCleanupExpired(&natCtx);
        lastCleanup = millis();
    }

    // Periodically update display
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 1000) {
        slipUpdateActiveDisplay();
        lastDisplayUpdate = millis();
    }
}

// ============================================================================
// Enter SLIP Gateway Mode
// ============================================================================

void enterSlipMode() {
    // Save previous mode to return to on disconnect
    slipModeCtx.previousMenuMode = menuMode;

    // Switch to SLIP mode
    menuMode = MODE_SLIP;

    // Enable binary mode - suppress text output to PhysicalSerial
    // SLIP requires clean binary channel on RS232
    setBinaryMode(true);

    SerialPrintLn("\r\nEntering SLIP Gateway Mode...");

    // Load settings from EEPROM
    loadSlipSettings();

    // Initialize SLIP context
    slipInit(&slipCtx);

    // Initialize NAT context
    natInit(&natCtx);

    // Apply loaded configuration
    natCtx.slipIP = slipModeCtx.config.slipIP;
    natCtx.clientIP = slipModeCtx.config.clientIP;
    natCtx.subnetMask = slipModeCtx.config.subnetMask;
    natCtx.dnsServer = slipModeCtx.config.dnsServer;

    // Connect to WiFi if not already connected
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            SerialPrintLn("WiFi connection required for SLIP mode");
            showMessage("WiFi Required\nConnect First");
            delay(2000);
            return;
        }
    }

    slipModeCtx.state = SLIP_MODE_ACTIVE;

    // Display configuration
    SerialPrintLn("SLIP Gateway Active");
    SerialPrint("  Gateway IP: ");
    SerialPrintLn(ipToString(natCtx.slipIP));
    SerialPrint("  Client IP:  ");
    SerialPrintLn(ipToString(natCtx.clientIP));
    SerialPrint("  WiFi IP:    ");
    SerialPrintLn(ipToString(WiFi.localIP()));
    SerialPrintLn("\r\nPress +++ or BACK button to exit");

    slipShowStatus();
}

// ============================================================================
// Exit SLIP Gateway Mode
// ============================================================================

void exitSlipMode() {
    // Disable binary mode - restore text output to PhysicalSerial
    setBinaryMode(false);

    SerialPrintLn("\r\nExiting SLIP Gateway Mode...");

    // Shutdown NAT (closes all connections)
    natShutdown(&natCtx);

    // Reset SLIP state
    slipReset(&slipCtx);

    slipModeCtx.state = SLIP_MODE_IDLE;

    SerialPrintLn("SLIP Gateway Stopped");
}

// ============================================================================
// Show SLIP Status on Display
// ============================================================================

void slipShowStatus() {
    display.clearDisplay();

    // Header
    display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
    display.drawLine(1, 1, 126, 1, SSD1306_BLACK);
    display.drawLine(1, 14, 126, 14, SSD1306_BLACK);
    display.drawLine(1, 1, 1, 14, SSD1306_BLACK);
    display.drawLine(126, 1, 126, 14, SSD1306_BLACK);

    display.setCursor(4, 4);
    display.setTextColor(SSD1306_BLACK);
    display.print("SLIP Gateway");
    display.setTextColor(SSD1306_WHITE);

    // Status info
    display.setCursor(3, 19);
    display.print("GW: ");
    display.print(ipToString(natCtx.slipIP));

    display.setCursor(3, 29);
    display.print("CL: ");
    display.print(ipToString(natCtx.clientIP));

    display.setCursor(3, 39);
    display.print("WiFi: ");
    display.print(ipToString(WiFi.localIP()));

    // Connection counts
    display.setCursor(3, 53);
    display.print("TCP:");
    display.print(natGetActiveTcpCount(&natCtx));
    display.print(" UDP:");
    display.print(natGetActiveUdpCount(&natCtx));

    display.display();
    lastMenu = -1;  // Force menu redraw on exit
}

// ============================================================================
// Update Active Display (Called Periodically)
// ============================================================================

static void slipUpdateActiveDisplay() {
    // Just update the connection counts
    display.fillRect(3, 53, 124, 10, SSD1306_BLACK);
    display.setCursor(3, 53);
    display.print("TCP:");
    display.print(natGetActiveTcpCount(&natCtx));
    display.print(" UDP:");
    display.print(natGetActiveUdpCount(&natCtx));
    display.print(" ");
    display.print(natCtx.packetsToInternet + natCtx.packetsFromInternet);
    display.print("pkt");
    display.display();
}

// ============================================================================
// Show SLIP Statistics
// ============================================================================

void slipShowStatistics() {
    SerialPrintLn("\r\n-=-=- SLIP Statistics -=-=-");
    SerialPrint("SLIP Frames Received: ");
    SerialPrintLn(slipCtx.framesReceived);
    SerialPrint("SLIP Frames Sent:     ");
    SerialPrintLn(slipCtx.framesSent);
    SerialPrint("SLIP RX Errors:       ");
    SerialPrintLn(slipCtx.rxErrors);
    SerialPrintLn();
    SerialPrint("Packets to Internet:  ");
    SerialPrintLn(natCtx.packetsToInternet);
    SerialPrint("Packets from Internet:");
    SerialPrintLn(natCtx.packetsFromInternet);
    SerialPrint("Dropped Packets:      ");
    SerialPrintLn(natCtx.droppedPackets);
    SerialPrintLn();
    SerialPrint("Active TCP:           ");
    SerialPrintLn(natGetActiveTcpCount(&natCtx));
    SerialPrint("Active UDP:           ");
    SerialPrintLn(natGetActiveUdpCount(&natCtx));
    SerialPrint("Total TCP Connections:");
    SerialPrintLn(natCtx.tcpConnections);
    SerialPrint("Total UDP Sessions:   ");
    SerialPrintLn(natCtx.udpSessions);
    SerialPrintLn();

    showMessage("Stats shown\non serial");
    delay(1500);
    slipMenu(false);
}

// ============================================================================
// Load SLIP Settings from EEPROM
// ============================================================================

void loadSlipSettings() {
    uint8_t ip[4];

    // Load Gateway IP
    for (int i = 0; i < 4; i++) {
        ip[i] = EEPROM.read(SLIP_IP_ADDRESS + i);
    }
    if (ip[0] == 255 || ip[0] == 0) {
        // Invalid - use default
        slipModeCtx.config.slipIP = IPAddress(192, 168, 7, 1);
    } else {
        slipModeCtx.config.slipIP = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }

    // Load Client IP
    for (int i = 0; i < 4; i++) {
        ip[i] = EEPROM.read(SLIP_CLIENT_IP_ADDRESS + i);
    }
    if (ip[0] == 255 || ip[0] == 0) {
        slipModeCtx.config.clientIP = IPAddress(192, 168, 7, 2);
    } else {
        slipModeCtx.config.clientIP = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }

    // Load Subnet Mask
    for (int i = 0; i < 4; i++) {
        ip[i] = EEPROM.read(SLIP_SUBNET_ADDRESS + i);
    }
    if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) {
        slipModeCtx.config.subnetMask = IPAddress(255, 255, 255, 0);
    } else if (ip[0] == 0) {
        slipModeCtx.config.subnetMask = IPAddress(255, 255, 255, 0);
    } else {
        slipModeCtx.config.subnetMask = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }

    // Load DNS Server
    for (int i = 0; i < 4; i++) {
        ip[i] = EEPROM.read(SLIP_DNS_ADDRESS + i);
    }
    if (ip[0] == 255 || ip[0] == 0) {
        slipModeCtx.config.dnsServer = IPAddress(8, 8, 8, 8);
    } else {
        slipModeCtx.config.dnsServer = IPAddress(ip[0], ip[1], ip[2], ip[3]);
    }

    // Load port forwards
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        int addr = SLIP_PORTFWD_BASE + (i * SLIP_PORTFWD_SIZE);
        uint8_t active = EEPROM.read(addr);
        if (active == 1) {
            uint8_t proto = EEPROM.read(addr + 1);
            uint16_t extPort = (EEPROM.read(addr + 2) << 8) | EEPROM.read(addr + 3);
            uint16_t intPort = (EEPROM.read(addr + 4) << 8) | EEPROM.read(addr + 5);
            // Internal IP is assumed to be clientIP for simplicity
            // Can extend to store full IP if needed
        }
    }

    slipModeCtx.configChanged = false;
}

// ============================================================================
// Save SLIP Settings to EEPROM
// ============================================================================

void saveSlipSettings() {
    // Save Gateway IP
    for (int i = 0; i < 4; i++) {
        EEPROM.write(SLIP_IP_ADDRESS + i, slipModeCtx.config.slipIP[i]);
    }

    // Save Client IP
    for (int i = 0; i < 4; i++) {
        EEPROM.write(SLIP_CLIENT_IP_ADDRESS + i, slipModeCtx.config.clientIP[i]);
    }

    // Save Subnet Mask
    for (int i = 0; i < 4; i++) {
        EEPROM.write(SLIP_SUBNET_ADDRESS + i, slipModeCtx.config.subnetMask[i]);
    }

    // Save DNS Server
    for (int i = 0; i < 4; i++) {
        EEPROM.write(SLIP_DNS_ADDRESS + i, slipModeCtx.config.dnsServer[i]);
    }

    EEPROM.commit();
    slipModeCtx.configChanged = false;
}

// ============================================================================
// Configure IP Addresses - Wizard Style
// ============================================================================

void slipConfigureIP() {
    loadSlipSettings();  // Load current settings

    SerialPrintLn("\r\n-=-=- SLIP IP Configuration Wizard -=-=-");
    SerialPrintLn("Press Enter to keep current value, or type new value");
    SerialPrintLn();

    // Prompt for Gateway IP
    String gatewayPrompt = "Gateway IP: ";
    String currentGateway = ipToString(slipModeCtx.config.slipIP);
    String newGateway = prompt(gatewayPrompt, currentGateway);

    IPAddress newGatewayIP;
    if (newGateway != "" && parseIPAddress(newGateway, newGatewayIP)) {
        slipModeCtx.config.slipIP = newGatewayIP;
        SerialPrintLn("Gateway IP updated");
    } else if (newGateway != currentGateway && newGateway != "") {
        SerialPrintLn("Invalid IP format - keeping current");
    }

    // Prompt for Client IP
    String clientPrompt = "Client IP: ";
    String currentClient = ipToString(slipModeCtx.config.clientIP);
    String newClient = prompt(clientPrompt, currentClient);

    IPAddress newClientIP;
    if (newClient != "" && parseIPAddress(newClient, newClientIP)) {
        slipModeCtx.config.clientIP = newClientIP;
        SerialPrintLn("Client IP updated");
    } else if (newClient != currentClient && newClient != "") {
        SerialPrintLn("Invalid IP format - keeping current");
    }

    // Prompt for DNS Server
    String dnsPrompt = "DNS Server: ";
    String currentDNS = ipToString(slipModeCtx.config.dnsServer);
    String newDNS = prompt(dnsPrompt, currentDNS);

    IPAddress newDNSIP;
    if (newDNS != "" && parseIPAddress(newDNS, newDNSIP)) {
        slipModeCtx.config.dnsServer = newDNSIP;
        SerialPrintLn("DNS server updated");
    } else if (newDNS != currentDNS && newDNS != "") {
        SerialPrintLn("Invalid IP format - keeping current");
    }

    // Save settings
    SerialPrintLn("\r\nSaving settings to EEPROM...");
    saveSlipSettings();
    SerialPrintLn("Configuration saved!");

    // Display summary
    SerialPrintLn("\r\nNew Configuration:");
    SerialPrint("  Gateway IP: ");
    SerialPrintLn(ipToString(slipModeCtx.config.slipIP));
    SerialPrint("  Client IP:  ");
    SerialPrintLn(ipToString(slipModeCtx.config.clientIP));
    SerialPrint("  DNS Server: ");
    SerialPrintLn(ipToString(slipModeCtx.config.dnsServer));

    showMessage("Config saved!\nSee serial");
    delay(2000);
    slipMenu(false);
}

// ============================================================================
// Configure Port Forwards - Submenu
// ============================================================================

void slipConfigurePortForward() {
    // Show submenu and wait for selection
    SerialPrintLn("\r\n-=-=- Port Forwarding Menu -=-=-");
    SerialPrintLn("  A) Add Forward");
    SerialPrintLn("  R) Remove Forward");
    SerialPrintLn("  L) List Forwards");
    SerialPrintLn("  B) Back to SLIP Menu");
    SerialPrintLn();

    showMessage("Port Fwd Menu\nSee serial");

    // Wait for selection
    String choice = prompt("Select option: ", "");
    choice.toUpperCase();

    if (choice == "A" || choice == "ADD") {
        slipAddPortForward();
    }
    else if (choice == "R" || choice == "REMOVE") {
        slipRemovePortForward();
    }
    else if (choice == "L" || choice == "LIST") {
        slipListPortForwards();
    }
    else {
        // Back or invalid - return to SLIP menu
        slipMenu(false);
    }
}

// ============================================================================
// Add Port Forward - Wizard
// ============================================================================

static void slipAddPortForward() {
    loadSlipSettings();  // Ensure we have current settings

    SerialPrintLn("\r\n-=-=- Add Port Forward -=-=-");
    SerialPrintLn();

    // Prompt for protocol (TCP or UDP)
    String protoPrompt = "Protocol (TCP/UDP): ";
    String protoStr = prompt(protoPrompt, "TCP");
    protoStr.toUpperCase();

    if (protoStr != "TCP" && protoStr != "UDP") {
        SerialPrintLn("Invalid protocol - must be TCP or UDP");
        showMessage("Invalid\nprotocol");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    uint8_t proto = (protoStr == "TCP") ? IP_PROTO_TCP : IP_PROTO_UDP;

    // Prompt for external port
    String extPortPrompt = "External Port (on ESP32): ";
    String extPortStr = prompt(extPortPrompt, "");

    if (extPortStr == "") {
        SerialPrintLn("Canceled");
        showMessage("Canceled");
        delay(1000);
        slipConfigurePortForward();
        return;
    }

    uint16_t extPort = extPortStr.toInt();
    if (extPort < 1 || extPort > 65535) {
        SerialPrintLn("Invalid port number");
        showMessage("Invalid port");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    // Prompt for internal IP (default to client IP)
    String intIPPrompt = "Internal IP: ";
    String currentClient = ipToString(slipModeCtx.config.clientIP);
    String intIPStr = prompt(intIPPrompt, currentClient);

    IPAddress intIP;
    if (!parseIPAddress(intIPStr, intIP)) {
        SerialPrintLn("Invalid IP format");
        showMessage("Invalid IP");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    // Prompt for internal port
    String intPortPrompt = "Internal Port (on client): ";
    String intPortStr = prompt(intPortPrompt, extPortStr);  // Default to same as external

    if (intPortStr == "") {
        SerialPrintLn("Canceled");
        showMessage("Canceled");
        delay(1000);
        slipConfigurePortForward();
        return;
    }

    uint16_t intPort = intPortStr.toInt();
    if (intPort < 1 || intPort > 65535) {
        SerialPrintLn("Invalid port number");
        showMessage("Invalid port");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    // Add the forward
    int idx = natAddPortForward(&natCtx, proto, extPort, intIP, intPort);

    if (idx >= 0) {
        SerialPrintLn("\r\nPort forward added:");
        SerialPrint("  ");
        SerialPrint(protoStr);
        SerialPrint(" ");
        SerialPrint(extPort);
        SerialPrint(" -> ");
        SerialPrint(ipToString(intIP));
        SerialPrint(":");
        SerialPrintLn(intPort);

        showMessage("Forward\nadded!");
        delay(1500);
    } else {
        SerialPrintLn("Failed to add forward - table full");
        showMessage("Table full!\nRemove some");
        delay(2000);
    }

    slipConfigurePortForward();
}

// ============================================================================
// Remove Port Forward - Wizard
// ============================================================================

static void slipRemovePortForward() {
    SerialPrintLn("\r\n-=-=- Remove Port Forward -=-=-");
    SerialPrintLn("Active port forwards:");

    int count = 0;
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (natCtx.portForwards[i].active) {
            SerialPrint("  ");
            SerialPrint(i);
            SerialPrint(": ");
            SerialPrint(natCtx.portForwards[i].protocol == IP_PROTO_TCP ? "TCP" : "UDP");
            SerialPrint(" ");
            SerialPrint(natCtx.portForwards[i].externalPort);
            SerialPrint(" -> ");
            SerialPrint(ipToString(natCtx.portForwards[i].internalIP));
            SerialPrint(":");
            SerialPrintLn(natCtx.portForwards[i].internalPort);
            count++;
        }
    }

    if (count == 0) {
        SerialPrintLn("  (none configured)");
        showMessage("No forwards\nto remove");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    SerialPrintLn();
    String indexPrompt = "Index to remove: ";
    String indexStr = prompt(indexPrompt, "");

    if (indexStr == "") {
        SerialPrintLn("Canceled");
        showMessage("Canceled");
        delay(1000);
        slipConfigurePortForward();
        return;
    }

    int idx = indexStr.toInt();
    if (idx < 0 || idx >= NAT_MAX_PORT_FORWARDS || !natCtx.portForwards[idx].active) {
        SerialPrintLn("Invalid index");
        showMessage("Invalid index");
        delay(1500);
        slipConfigurePortForward();
        return;
    }

    natRemovePortForward(&natCtx, idx);
    SerialPrintLn("Port forward removed");
    showMessage("Forward\nremoved!");
    delay(1500);

    slipConfigurePortForward();
}

// ============================================================================
// List Port Forwards
// ============================================================================

static void slipListPortForwards() {
    SerialPrintLn("\r\n-=-=- Port Forwarding -=-=-");
    SerialPrintLn("Active port forwards:");

    int count = 0;
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (natCtx.portForwards[i].active) {
            SerialPrint("  ");
            SerialPrint(i);
            SerialPrint(": ");
            SerialPrint(natCtx.portForwards[i].protocol == IP_PROTO_TCP ? "TCP" : "UDP");
            SerialPrint(" ");
            SerialPrint(natCtx.portForwards[i].externalPort);
            SerialPrint(" -> ");
            SerialPrint(ipToString(natCtx.portForwards[i].internalIP));
            SerialPrint(":");
            SerialPrintLn(natCtx.portForwards[i].internalPort);
            count++;
        }
    }

    if (count == 0) {
        SerialPrintLn("  (none configured)");
    }

    showMessage("Forwards shown\non serial");
    delay(2000);
    slipConfigurePortForward();
}

// ============================================================================
// Set Default SLIP Configuration
// ============================================================================

void slipDefaultConfig() {
    slipModeCtx.config.slipIP = IPAddress(192, 168, 7, 1);
    slipModeCtx.config.clientIP = IPAddress(192, 168, 7, 2);
    slipModeCtx.config.subnetMask = IPAddress(255, 255, 255, 0);
    slipModeCtx.config.dnsServer = IPAddress(8, 8, 8, 8);
    slipModeCtx.configChanged = true;
}

// ============================================================================
// Parse IP Address from String
// ============================================================================

static bool parseIPAddress(const String& str, IPAddress& ip) {
    int parts[4];
    int partIdx = 0;
    int num = 0;

    for (int i = 0; i <= str.length() && partIdx < 4; i++) {
        char c = (i < str.length()) ? str[i] : '.';
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
        } else if (c == '.') {
            if (num > 255) return false;
            parts[partIdx++] = num;
            num = 0;
        } else {
            return false;
        }
    }

    if (partIdx != 4) return false;

    ip = IPAddress(parts[0], parts[1], parts[2], parts[3]);
    return true;
}

// ============================================================================
// Handle SLIP AT Commands
// ============================================================================

bool handleSlipCommand(String& cmd, String& upCmd) {
    // AT$SLIP - Enter SLIP mode
    if (upCmd == "ATSLIP" || upCmd == "AT$SLIP") {
        enterSlipMode();
        return true;
    }

    // AT$SLIPSTAT - Show statistics
    if (upCmd == "AT$SLIPSTAT") {
        slipShowStatistics();
        return true;
    }

    // AT$SLIPIP=x.x.x.x - Set gateway IP
    if (upCmd.startsWith("AT$SLIPIP=")) {
        String ipStr = cmd.substring(10);
        IPAddress newIP;
        if (parseIPAddress(ipStr, newIP)) {
            slipModeCtx.config.slipIP = newIP;
            slipModeCtx.configChanged = true;
            SerialPrintLn("Gateway IP set to " + ipToString(newIP));
            return true;
        } else {
            SerialPrintLn("Invalid IP format");
            return true;
        }
    }

    // AT$SLIPCLIENT=x.x.x.x - Set client IP
    if (upCmd.startsWith("AT$SLIPCLIENT=")) {
        String ipStr = cmd.substring(14);
        IPAddress newIP;
        if (parseIPAddress(ipStr, newIP)) {
            slipModeCtx.config.clientIP = newIP;
            slipModeCtx.configChanged = true;
            SerialPrintLn("Client IP set to " + ipToString(newIP));
            return true;
        } else {
            SerialPrintLn("Invalid IP format");
            return true;
        }
    }

    // AT$SLIPDNS=x.x.x.x - Set DNS server
    if (upCmd.startsWith("AT$SLIPDNS=")) {
        String ipStr = cmd.substring(11);
        IPAddress newIP;
        if (parseIPAddress(ipStr, newIP)) {
            slipModeCtx.config.dnsServer = newIP;
            slipModeCtx.configChanged = true;
            SerialPrintLn("DNS server set to " + ipToString(newIP));
            return true;
        } else {
            SerialPrintLn("Invalid IP format");
            return true;
        }
    }

    // AT$SLIPFWD=TCP,extport,intport - Add port forward
    if (upCmd.startsWith("AT$SLIPFWD=")) {
        String params = cmd.substring(11);
        // Parse: TCP,80,8080 or UDP,53,53
        int comma1 = params.indexOf(',');
        int comma2 = params.indexOf(',', comma1 + 1);

        if (comma1 > 0 && comma2 > comma1) {
            String protoStr = params.substring(0, comma1);
            protoStr.toUpperCase();
            uint16_t extPort = params.substring(comma1 + 1, comma2).toInt();
            uint16_t intPort = params.substring(comma2 + 1).toInt();

            uint8_t proto = (protoStr == "TCP") ? IP_PROTO_TCP : IP_PROTO_UDP;

            int idx = natAddPortForward(&natCtx, proto, extPort,
                                        slipModeCtx.config.clientIP, intPort);
            if (idx >= 0) {
                SerialPrint("Port forward added: ");
                SerialPrint(protoStr);
                SerialPrint(" ");
                SerialPrint(extPort);
                SerialPrint(" -> ");
                SerialPrint(ipToString(slipModeCtx.config.clientIP));
                SerialPrint(":");
                SerialPrintLn(intPort);
            } else {
                SerialPrintLn("Failed - table full");
            }
            return true;
        } else {
            SerialPrintLn("Format: AT$SLIPFWD=TCP,extport,intport");
            return true;
        }
    }

    // AT$SLIPFWDDEL=index - Remove port forward
    if (upCmd.startsWith("AT$SLIPFWDDEL=")) {
        int idx = cmd.substring(14).toInt();
        natRemovePortForward(&natCtx, idx);
        SerialPrintLn("Port forward removed");
        return true;
    }

    // AT$SLIPSHOW - Show current configuration
    if (upCmd == "AT$SLIPSHOW") {
        SerialPrintLn("\r\n-=-=- SLIP Configuration -=-=-");
        SerialPrint("Gateway IP: ");
        SerialPrintLn(ipToString(slipModeCtx.config.slipIP));
        SerialPrint("Client IP:  ");
        SerialPrintLn(ipToString(slipModeCtx.config.clientIP));
        SerialPrint("Subnet:     ");
        SerialPrintLn(ipToString(slipModeCtx.config.subnetMask));
        SerialPrint("DNS Server: ");
        SerialPrintLn(ipToString(slipModeCtx.config.dnsServer));
        return true;
    }

    return false;  // Command not handled
}

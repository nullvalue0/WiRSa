// ============================================================================
// PPP NAT Engine
// ============================================================================
// Network Address Translation for PPP gateway
// Independent implementation (not shared with SLIP)
// ============================================================================

#ifndef PPP_NAT_H
#define PPP_NAT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

// ============================================================================
// NAT Configuration Constants
// ============================================================================

// Connection table sizes (slightly smaller than SLIP to save RAM)
#define PPP_NAT_MAX_TCP     12      // Max concurrent TCP connections
#define PPP_NAT_MAX_UDP     32      // Max concurrent UDP sessions
#define PPP_NAT_MAX_ICMP    8       // Max concurrent ICMP (ping) sessions
#define PPP_NAT_MAX_PORT_FORWARDS 8 // Max port forwarding rules (matches SLIP)

// Timeout values (milliseconds)
#define PPP_NAT_TCP_TIMEOUT_MS      1800000 // 30 minutes for established TCP (IRC, etc)
#define PPP_NAT_UDP_TIMEOUT_MS      30000   // 30 seconds for UDP
#define PPP_NAT_TCP_SYN_TIMEOUT_MS  30000   // 30 seconds for connection setup
#define PPP_NAT_ICMP_TIMEOUT_MS     10000   // 10 seconds for ICMP
#define PPP_NAT_TCP_KEEPALIVE_MS    300000  // 5 minutes between keepalives

// Stall timeout - close connection if no ACK received within this time
// Set to 60s to handle very slow serial links (was 10s which was too aggressive)
#define PPP_NAT_TCP_STALL_TIMEOUT_MS 60000

// Ephemeral port range
#define PPP_NAT_PORT_START  20000
#define PPP_NAT_PORT_END    50000

// ============================================================================
// IP Protocol Numbers
// ============================================================================

#define PPP_IP_PROTO_ICMP   1
#define PPP_IP_PROTO_TCP    6
#define PPP_IP_PROTO_UDP    17

// ============================================================================
// TCP Flags
// ============================================================================

#define PPP_TCP_FLAG_FIN    0x01
#define PPP_TCP_FLAG_SYN    0x02
#define PPP_TCP_FLAG_RST    0x04
#define PPP_TCP_FLAG_PSH    0x08
#define PPP_TCP_FLAG_ACK    0x10
#define PPP_TCP_FLAG_URG    0x20

// ============================================================================
// TCP NAT Connection States
// ============================================================================

enum PppNatTcpState {
    PPP_NAT_TCP_CLOSED,
    PPP_NAT_TCP_SYN_SENT,
    PPP_NAT_TCP_ESTABLISHED,
    PPP_NAT_TCP_FIN_WAIT,
    PPP_NAT_TCP_CLOSING
};

// ============================================================================
// NAT TCP Connection Entry
// ============================================================================

struct PppNatTcpEntry {
    bool active;
    PppNatTcpState state;

    // Original endpoint (PPP client side)
    IPAddress srcIP;
    uint16_t srcPort;

    // Remote endpoint (internet side)
    IPAddress dstIP;
    uint16_t dstPort;

    // Local NAT port
    uint16_t natPort;

    // TCP sequence tracking
    uint32_t clientSeq;
    uint32_t clientAck;
    uint32_t serverSeq;
    uint32_t serverAck;

    // WiFiClient for this connection
    WiFiClient* client;

    // Timestamps
    unsigned long lastActivity;
    unsigned long lastKeepalive;
    unsigned long lastServerData;  // Last time we received data FROM the internet server
    unsigned long created;
};

// ============================================================================
// NAT UDP Session Entry
// ============================================================================

struct PppNatUdpEntry {
    bool active;

    IPAddress srcIP;
    uint16_t srcPort;

    IPAddress dstIP;
    uint16_t dstPort;

    uint16_t natPort;

    unsigned long lastActivity;
};

// ============================================================================
// NAT ICMP Session Entry
// ============================================================================

struct PppNatIcmpEntry {
    bool active;

    IPAddress srcIP;        // Original source (PPP client)
    IPAddress dstIP;        // Destination (ping target)
    uint16_t id;            // ICMP identifier
    uint16_t sequence;      // ICMP sequence number

    unsigned long lastActivity;
};

// ============================================================================
// IP Header Structure (20 bytes minimum)
// ============================================================================

struct __attribute__((packed)) PppIpHeader {
    uint8_t  versionIhl;
    uint8_t  tos;
    uint16_t totalLength;
    uint16_t id;
    uint16_t flagsFragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t srcIP;
    uint32_t dstIP;
};

// ============================================================================
// TCP Header Structure (20 bytes minimum)
// ============================================================================

struct __attribute__((packed)) PppTcpHeader {
    uint16_t srcPort;
    uint16_t dstPort;
    uint32_t seqNum;
    uint32_t ackNum;
    uint8_t  dataOffset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgentPtr;
};

// ============================================================================
// UDP Header Structure (8 bytes)
// ============================================================================

struct __attribute__((packed)) PppUdpHeader {
    uint16_t srcPort;
    uint16_t dstPort;
    uint16_t length;
    uint16_t checksum;
};

// ============================================================================
// ICMP Header Structure
// ============================================================================

struct __attribute__((packed)) PppIcmpHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
};

#define PPP_ICMP_ECHO_REPLY     0
#define PPP_ICMP_ECHO_REQUEST   8

// ============================================================================
// Port Forward Entry (shared format with SLIP for EEPROM compatibility)
// ============================================================================

struct PppPortForwardEntry {
    bool active;
    uint8_t protocol;       // PPP_IP_PROTO_TCP or PPP_IP_PROTO_UDP
    uint16_t externalPort;  // Port on ESP32 (WAN side)
    IPAddress internalIP;   // Destination IP (vintage computer)
    uint16_t internalPort;  // Destination port
};

// ============================================================================
// PPP NAT Context
// ============================================================================

struct PppNatContext {
    // IP Configuration
    IPAddress gatewayIP;    // ESP32's IP on PPP side
    IPAddress clientIP;     // PPP client's assigned IP
    IPAddress subnetMask;
    IPAddress dnsServer;

    // Connection tables
    PppNatTcpEntry tcpTable[PPP_NAT_MAX_TCP];
    PppNatUdpEntry udpTable[PPP_NAT_MAX_UDP];
    PppNatIcmpEntry icmpTable[PPP_NAT_MAX_ICMP];

    // Port forwarding rules (shared EEPROM storage with SLIP)
    PppPortForwardEntry portForwards[PPP_NAT_MAX_PORT_FORWARDS];

    // UDP socket for outbound NAT
    WiFiUDP udpSocket;
    bool udpSocketBound;

    // ICMP socket for ping NAT (lwIP raw socket)
    void* icmpPcb;          // struct raw_pcb* (opaque to avoid header dependency)

    // Port forwarding servers (TCP listeners)
    WiFiServer* tcpForwardServers[PPP_NAT_MAX_PORT_FORWARDS];

    // Ephemeral port counter
    uint16_t nextPort;

    // Statistics
    uint32_t packetsToInternet;
    uint32_t packetsFromInternet;
    uint32_t droppedPackets;
    uint32_t tcpConnections;
    uint32_t udpSessions;
    uint32_t icmpPackets;
};

// ============================================================================
// Forward declaration
// ============================================================================
struct PppContext;

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize NAT context
void pppNatInit(PppNatContext* ctx);

// Shutdown NAT - close all connections
void pppNatShutdown(PppNatContext* ctx);

// Set IP configuration
void pppNatSetIPs(PppNatContext* ctx, IPAddress gateway, IPAddress client,
                  IPAddress subnet, IPAddress dns);

// Process incoming IP packet from PPP client
void pppNatProcessPacket(PppNatContext* ctx, PppContext* ppp,
                         uint8_t* packet, uint16_t length);

// Poll all active connections for incoming data
// Returns number of packets sent back to PPP client
int pppNatPollConnections(PppNatContext* ctx, PppContext* ppp);

// Process pending ICMP replies (call from main loop)
void pppNatProcessPendingIcmp(PppNatContext* ctx, PppContext* ppp);

// Clean up expired connections
void pppNatCleanupExpired(PppNatContext* ctx);

// Utility functions
uint16_t pppIpChecksum(const uint8_t* data, uint16_t length);
void pppIpRecalcChecksum(PppIpHeader* ip);
uint16_t pppTcpUdpChecksum(PppIpHeader* ip, const uint8_t* payload, uint16_t payloadLen);

// Send IP packet back to PPP client
void pppNatSendToClient(PppNatContext* ctx, PppContext* ppp,
                        uint8_t* packet, uint16_t length);

// Get active connection counts
int pppNatGetActiveTcpCount(PppNatContext* ctx);
int pppNatGetActiveUdpCount(PppNatContext* ctx);

// Port forwarding management (uses shared EEPROM storage with SLIP)
int pppNatAddPortForward(PppNatContext* ctx, uint8_t proto, uint16_t extPort,
                         IPAddress intIP, uint16_t intPort, bool startServer = false);
void pppNatRemovePortForward(PppNatContext* ctx, int index);
void pppNatStartPortForwardServers(PppNatContext* ctx);
void pppNatStopPortForwardServers(PppNatContext* ctx);

// Port forward persistence (EEPROM - shared storage with SLIP)
void pppSavePortForwards(PppNatContext* ctx);
void pppLoadPortForwards(PppNatContext* ctx);

// Check for incoming port-forwarded connections
void pppNatCheckPortForwards(PppNatContext* ctx, PppContext* ppp);

#endif // PPP_NAT_H

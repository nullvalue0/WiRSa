#ifndef SLIP_NAT_H
#define SLIP_NAT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>

// ============================================================================
// NAT Configuration Constants
// ============================================================================

// Connection table sizes (ESP32 RAM limited - ~320KB total)
#define NAT_MAX_TCP_CONNECTIONS  16    // Max concurrent TCP connections
#define NAT_MAX_UDP_SESSIONS     8     // Max concurrent UDP sessions
#define NAT_MAX_PORT_FORWARDS    8     // Max port forwarding rules

// Timeout values (milliseconds)
#define NAT_TCP_TIMEOUT_MS       300000  // 5 minutes for TCP
#define NAT_UDP_TIMEOUT_MS       60000   // 1 minute for UDP
#define NAT_TCP_SYN_TIMEOUT_MS   30000   // 30 seconds for connection setup

// Ephemeral port range for NAT
#define NAT_PORT_START           10000
#define NAT_PORT_END             60000

// ============================================================================
// IP Protocol Numbers
// ============================================================================

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

// ============================================================================
// TCP Flags
// ============================================================================

#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10
#define TCP_FLAG_URG    0x20

// ============================================================================
// TCP NAT Connection States
// ============================================================================

enum NatTcpState {
    NAT_TCP_CLOSED,         // No connection
    NAT_TCP_SYN_SENT,       // SYN received, connecting to remote
    NAT_TCP_ESTABLISHED,    // Connection active
    NAT_TCP_FIN_WAIT,       // FIN received, closing
    NAT_TCP_CLOSING         // Waiting for final cleanup
};

// ============================================================================
// NAT TCP Connection Entry
// ============================================================================

struct NatTcpEntry {
    bool active;
    NatTcpState state;

    // Original endpoint (vintage computer side)
    IPAddress srcIP;        // Vintage computer's IP
    uint16_t srcPort;       // Vintage computer's port

    // Remote endpoint (internet side)
    IPAddress dstIP;        // Remote server IP
    uint16_t dstPort;       // Remote server port

    // Local NAT port (ESP32's ephemeral port)
    uint16_t natPort;

    // TCP sequence tracking (for proper ACK handling)
    uint32_t clientSeq;     // Last sequence from client
    uint32_t clientAck;     // Last ACK from client
    uint32_t serverSeq;     // Last sequence from server
    uint32_t serverAck;     // Last ACK from server

    // WiFiClient for this connection
    WiFiClient* client;

    // RX buffer for data from server
    uint8_t* rxBuffer;
    uint16_t rxBufferLen;
    uint16_t rxBufferSize;

    // Timestamps
    unsigned long lastActivity;
    unsigned long created;
};

// ============================================================================
// NAT UDP Session Entry
// ============================================================================

struct NatUdpEntry {
    bool active;

    // Original endpoint
    IPAddress srcIP;
    uint16_t srcPort;

    // Remote endpoint
    IPAddress dstIP;
    uint16_t dstPort;

    // Local NAT port
    uint16_t natPort;

    // Timestamp
    unsigned long lastActivity;
};

// ============================================================================
// Port Forward Entry
// ============================================================================

struct PortForwardEntry {
    bool active;
    uint8_t protocol;       // IP_PROTO_TCP or IP_PROTO_UDP
    uint16_t externalPort;  // Port on ESP32 (WAN side)
    IPAddress internalIP;   // Destination IP (vintage computer)
    uint16_t internalPort;  // Destination port
};

// ============================================================================
// NAT Gateway Context
// ============================================================================

struct NatContext {
    // IP Configuration (SLIP side)
    IPAddress slipIP;       // ESP32's IP on SLIP side (e.g., 192.168.7.1)
    IPAddress clientIP;     // Vintage computer's IP (e.g., 192.168.7.2)
    IPAddress subnetMask;   // Subnet mask (e.g., 255.255.255.0)
    IPAddress dnsServer;    // DNS server to use

    // Connection tables
    NatTcpEntry tcpTable[NAT_MAX_TCP_CONNECTIONS];
    NatUdpEntry udpTable[NAT_MAX_UDP_SESSIONS];

    // Port forwarding rules
    PortForwardEntry portForwards[NAT_MAX_PORT_FORWARDS];

    // UDP socket for outbound NAT
    WiFiUDP udpSocket;
    bool udpSocketBound;

    // Port forwarding servers
    WiFiServer* tcpForwardServers[NAT_MAX_PORT_FORWARDS];

    // Ephemeral port counter
    uint16_t nextPort;

    // Statistics
    uint32_t packetsToInternet;
    uint32_t packetsFromInternet;
    uint32_t droppedPackets;
    uint32_t tcpConnections;
    uint32_t udpSessions;
};

// ============================================================================
// IP Header Structure (20 bytes minimum)
// ============================================================================

struct __attribute__((packed)) IpHeader {
    uint8_t  versionIhl;    // Version (4 bits) + IHL (4 bits)
    uint8_t  tos;           // Type of service
    uint16_t totalLength;   // Total length (big endian)
    uint16_t id;            // Identification
    uint16_t flagsFragment; // Flags (3 bits) + Fragment offset (13 bits)
    uint8_t  ttl;           // Time to live
    uint8_t  protocol;      // Protocol (TCP=6, UDP=17, ICMP=1)
    uint16_t checksum;      // Header checksum
    uint32_t srcIP;         // Source IP (big endian)
    uint32_t dstIP;         // Destination IP (big endian)
};

// ============================================================================
// TCP Header Structure (20 bytes minimum)
// ============================================================================

struct __attribute__((packed)) TcpHeader {
    uint16_t srcPort;       // Source port (big endian)
    uint16_t dstPort;       // Destination port (big endian)
    uint32_t seqNum;        // Sequence number
    uint32_t ackNum;        // Acknowledgment number
    uint8_t  dataOffset;    // Data offset (4 bits) + reserved (4 bits)
    uint8_t  flags;         // Control flags
    uint16_t window;        // Window size
    uint16_t checksum;      // Checksum
    uint16_t urgentPtr;     // Urgent pointer
};

// ============================================================================
// UDP Header Structure (8 bytes)
// ============================================================================

struct __attribute__((packed)) UdpHeader {
    uint16_t srcPort;       // Source port (big endian)
    uint16_t dstPort;       // Destination port (big endian)
    uint16_t length;        // Length (big endian)
    uint16_t checksum;      // Checksum
};

// ============================================================================
// ICMP Header Structure
// ============================================================================

struct __attribute__((packed)) IcmpHeader {
    uint8_t  type;          // Message type
    uint8_t  code;          // Message code
    uint16_t checksum;      // Checksum
    uint16_t id;            // Identifier
    uint16_t sequence;      // Sequence number
};

// ICMP types
#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize NAT context
void natInit(NatContext* ctx);

// Shutdown NAT - close all connections
void natShutdown(NatContext* ctx);

// Process incoming IP packet from vintage computer
void natProcessPacket(NatContext* ctx, uint8_t* packet, uint16_t length);

// Poll all active connections for incoming data
// Returns number of packets sent back to vintage computer
int natPollConnections(NatContext* ctx);

// Clean up expired connections
void natCleanupExpired(NatContext* ctx);

// Port forwarding management
int natAddPortForward(NatContext* ctx, uint8_t proto, uint16_t extPort,
                      IPAddress intIP, uint16_t intPort);
void natRemovePortForward(NatContext* ctx, int index);

// Check for incoming port-forwarded connections
void natCheckPortForwards(NatContext* ctx);

// Utility functions
uint16_t ipChecksum(const uint8_t* data, uint16_t length);
void ipRecalcChecksum(IpHeader* ip);
uint16_t tcpUdpChecksum(IpHeader* ip, const uint8_t* payload, uint16_t payloadLen);

// Send IP packet back to vintage computer via SLIP
void natSendToClient(NatContext* ctx, uint8_t* packet, uint16_t length);

// Get active connection counts
int natGetActiveTcpCount(NatContext* ctx);
int natGetActiveUdpCount(NatContext* ctx);

#endif // SLIP_NAT_H

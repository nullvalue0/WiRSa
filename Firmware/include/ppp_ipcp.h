// ============================================================================
// PPP IP Control Protocol (IPCP) - RFC 1332
// ============================================================================
// Handles IP address assignment and configuration
// ============================================================================

#ifndef PPP_IPCP_H
#define PPP_IPCP_H

#include <Arduino.h>
#include <WiFi.h>
#include "ppp.h"

// ============================================================================
// IPCP Code Values (same as LCP)
// ============================================================================

#define IPCP_CONFIGURE_REQUEST   1
#define IPCP_CONFIGURE_ACK       2
#define IPCP_CONFIGURE_NAK       3
#define IPCP_CONFIGURE_REJECT    4
#define IPCP_TERMINATE_REQUEST   5
#define IPCP_TERMINATE_ACK       6

// ============================================================================
// IPCP Option Types
// ============================================================================

#define IPCP_OPT_IP_ADDRESSES    1   // Deprecated (RFC 1172)
#define IPCP_OPT_IP_COMPRESSION  2   // Van Jacobson compression
#define IPCP_OPT_IP_ADDRESS      3   // IP address
#define IPCP_OPT_PRIMARY_DNS     129 // Primary DNS (Microsoft extension)
#define IPCP_OPT_PRIMARY_NBNS    130 // Primary NBNS/WINS (Microsoft)
#define IPCP_OPT_SECONDARY_DNS   131 // Secondary DNS (Microsoft extension)
#define IPCP_OPT_SECONDARY_NBNS  132 // Secondary NBNS/WINS (Microsoft)

// ============================================================================
// IPCP States
// ============================================================================

enum IpcpState {
    IPCP_STATE_INITIAL,     // Not started
    IPCP_STATE_STARTING,    // Waiting to send config
    IPCP_STATE_CLOSED,      // Closed
    IPCP_STATE_STOPPED,     // Stopped
    IPCP_STATE_CLOSING,     // Closing
    IPCP_STATE_STOPPING,    // Stopping
    IPCP_STATE_REQ_SENT,    // Configure-Request sent
    IPCP_STATE_ACK_RCVD,    // Configure-Ack received
    IPCP_STATE_ACK_SENT,    // Configure-Ack sent
    IPCP_STATE_OPENED       // IPCP opened, IP layer up
};

// ============================================================================
// IP Address Pool
// ============================================================================

#define PPP_IP_POOL_SIZE    4   // Number of IPs in pool

struct PppIpPool {
    IPAddress poolStart;            // First IP in pool (e.g., 192.168.8.2)
    bool allocated[PPP_IP_POOL_SIZE];
    int8_t currentIndex;            // Currently assigned index (-1 = none)
};

// ============================================================================
// IPCP Context Structure
// ============================================================================

struct IpcpContext {
    IpcpState state;

    // Our configuration (gateway)
    IPAddress ourIP;            // Gateway IP (e.g., 192.168.8.1)
    IPAddress subnetMask;       // Subnet mask (e.g., 255.255.255.0)
    IPAddress primaryDns;       // Primary DNS server
    IPAddress secondaryDns;     // Secondary DNS server

    // Peer's configuration (client)
    IPAddress peerIP;           // Client IP (assigned from pool)
    bool peerIPAssigned;        // True if we assigned an IP

    // IP Pool
    PppIpPool pool;

    // Request tracking
    uint8_t identifier;
    uint8_t configRetries;
    unsigned long lastSendTime;

    // Timeouts and limits
    static const uint16_t RESTART_TIMER_MS = 3000;
    static const uint8_t MAX_CONFIGURE = 10;
};

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize IPCP context
void ipcpInit(IpcpContext* ctx);

// Configure IP addresses (call before ipcpOpen)
void ipcpSetGatewayIP(IpcpContext* ctx, IPAddress ip);
void ipcpSetPoolStart(IpcpContext* ctx, IPAddress ip);
void ipcpSetDns(IpcpContext* ctx, IPAddress primary, IPAddress secondary);

// Start IPCP negotiation (call after LCP opens)
void ipcpOpen(IpcpContext* ctx, PppContext* ppp);

// Close IPCP
void ipcpClose(IpcpContext* ctx, PppContext* ppp);

// Process incoming IPCP packet
void ipcpProcessPacket(IpcpContext* ctx, PppContext* ppp,
                       uint8_t* data, uint16_t length);

// Handle timeout
bool ipcpTimeout(IpcpContext* ctx, PppContext* ppp);

// Check if IPCP is opened
inline bool ipcpIsOpened(IpcpContext* ctx) {
    return ctx->state == IPCP_STATE_OPENED;
}

// Get assigned client IP
inline IPAddress ipcpGetClientIP(IpcpContext* ctx) {
    return ctx->peerIP;
}

// Allocate IP from pool (returns 0.0.0.0 if none available)
IPAddress ipcpAllocateIP(IpcpContext* ctx);

// Release IP back to pool
void ipcpReleaseIP(IpcpContext* ctx);

// Get state name for debugging
const char* ipcpStateName(IpcpState state);

#endif // PPP_IPCP_H

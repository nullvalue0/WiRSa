// ============================================================================
// PPP Link Control Protocol (LCP) - RFC 1661
// ============================================================================
// Handles link establishment, configuration, and termination
// ============================================================================

#ifndef PPP_LCP_H
#define PPP_LCP_H

#include <Arduino.h>
#include "ppp.h"

// ============================================================================
// LCP Code Values (RFC 1661)
// ============================================================================

#define LCP_CONFIGURE_REQUEST   1
#define LCP_CONFIGURE_ACK       2
#define LCP_CONFIGURE_NAK       3
#define LCP_CONFIGURE_REJECT    4
#define LCP_TERMINATE_REQUEST   5
#define LCP_TERMINATE_ACK       6
#define LCP_CODE_REJECT         7
#define LCP_PROTOCOL_REJECT     8
#define LCP_ECHO_REQUEST        9
#define LCP_ECHO_REPLY          10
#define LCP_DISCARD_REQUEST     11

// ============================================================================
// LCP Option Types
// ============================================================================

#define LCP_OPT_MRU             1   // Maximum Receive Unit
#define LCP_OPT_ACCM            2   // Async Control Character Map
#define LCP_OPT_AUTH_PROTO      3   // Authentication Protocol
#define LCP_OPT_QUALITY         4   // Quality Protocol
#define LCP_OPT_MAGIC_NUMBER    5   // Magic Number (loop detection)
#define LCP_OPT_PFC             7   // Protocol Field Compression
#define LCP_OPT_ACFC            8   // Address/Control Field Compression

// ============================================================================
// LCP States (Simplified from RFC 1661)
// ============================================================================

enum LcpState {
    LCP_STATE_INITIAL,      // Link not started
    LCP_STATE_STARTING,     // Lower layer up, waiting to send config
    LCP_STATE_CLOSED,       // Link closed
    LCP_STATE_STOPPED,      // Link stopped (waiting for remote)
    LCP_STATE_CLOSING,      // Terminate-Request sent
    LCP_STATE_STOPPING,     // Terminate-Request sent, was open
    LCP_STATE_REQ_SENT,     // Configure-Request sent, waiting for response
    LCP_STATE_ACK_RCVD,     // Configure-Ack received (waiting for peer's request)
    LCP_STATE_ACK_SENT,     // Configure-Ack sent (waiting for our ACK)
    LCP_STATE_OPENED        // Link open and operational
};

// ============================================================================
// LCP Context Structure
// ============================================================================

struct LcpContext {
    LcpState state;

    // Our configuration (what we request)
    uint16_t ourMru;            // Our MRU (default 1500)
    uint32_t ourAccm;           // Our ACCM (start 0xFFFFFFFF, try 0x00000000)
    uint32_t ourMagic;          // Our magic number (random)

    // Peer's configuration (what they requested)
    uint16_t peerMru;           // Peer's MRU
    uint32_t peerAccm;          // Peer's ACCM
    uint32_t peerMagic;         // Peer's magic number

    // Request tracking
    uint8_t identifier;         // Current request ID (incremented per request)
    uint8_t configRetries;      // Configure-Request retry counter
    uint8_t termRetries;        // Terminate-Request retry counter
    unsigned long lastSendTime; // Time of last packet sent (for timeout)

    // Negotiation flags (what peer has ACKed for us)
    bool ourMruAcked;
    bool ourAccmAcked;
    bool ourMagicAcked;

    // Timeouts and limits
    static const uint16_t RESTART_TIMER_MS = 3000;  // Retransmit timeout
    static const uint8_t MAX_CONFIGURE = 10;        // Max configure retries
    static const uint8_t MAX_TERMINATE = 2;         // Max terminate retries
};

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize LCP context
void lcpInit(LcpContext* ctx);

// Start LCP negotiation (called when lower layer is up)
void lcpOpen(LcpContext* ctx, PppContext* ppp);

// Close LCP (send Terminate-Request)
void lcpClose(LcpContext* ctx, PppContext* ppp);

// Process incoming LCP packet
// data points to LCP payload (after PPP header)
// length is LCP packet length
void lcpProcessPacket(LcpContext* ctx, PppContext* ppp,
                      uint8_t* data, uint16_t length);

// Handle timeout (call periodically, e.g., every 100ms)
// Returns true if state changed
bool lcpTimeout(LcpContext* ctx, PppContext* ppp);

// Check if LCP is in opened state
inline bool lcpIsOpened(LcpContext* ctx) {
    return ctx->state == LCP_STATE_OPENED;
}

// Check if LCP is in starting/negotiation state
inline bool lcpIsNegotiating(LcpContext* ctx) {
    return ctx->state == LCP_STATE_REQ_SENT ||
           ctx->state == LCP_STATE_ACK_RCVD ||
           ctx->state == LCP_STATE_ACK_SENT;
}

// Get state name for debugging
const char* lcpStateName(LcpState state);

#endif // PPP_LCP_H

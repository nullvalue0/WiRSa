// ============================================================================
// PPP Protocol Framing Layer (RFC 1662)
// ============================================================================
// Point-to-Point Protocol framing with HDLC-like encapsulation
// ============================================================================

#ifndef PPP_H
#define PPP_H

#include <Arduino.h>

// ============================================================================
// PPP Frame Constants (RFC 1662)
// ============================================================================

// PPP special characters
#define PPP_FLAG        0x7E    // Frame delimiter (126)
#define PPP_ADDR        0xFF    // All-stations address (255)
#define PPP_CTRL        0x03    // Unnumbered information (3)
#define PPP_ESCAPE      0x7D    // Control escape (125)
#define PPP_ESCAPE_XOR  0x20    // XOR value for escaping

// PPP Protocol numbers (big-endian in frame)
#define PPP_PROTO_IP    0x0021  // Internet Protocol
#define PPP_PROTO_IPCP  0x8021  // IP Control Protocol
#define PPP_PROTO_LCP   0xC021  // Link Control Protocol

// PPP configuration
#define PPP_MTU         1500    // Maximum transmission unit
#define PPP_MRU         1500    // Maximum receive unit
#define PPP_BUFFER_SIZE 1600    // Buffer size with overhead

// FCS (Frame Check Sequence) - CRC-16 CCITT
#define PPP_FCS_INIT    0xFFFF  // Initial FCS value
#define PPP_FCS_GOOD    0xF0B8  // Good final FCS after including received FCS

// ============================================================================
// PPP State Machine
// ============================================================================

enum PppRxState {
    PPP_RX_IDLE,        // Waiting for frame start (flag)
    PPP_RX_RECEIVING,   // Receiving frame data
    PPP_RX_ESCAPE,      // Just received escape character
    PPP_RX_FRAME_DONE   // Frame complete, caller processing it
};

// ============================================================================
// PPP Context Structure
// ============================================================================

struct PppContext {
    // Receive state
    PppRxState rxState;
    uint8_t rxBuffer[PPP_BUFFER_SIZE];
    uint16_t rxPos;
    uint16_t rxFcs;         // Running FCS calculation

    // Transmit buffer
    uint8_t txBuffer[PPP_BUFFER_SIZE];
    uint16_t txPos;

    // Async Control Character Map (ACCM) - which chars need escaping
    // Bit N set means character N (0x00-0x1F) must be escaped
    uint32_t txAccm;        // Transmit ACCM (what WE escape when sending)
    uint32_t rxAccm;        // Receive ACCM (what PEER escapes when sending)

    // Address/Control field compression negotiated
    bool addrCtrlCompression;
    // Protocol field compression negotiated
    bool protoCompression;

    // Statistics
    uint32_t framesReceived;
    uint32_t framesSent;
    uint32_t fcsErrors;     // CRC failures
    uint32_t rxErrors;      // Framing errors, overruns
    uint32_t bytesReceived;
    uint32_t bytesSent;
};

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize PPP context
void pppInit(PppContext* ctx);

// Process incoming byte from serial
// Returns: 0 = byte consumed, continue receiving
//          >0 = complete frame length (frame data in ctx->rxBuffer)
//          -1 = FCS error (frame discarded)
int pppReceiveByte(PppContext* ctx, uint8_t byte);

// Send a frame with PPP encoding
// protocol: PPP protocol number (e.g., PPP_PROTO_IP, PPP_PROTO_LCP)
// data: payload data (excluding Address, Control, Protocol fields)
// length: payload length
void pppSendFrame(PppContext* ctx, uint16_t protocol,
                  const uint8_t* data, uint16_t length);

// Calculate FCS (CRC-16) for one byte
// Uses table-driven calculation for speed
uint16_t pppCalcFcs(uint16_t fcs, uint8_t byte);

// Calculate FCS over a buffer
uint16_t pppCalcFcsBuffer(const uint8_t* data, uint16_t length);

// Check if a byte needs to be escaped based on ACCM
bool pppNeedsEscape(PppContext* ctx, uint8_t byte);

// Reset receiver state (after error or timeout)
void pppReset(PppContext* ctx);

// Get protocol number from received frame (first 2 bytes after addr/ctrl)
// Returns protocol number, or 0 if frame too short
uint16_t pppGetProtocol(PppContext* ctx);

// Get pointer to payload (after protocol field)
// Also returns payload length via pointer
uint8_t* pppGetPayload(PppContext* ctx, uint16_t* length);

// Get pointer to received frame data
inline uint8_t* pppGetRxBuffer(PppContext* ctx) {
    return ctx->rxBuffer;
}

// Get received frame length
inline uint16_t pppGetRxLength(PppContext* ctx) {
    return ctx->rxPos;
}

#endif // PPP_H

#ifndef SLIP_H
#define SLIP_H

#include <Arduino.h>

// ============================================================================
// SLIP Protocol Constants (RFC 1055)
// ============================================================================

// SLIP special characters
#define SLIP_END     0xC0   // Frame delimiter (192)
#define SLIP_ESC     0xDB   // Escape character (219)
#define SLIP_ESC_END 0xDC   // Escaped END (220) - represents END in data
#define SLIP_ESC_ESC 0xDD   // Escaped ESC (221) - represents ESC in data

// SLIP configuration
#define SLIP_MTU         1500   // Maximum transmission unit (matches Ethernet)
#define SLIP_BUFFER_SIZE 1600   // Buffer size with overhead for escaping

// ============================================================================
// SLIP State Machine
// ============================================================================

enum SlipRxState {
    SLIP_RX_IDLE,       // Waiting for frame start
    SLIP_RX_RECEIVING,  // Receiving frame data
    SLIP_RX_ESCAPE      // Just received ESC, next byte is escaped
};

// ============================================================================
// SLIP Context Structure
// ============================================================================

struct SlipContext {
    // Receive state
    SlipRxState rxState;
    uint8_t rxBuffer[SLIP_BUFFER_SIZE];
    uint16_t rxPos;

    // Transmit buffer (for building frames if needed)
    uint8_t txBuffer[SLIP_BUFFER_SIZE];
    uint16_t txPos;

    // Statistics
    uint32_t framesReceived;
    uint32_t framesSent;
    uint32_t rxErrors;         // Framing errors, overruns
    uint32_t txErrors;
    uint32_t bytesReceived;    // Total bytes (before deframing)
    uint32_t bytesSent;        // Total bytes (after framing)
};

// ============================================================================
// Function Declarations
// ============================================================================

// Initialize SLIP context
void slipInit(SlipContext* ctx);

// Process incoming byte from serial
// Returns: 0 = byte consumed, continue receiving
//          >0 = complete frame length (frame data in ctx->rxBuffer)
int slipReceiveByte(SlipContext* ctx, uint8_t byte);

// Send a frame (IP packet) with SLIP encoding
// Writes directly to PhysicalSerial
void slipSendFrame(SlipContext* ctx, const uint8_t* data, uint16_t length);

// Reset receiver state (after error or timeout)
void slipReset(SlipContext* ctx);

// Get pointer to received frame data
inline uint8_t* slipGetRxBuffer(SlipContext* ctx) {
    return ctx->rxBuffer;
}

#endif // SLIP_H

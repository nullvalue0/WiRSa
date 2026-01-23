// ============================================================================
// SLIP Protocol Implementation (RFC 1055)
// ============================================================================
// Serial Line Internet Protocol - simple framing for IP packets over serial
// ============================================================================

#include "slip.h"
#include "globals.h"

// ============================================================================
// Initialize SLIP Context
// ============================================================================

void slipInit(SlipContext* ctx) {
    ctx->rxState = SLIP_RX_IDLE;
    ctx->rxPos = 0;
    ctx->txPos = 0;
    ctx->framesReceived = 0;
    ctx->framesSent = 0;
    ctx->rxErrors = 0;
    ctx->txErrors = 0;
    ctx->bytesReceived = 0;
    ctx->bytesSent = 0;
}

// ============================================================================
// Receive SLIP-encoded byte
// ============================================================================
// Process one byte at a time from serial input
// Returns: 0 = continue receiving
//          >0 = complete frame length (data in rxBuffer)

int slipReceiveByte(SlipContext* ctx, uint8_t byte) {
    ctx->bytesReceived++;

    switch (ctx->rxState) {
        case SLIP_RX_IDLE:
            // Waiting for frame start
            if (byte == SLIP_END) {
                // Start of frame (or empty frame delimiter)
                ctx->rxPos = 0;
                ctx->rxState = SLIP_RX_RECEIVING;
            }
            // Ignore any other bytes while idle (line noise)
            return 0;

        case SLIP_RX_RECEIVING:
            if (byte == SLIP_END) {
                // End of frame
                if (ctx->rxPos > 0) {
                    // Valid frame received
                    ctx->framesReceived++;
                    int len = ctx->rxPos;
                    ctx->rxState = SLIP_RX_IDLE;
                    // Don't reset rxPos - caller needs the data
                    return len;
                }
                // Empty frame (consecutive ENDs) - stay receiving
                ctx->rxPos = 0;
                return 0;
            } else if (byte == SLIP_ESC) {
                // Escape sequence starting
                ctx->rxState = SLIP_RX_ESCAPE;
                return 0;
            } else {
                // Regular data byte
                if (ctx->rxPos < SLIP_BUFFER_SIZE) {
                    ctx->rxBuffer[ctx->rxPos++] = byte;
                } else {
                    // Buffer overflow - discard frame
                    ctx->rxErrors++;
                    ctx->rxState = SLIP_RX_IDLE;
                    ctx->rxPos = 0;
                }
                return 0;
            }

        case SLIP_RX_ESCAPE:
            // Process escaped byte
            ctx->rxState = SLIP_RX_RECEIVING;

            if (byte == SLIP_ESC_END) {
                // Escaped END -> store actual END character
                byte = SLIP_END;
            } else if (byte == SLIP_ESC_ESC) {
                // Escaped ESC -> store actual ESC character
                byte = SLIP_ESC;
            } else {
                // Invalid escape sequence - protocol error
                // RFC 1055 suggests storing the byte anyway, but we'll flag error
                ctx->rxErrors++;
                // Continue anyway - some implementations may have quirks
            }

            if (ctx->rxPos < SLIP_BUFFER_SIZE) {
                ctx->rxBuffer[ctx->rxPos++] = byte;
            } else {
                // Buffer overflow
                ctx->rxErrors++;
                ctx->rxState = SLIP_RX_IDLE;
                ctx->rxPos = 0;
            }
            return 0;
    }

    return 0;
}

// ============================================================================
// Send SLIP-encoded frame
// ============================================================================
// Takes raw IP packet data and sends it with SLIP framing to PhysicalSerial

void slipSendFrame(SlipContext* ctx, const uint8_t* data, uint16_t length) {
    // RFC 1055 recommends sending END at start to flush any line noise
    // This also serves as frame delimiter for back-to-back frames
    PhysicalSerial.write(SLIP_END);
    ctx->bytesSent++;

    // Send data with escaping
    for (uint16_t i = 0; i < length; i++) {
        switch (data[i]) {
            case SLIP_END:
                // END character in data must be escaped
                PhysicalSerial.write(SLIP_ESC);
                PhysicalSerial.write(SLIP_ESC_END);
                ctx->bytesSent += 2;
                break;

            case SLIP_ESC:
                // ESC character in data must be escaped
                PhysicalSerial.write(SLIP_ESC);
                PhysicalSerial.write(SLIP_ESC_ESC);
                ctx->bytesSent += 2;
                break;

            default:
                // Regular byte - send as-is
                PhysicalSerial.write(data[i]);
                ctx->bytesSent++;
                break;
        }
    }

    // End frame
    PhysicalSerial.write(SLIP_END);
    ctx->bytesSent++;

    ctx->framesSent++;
}

// ============================================================================
// Reset SLIP receiver state
// ============================================================================
// Called after timeout or error to resync with incoming data

void slipReset(SlipContext* ctx) {
    ctx->rxState = SLIP_RX_IDLE;
    ctx->rxPos = 0;
}

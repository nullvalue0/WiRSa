// ============================================================================
// PPP Protocol Framing Implementation (RFC 1662)
// ============================================================================
// Point-to-Point Protocol framing with HDLC-like encapsulation
// ============================================================================

#include "ppp.h"
#include "globals.h"

// ============================================================================
// CRC-16 FCS Table (CCITT polynomial 0x8408, reversed 0x1021)
// ============================================================================
// This is the standard PPP FCS lookup table for fast CRC calculation

static const uint16_t pppFcsTable[256] PROGMEM = {
    0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
    0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
    0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
    0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
    0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
    0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
    0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
    0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
    0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
    0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
    0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
    0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
    0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
    0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
    0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
    0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
    0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
    0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
    0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
    0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
    0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
    0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
    0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
    0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
    0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
    0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
    0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
    0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
    0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
    0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
    0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
    0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
};

// ============================================================================
// Initialize PPP Context
// ============================================================================

void pppInit(PppContext* ctx) {
    ctx->rxState = PPP_RX_IDLE;
    ctx->rxPos = 0;
    ctx->rxFcs = PPP_FCS_INIT;
    ctx->txPos = 0;

    // Default ACCM: escape all control characters (0x00-0x1F)
    // This is the safe default before LCP negotiation
    ctx->txAccm = 0xFFFFFFFF;
    ctx->rxAccm = 0xFFFFFFFF;

    // No compression until negotiated
    ctx->addrCtrlCompression = false;
    ctx->protoCompression = false;

    // Reset statistics
    ctx->framesReceived = 0;
    ctx->framesSent = 0;
    ctx->fcsErrors = 0;
    ctx->rxErrors = 0;
    ctx->bytesReceived = 0;
    ctx->bytesSent = 0;
}

// ============================================================================
// Calculate FCS (CRC-16) for one byte
// ============================================================================

uint16_t pppCalcFcs(uint16_t fcs, uint8_t byte) {
    return (fcs >> 8) ^ pgm_read_word(&pppFcsTable[(fcs ^ byte) & 0xFF]);
}

// ============================================================================
// Calculate FCS over a buffer
// ============================================================================

uint16_t pppCalcFcsBuffer(const uint8_t* data, uint16_t length) {
    uint16_t fcs = PPP_FCS_INIT;
    for (uint16_t i = 0; i < length; i++) {
        fcs = pppCalcFcs(fcs, data[i]);
    }
    return fcs;
}

// ============================================================================
// Check if byte needs escaping
// ============================================================================

bool pppNeedsEscape(PppContext* ctx, uint8_t byte) {
    // Always escape FLAG and ESCAPE characters
    if (byte == PPP_FLAG || byte == PPP_ESCAPE) {
        return true;
    }
    // Check ACCM for control characters (0x00-0x1F)
    if (byte < 0x20) {
        return (ctx->txAccm & (1UL << byte)) != 0;
    }
    return false;
}

// ============================================================================
// Receive PPP-encoded byte
// ============================================================================
// Process one byte at a time from serial input
// Returns: 0 = continue receiving
//          >0 = complete frame length (data in rxBuffer, including addr/ctrl/proto)
//          -1 = FCS error (frame discarded)

int pppReceiveByte(PppContext* ctx, uint8_t byte) {
    ctx->bytesReceived++;

    // Handle transition from completed frame to next frame.
    // Per RFC 1662, a single 0x7E flag can end one frame and start the next
    // ("flag sharing"). After completing a frame, we enter FRAME_DONE state.
    // The next byte determines what happens:
    //   - 0x7E: redundant flag (separate flags between frames) - just reset
    //   - 0x7D: escape char starting a flag-shared frame
    //   - other: first data byte of a flag-shared frame
    if (ctx->rxState == PPP_RX_FRAME_DONE) {
        ctx->rxPos = 0;
        ctx->rxFcs = PPP_FCS_INIT;
        if (byte == PPP_FLAG) {
            // Separate flag between frames - just start fresh
            ctx->rxState = PPP_RX_RECEIVING;
            return 0;
        }
        // Flag sharing: this byte is the first byte of the next frame
        ctx->rxState = PPP_RX_RECEIVING;
        if (byte == PPP_ESCAPE) {
            ctx->rxState = PPP_RX_ESCAPE;
            return 0;
        }
        // Regular data byte - add to buffer
        ctx->rxBuffer[ctx->rxPos++] = byte;
        ctx->rxFcs = pppCalcFcs(ctx->rxFcs, byte);
        return 0;
    }

    switch (ctx->rxState) {
        case PPP_RX_IDLE:
            // Waiting for frame start
            if (byte == PPP_FLAG) {
                // Start of frame
                ctx->rxPos = 0;
                ctx->rxFcs = PPP_FCS_INIT;
                ctx->rxState = PPP_RX_RECEIVING;
            }
            // Ignore any other bytes while idle (line noise)
            return 0;

        case PPP_RX_RECEIVING:
            if (byte == PPP_FLAG) {
                // End of frame (or start of next)
                if (ctx->rxPos < 4) {
                    // Frame too short (need at least addr+ctrl+proto+fcs)
                    // Could be empty frame delimiter or noise
                    // Stay in RECEIVING - this flag starts the next frame
                    ctx->rxPos = 0;
                    ctx->rxFcs = PPP_FCS_INIT;
                    return 0;
                }

                // Check FCS - good FCS after including received FCS bytes
                // should equal PPP_FCS_GOOD (0xF0B8)
                if (ctx->rxFcs != PPP_FCS_GOOD) {
                    // FCS error - use FRAME_DONE so next byte is handled correctly
                    // (the 0x7E that ended this bad frame may start the next one)
                    ctx->fcsErrors++;
                    ctx->rxState = PPP_RX_FRAME_DONE;
                    ctx->rxPos = 0;
                    return -1;
                }

                // Valid frame - remove FCS bytes from count
                // Use FRAME_DONE state so the next byte handles flag sharing
                ctx->framesReceived++;
                int len = ctx->rxPos - 2;  // Exclude 2-byte FCS
                ctx->rxState = PPP_RX_FRAME_DONE;
                ctx->rxPos = len;  // Update to actual data length
                return len;

            } else if (byte == PPP_ESCAPE) {
                // Escape sequence starting
                ctx->rxState = PPP_RX_ESCAPE;
                return 0;

            } else {
                // Regular data byte - add to buffer and update FCS
                if (ctx->rxPos < PPP_BUFFER_SIZE) {
                    ctx->rxBuffer[ctx->rxPos++] = byte;
                    ctx->rxFcs = pppCalcFcs(ctx->rxFcs, byte);
                } else {
                    // Buffer overflow - discard frame
                    ctx->rxErrors++;
                    ctx->rxState = PPP_RX_IDLE;
                    ctx->rxPos = 0;
                }
                return 0;
            }

        case PPP_RX_ESCAPE:
            // Process escaped byte - XOR with 0x20
            ctx->rxState = PPP_RX_RECEIVING;
            byte ^= PPP_ESCAPE_XOR;

            if (ctx->rxPos < PPP_BUFFER_SIZE) {
                ctx->rxBuffer[ctx->rxPos++] = byte;
                ctx->rxFcs = pppCalcFcs(ctx->rxFcs, byte);
            } else {
                // Buffer overflow
                ctx->rxErrors++;
                ctx->rxState = PPP_RX_IDLE;
                ctx->rxPos = 0;
            }
            return 0;

        default:
            // Should not happen - reset to safe state
            ctx->rxState = PPP_RX_IDLE;
            ctx->rxPos = 0;
            return 0;
    }

    return 0;
}

// ============================================================================
// Send PPP-encoded frame
// ============================================================================
// Takes protocol and payload, adds framing and FCS, sends to PhysicalSerial

void pppSendFrame(PppContext* ctx, uint16_t protocol,
                  const uint8_t* data, uint16_t length) {
    uint16_t fcs = PPP_FCS_INIT;

    // Wait for CTS (Clear To Send) ONCE at start of frame when hardware flow control enabled
    // F_HARDWARE = 1 (from modem.cpp enum)
    // Logic matches handleFlowControl(): pause when CTS == pinPolarity
    if (flowControl == 1) {  // F_HARDWARE
        unsigned long startWait = millis();
        while (digitalRead(CTS_PIN) == pinPolarity) {
            if (millis() - startWait > 1000) {
                // Timeout after 1 second - don't block forever
                break;
            }
            yield();  // Allow other tasks to run
        }
    }

    // Helper to send a byte with escaping if needed
    auto sendByte = [&](uint8_t byte) {
        if (pppNeedsEscape(ctx, byte)) {
            PhysicalSerial.write(PPP_ESCAPE);
            PhysicalSerial.write(byte ^ PPP_ESCAPE_XOR);
            ctx->bytesSent += 2;
        } else {
            PhysicalSerial.write(byte);
            ctx->bytesSent++;
        }
    };

    // Opening flag
    PhysicalSerial.write(PPP_FLAG);
    ctx->bytesSent++;

    // Address field (unless compression negotiated)
    if (!ctx->addrCtrlCompression) {
        sendByte(PPP_ADDR);
        fcs = pppCalcFcs(fcs, PPP_ADDR);

        // Control field
        sendByte(PPP_CTRL);
        fcs = pppCalcFcs(fcs, PPP_CTRL);
    }

    // Protocol field (2 bytes, big-endian)
    // Could be compressed to 1 byte if protocol < 0x100 and compression enabled
    if (ctx->protoCompression && (protocol & 0xFF00) == 0) {
        sendByte(protocol & 0xFF);
        fcs = pppCalcFcs(fcs, protocol & 0xFF);
    } else {
        sendByte((protocol >> 8) & 0xFF);
        fcs = pppCalcFcs(fcs, (protocol >> 8) & 0xFF);
        sendByte(protocol & 0xFF);
        fcs = pppCalcFcs(fcs, protocol & 0xFF);
    }

    // Payload data
    for (uint16_t i = 0; i < length; i++) {
        sendByte(data[i]);
        fcs = pppCalcFcs(fcs, data[i]);
    }

    // FCS (complemented, little-endian)
    fcs ^= 0xFFFF;
    sendByte(fcs & 0xFF);
    sendByte((fcs >> 8) & 0xFF);

    // Closing flag
    PhysicalSerial.write(PPP_FLAG);
    ctx->bytesSent++;

    // Flush to ensure all bytes are transmitted before proceeding
    // This prevents TX buffer overflow on large frames
    PhysicalSerial.flush();

    ctx->framesSent++;
}

// ============================================================================
// Reset PPP receiver state
// ============================================================================

void pppReset(PppContext* ctx) {
    ctx->rxState = PPP_RX_IDLE;
    ctx->rxPos = 0;
    ctx->rxFcs = PPP_FCS_INIT;
}

// ============================================================================
// Get protocol number from received frame
// ============================================================================
// Returns protocol number from frame, handling addr/ctrl compression

uint16_t pppGetProtocol(PppContext* ctx) {
    if (ctx->rxPos < 2) {
        return 0;  // Frame too short
    }

    uint8_t* buf = ctx->rxBuffer;
    int offset = 0;

    // Check for Address and Control fields
    if (buf[0] == PPP_ADDR && buf[1] == PPP_CTRL) {
        offset = 2;
    }

    if (ctx->rxPos < offset + 1) {
        return 0;
    }

    // Protocol field - could be 1 or 2 bytes
    // If first byte has bit 0 set, it's a 1-byte compressed protocol
    if (buf[offset] & 0x01) {
        return buf[offset];
    }

    if (ctx->rxPos < offset + 2) {
        return 0;
    }

    // 2-byte protocol (big-endian)
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}

// ============================================================================
// Get pointer to payload (after addr/ctrl/protocol)
// ============================================================================

uint8_t* pppGetPayload(PppContext* ctx, uint16_t* length) {
    if (ctx->rxPos < 4) {
        *length = 0;
        return nullptr;
    }

    uint8_t* buf = ctx->rxBuffer;
    int offset = 0;

    // Skip Address and Control fields if present
    if (buf[0] == PPP_ADDR && buf[1] == PPP_CTRL) {
        offset = 2;
    }

    // Skip Protocol field (1 or 2 bytes)
    if (buf[offset] & 0x01) {
        offset += 1;  // Compressed 1-byte protocol
    } else {
        offset += 2;  // Full 2-byte protocol
    }

    *length = ctx->rxPos - offset;
    return &buf[offset];
}

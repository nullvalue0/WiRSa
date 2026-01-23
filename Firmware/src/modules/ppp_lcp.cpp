// ============================================================================
// PPP Link Control Protocol (LCP) Implementation - RFC 1661
// ============================================================================

#include "ppp_lcp.h"
#include "globals.h"

// ============================================================================
// Debug output (can be disabled in production)
// ============================================================================

#ifdef PPP_DEBUG
#define LCP_DEBUG(msg) Serial.print(msg); Serial.print("\r\n")
#define LCP_DEBUG_F(fmt, ...) Serial.printf(fmt "\r\n", ##__VA_ARGS__)
#else
#define LCP_DEBUG(msg)
#define LCP_DEBUG_F(fmt, ...)
#endif

// ============================================================================
// Initialize LCP Context
// ============================================================================

void lcpInit(LcpContext* ctx) {
    ctx->state = LCP_STATE_INITIAL;

    // Our configuration defaults
    ctx->ourMru = PPP_MRU;          // 1500
    ctx->ourAccm = 0x00000000;      // Request no escaping (ideal)
    ctx->ourMagic = random(1, 0xFFFFFFFF);  // Random non-zero magic

    // Peer defaults (until negotiated)
    ctx->peerMru = PPP_MRU;
    ctx->peerAccm = 0xFFFFFFFF;     // Assume peer escapes all until told otherwise
    ctx->peerMagic = 0;

    // Request tracking
    ctx->identifier = 0;
    ctx->configRetries = 0;
    ctx->termRetries = 0;
    ctx->lastSendTime = 0;

    // Nothing ACKed yet
    ctx->ourMruAcked = false;
    ctx->ourAccmAcked = false;
    ctx->ourMagicAcked = false;
}

// ============================================================================
// Send Configure-Request
// ============================================================================

static void lcpSendConfigRequest(LcpContext* ctx, PppContext* ppp) {
    uint8_t packet[32];
    uint16_t pos = 0;

    // LCP header: Code, Identifier, Length (length filled in later)
    packet[pos++] = LCP_CONFIGURE_REQUEST;
    packet[pos++] = ++ctx->identifier;
    uint16_t lenPos = pos;
    pos += 2;  // Reserve space for length

    // Option: MRU (if not default 1500)
    if (ctx->ourMru != 1500) {
        packet[pos++] = LCP_OPT_MRU;
        packet[pos++] = 4;  // Length including type and length bytes
        packet[pos++] = (ctx->ourMru >> 8) & 0xFF;
        packet[pos++] = ctx->ourMru & 0xFF;
    }

    // Option: ACCM (always send to negotiate minimal escaping)
    packet[pos++] = LCP_OPT_ACCM;
    packet[pos++] = 6;  // Length
    packet[pos++] = (ctx->ourAccm >> 24) & 0xFF;
    packet[pos++] = (ctx->ourAccm >> 16) & 0xFF;
    packet[pos++] = (ctx->ourAccm >> 8) & 0xFF;
    packet[pos++] = ctx->ourAccm & 0xFF;

    // Option: Magic Number (for loop detection)
    packet[pos++] = LCP_OPT_MAGIC_NUMBER;
    packet[pos++] = 6;  // Length
    packet[pos++] = (ctx->ourMagic >> 24) & 0xFF;
    packet[pos++] = (ctx->ourMagic >> 16) & 0xFF;
    packet[pos++] = (ctx->ourMagic >> 8) & 0xFF;
    packet[pos++] = ctx->ourMagic & 0xFF;

    // Fill in length
    packet[lenPos] = (pos >> 8) & 0xFF;
    packet[lenPos + 1] = pos & 0xFF;

    // Send the packet
    pppSendFrame(ppp, PPP_PROTO_LCP, packet, pos);
    ctx->lastSendTime = millis();
    ctx->configRetries++;

    LCP_DEBUG_F("LCP: Sent Configure-Request id=%d\n", ctx->identifier);
}

// ============================================================================
// Send Configure-Ack
// ============================================================================

static void lcpSendConfigAck(LcpContext* ctx, PppContext* ppp,
                              uint8_t id, uint8_t* options, uint16_t optLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    // LCP header
    packet[pos++] = LCP_CONFIGURE_ACK;
    packet[pos++] = id;
    uint16_t totalLen = 4 + optLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    // Copy all options (ACK echoes back the entire request)
    memcpy(&packet[pos], options, optLen);
    pos += optLen;

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, pos);
    LCP_DEBUG_F("LCP: Sent Configure-Ack id=%d\n", id);
}

// ============================================================================
// Send Configure-Nak (with suggested values)
// ============================================================================

static void lcpSendConfigNak(LcpContext* ctx, PppContext* ppp,
                              uint8_t id, uint8_t* nakOptions, uint16_t nakLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = LCP_CONFIGURE_NAK;
    packet[pos++] = id;
    uint16_t totalLen = 4 + nakLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    memcpy(&packet[pos], nakOptions, nakLen);
    pos += nakLen;

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, pos);
    LCP_DEBUG_F("LCP: Sent Configure-Nak id=%d\n", id);
}

// ============================================================================
// Send Configure-Reject (for unsupported options)
// ============================================================================

static void lcpSendConfigReject(LcpContext* ctx, PppContext* ppp,
                                 uint8_t id, uint8_t* rejOptions, uint16_t rejLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = LCP_CONFIGURE_REJECT;
    packet[pos++] = id;
    uint16_t totalLen = 4 + rejLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    memcpy(&packet[pos], rejOptions, rejLen);
    pos += rejLen;

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, pos);
    LCP_DEBUG_F("LCP: Sent Configure-Reject id=%d\n", id);
}

// ============================================================================
// Send Terminate-Ack
// ============================================================================

static void lcpSendTerminateAck(LcpContext* ctx, PppContext* ppp, uint8_t id) {
    uint8_t packet[4];
    packet[0] = LCP_TERMINATE_ACK;
    packet[1] = id;
    packet[2] = 0;
    packet[3] = 4;  // Length

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, 4);
    LCP_DEBUG_F("LCP: Sent Terminate-Ack id=%d\n", id);
}

// ============================================================================
// Send Echo-Reply
// ============================================================================

static void lcpSendEchoReply(LcpContext* ctx, PppContext* ppp,
                              uint8_t id, uint8_t* data, uint16_t len) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = LCP_ECHO_REPLY;
    packet[pos++] = id;
    uint16_t totalLen = 4 + 4 + (len > 4 ? len - 4 : 0);  // header + magic + echo data
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    // Our magic number
    packet[pos++] = (ctx->ourMagic >> 24) & 0xFF;
    packet[pos++] = (ctx->ourMagic >> 16) & 0xFF;
    packet[pos++] = (ctx->ourMagic >> 8) & 0xFF;
    packet[pos++] = ctx->ourMagic & 0xFF;

    // Copy any additional echo data (skip incoming magic number)
    if (len > 4) {
        memcpy(&packet[pos], &data[4], len - 4);
        pos += len - 4;
    }

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, pos);
    LCP_DEBUG_F("LCP: Sent Echo-Reply id=%d\n", id);
}

// ============================================================================
// Handle incoming Configure-Request
// ============================================================================

static void lcpHandleConfigRequest(LcpContext* ctx, PppContext* ppp,
                                    uint8_t id, uint8_t* options, uint16_t optLen) {
    uint8_t ackOptions[256];
    uint8_t nakOptions[64];
    uint8_t rejOptions[64];
    uint16_t ackLen = 0;
    uint16_t nakLen = 0;
    uint16_t rejLen = 0;

    uint16_t pos = 0;
    while (pos < optLen) {
        uint8_t optType = options[pos];
        uint8_t optLen2 = options[pos + 1];

        if (optLen2 < 2 || pos + optLen2 > optLen) {
            // Malformed option
            break;
        }

        switch (optType) {
            case LCP_OPT_MRU: {
                // Accept any reasonable MRU (128-1500)
                uint16_t mru = ((uint16_t)options[pos + 2] << 8) | options[pos + 3];
                if (mru >= 128 && mru <= 1500) {
                    ctx->peerMru = mru;
                    memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                    ackLen += optLen2;
                } else {
                    // NAK with suggested value
                    nakOptions[nakLen++] = LCP_OPT_MRU;
                    nakOptions[nakLen++] = 4;
                    nakOptions[nakLen++] = (1500 >> 8) & 0xFF;
                    nakOptions[nakLen++] = 1500 & 0xFF;
                }
                break;
            }

            case LCP_OPT_ACCM: {
                // Accept any ACCM value
                ctx->peerAccm = ((uint32_t)options[pos + 2] << 24) |
                                ((uint32_t)options[pos + 3] << 16) |
                                ((uint32_t)options[pos + 4] << 8) |
                                options[pos + 5];
                memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                ackLen += optLen2;
                break;
            }

            case LCP_OPT_MAGIC_NUMBER: {
                // Accept any non-zero magic number
                uint32_t magic = ((uint32_t)options[pos + 2] << 24) |
                                 ((uint32_t)options[pos + 3] << 16) |
                                 ((uint32_t)options[pos + 4] << 8) |
                                 options[pos + 5];
                if (magic != 0 && magic != ctx->ourMagic) {
                    ctx->peerMagic = magic;
                    memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                    ackLen += optLen2;
                } else if (magic == ctx->ourMagic) {
                    // Loop detected - NAK with different magic
                    nakOptions[nakLen++] = LCP_OPT_MAGIC_NUMBER;
                    nakOptions[nakLen++] = 6;
                    uint32_t newMagic = random(1, 0xFFFFFFFF);
                    nakOptions[nakLen++] = (newMagic >> 24) & 0xFF;
                    nakOptions[nakLen++] = (newMagic >> 16) & 0xFF;
                    nakOptions[nakLen++] = (newMagic >> 8) & 0xFF;
                    nakOptions[nakLen++] = newMagic & 0xFF;
                } else {
                    // Zero magic - NAK with our suggestion
                    nakOptions[nakLen++] = LCP_OPT_MAGIC_NUMBER;
                    nakOptions[nakLen++] = 6;
                    nakOptions[nakLen++] = 0x12;
                    nakOptions[nakLen++] = 0x34;
                    nakOptions[nakLen++] = 0x56;
                    nakOptions[nakLen++] = 0x78;
                }
                break;
            }

            case LCP_OPT_AUTH_PROTO:
                // ALWAYS REJECT authentication - we don't support it
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                LCP_DEBUG("LCP: Rejecting authentication option");
                break;

            case LCP_OPT_PFC:
            case LCP_OPT_ACFC:
                // Reject compression options for simplicity
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                break;

            case LCP_OPT_QUALITY:
                // Reject quality protocol
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                break;

            default:
                // Unknown option - reject
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                LCP_DEBUG_F("LCP: Rejecting unknown option %d\n", optType);
                break;
        }

        pos += optLen2;
    }

    // Send appropriate response
    // Priority: Reject > Nak > Ack
    if (rejLen > 0) {
        lcpSendConfigReject(ctx, ppp, id, rejOptions, rejLen);
    } else if (nakLen > 0) {
        lcpSendConfigNak(ctx, ppp, id, nakOptions, nakLen);
    } else {
        lcpSendConfigAck(ctx, ppp, id, ackOptions, ackLen);

        // Update state based on current state
        switch (ctx->state) {
            case LCP_STATE_REQ_SENT:
                ctx->state = LCP_STATE_ACK_SENT;
                break;
            case LCP_STATE_ACK_RCVD:
                ctx->state = LCP_STATE_OPENED;
                // Update PPP context with negotiated ACCM
                ppp->rxAccm = ctx->peerAccm;
                LCP_DEBUG("LCP: Link OPENED!");
                break;
            default:
                break;
        }
    }
}

// ============================================================================
// Handle incoming Configure-Ack
// ============================================================================

static void lcpHandleConfigAck(LcpContext* ctx, PppContext* ppp,
                                uint8_t id, uint8_t* options, uint16_t optLen) {
    // Verify this is a response to our request
    if (id != ctx->identifier) {
        LCP_DEBUG_F("LCP: Ignoring Configure-Ack with wrong id=%d (expected %d)\n",
                    id, ctx->identifier);
        return;
    }

    // Parse ACKed options to confirm what peer accepted
    uint16_t pos = 0;
    while (pos < optLen) {
        uint8_t optType = options[pos];
        uint8_t optLen2 = options[pos + 1];

        switch (optType) {
            case LCP_OPT_MRU:
                ctx->ourMruAcked = true;
                break;
            case LCP_OPT_ACCM:
                ctx->ourAccmAcked = true;
                // Peer accepted our ACCM request, but we still escape all control chars
                // when SENDING for compatibility with Windows 98 serial drivers that
                // may interpret control characters (XON/XOFF, etc.)
                ppp->txAccm = 0xFFFFFFFF;  // Always escape all control chars for safety
                break;
            case LCP_OPT_MAGIC_NUMBER:
                ctx->ourMagicAcked = true;
                break;
        }

        pos += optLen2;
    }

    ctx->configRetries = 0;

    // Update state
    switch (ctx->state) {
        case LCP_STATE_REQ_SENT:
            ctx->state = LCP_STATE_ACK_RCVD;
            break;
        case LCP_STATE_ACK_SENT:
            ctx->state = LCP_STATE_OPENED;
            LCP_DEBUG("LCP: Link OPENED!");
            break;
        default:
            break;
    }
}

// ============================================================================
// Handle incoming Configure-Nak
// ============================================================================

static void lcpHandleConfigNak(LcpContext* ctx, PppContext* ppp,
                                uint8_t id, uint8_t* options, uint16_t optLen) {
    if (id != ctx->identifier) {
        return;
    }

    // Parse NAKed options and adjust our values
    uint16_t pos = 0;
    while (pos < optLen) {
        uint8_t optType = options[pos];
        uint8_t optLen2 = options[pos + 1];

        switch (optType) {
            case LCP_OPT_MRU:
                // Use suggested MRU
                ctx->ourMru = ((uint16_t)options[pos + 2] << 8) | options[pos + 3];
                break;
            case LCP_OPT_ACCM:
                // Use suggested ACCM (peer wants us to escape more)
                ctx->ourAccm = ((uint32_t)options[pos + 2] << 24) |
                               ((uint32_t)options[pos + 3] << 16) |
                               ((uint32_t)options[pos + 4] << 8) |
                               options[pos + 5];
                break;
            case LCP_OPT_MAGIC_NUMBER:
                // Use suggested magic number (or generate new one)
                ctx->ourMagic = random(1, 0xFFFFFFFF);
                break;
        }

        pos += optLen2;
    }

    // Resend Configure-Request with adjusted values
    ctx->configRetries = 0;  // Reset retry counter for new attempt
    lcpSendConfigRequest(ctx, ppp);
}

// ============================================================================
// Handle incoming Configure-Reject
// ============================================================================

static void lcpHandleConfigReject(LcpContext* ctx, PppContext* ppp,
                                   uint8_t id, uint8_t* options, uint16_t optLen) {
    if (id != ctx->identifier) {
        return;
    }

    // Parse rejected options - don't send them again
    // For simplicity, we just resend without the rejected options
    // In a full implementation, we'd track which options were rejected

    uint16_t pos = 0;
    while (pos < optLen) {
        uint8_t optType = options[pos];
        uint8_t optLen2 = options[pos + 1];

        switch (optType) {
            case LCP_OPT_MRU:
                ctx->ourMru = 1500;  // Use default, don't send option
                ctx->ourMruAcked = true;  // Pretend it's ACKed (at default)
                break;
            case LCP_OPT_ACCM:
                ctx->ourAccm = 0xFFFFFFFF;  // Use safe default
                ctx->ourAccmAcked = true;
                ppp->txAccm = 0xFFFFFFFF;   // Always escape all control chars
                break;
            case LCP_OPT_MAGIC_NUMBER:
                ctx->ourMagic = 0;  // Disable magic number
                ctx->ourMagicAcked = true;
                break;
        }

        pos += optLen2;
    }

    // Resend Configure-Request without rejected options
    ctx->configRetries = 0;
    lcpSendConfigRequest(ctx, ppp);
}

// ============================================================================
// Start LCP negotiation
// ============================================================================

void lcpOpen(LcpContext* ctx, PppContext* ppp) {
    ctx->state = LCP_STATE_REQ_SENT;
    ctx->configRetries = 0;
    ctx->identifier = 0;

    // Reset ACK tracking
    ctx->ourMruAcked = false;
    ctx->ourAccmAcked = false;
    ctx->ourMagicAcked = false;

    // Generate new magic number
    ctx->ourMagic = random(1, 0xFFFFFFFF);

    lcpSendConfigRequest(ctx, ppp);
    LCP_DEBUG("LCP: Started negotiation");
}

// ============================================================================
// Close LCP
// ============================================================================

void lcpClose(LcpContext* ctx, PppContext* ppp) {
    uint8_t packet[4];
    packet[0] = LCP_TERMINATE_REQUEST;
    packet[1] = ++ctx->identifier;
    packet[2] = 0;
    packet[3] = 4;  // Length

    pppSendFrame(ppp, PPP_PROTO_LCP, packet, 4);
    ctx->state = LCP_STATE_CLOSING;
    ctx->termRetries = 1;
    ctx->lastSendTime = millis();

    LCP_DEBUG("LCP: Sent Terminate-Request");
}

// ============================================================================
// Process incoming LCP packet
// ============================================================================

void lcpProcessPacket(LcpContext* ctx, PppContext* ppp,
                      uint8_t* data, uint16_t length) {
    if (length < 4) {
        return;  // Packet too short
    }

    uint8_t code = data[0];
    uint8_t id = data[1];
    uint16_t pktLen = ((uint16_t)data[2] << 8) | data[3];

    if (pktLen > length) {
        return;  // Length mismatch
    }

    uint8_t* options = &data[4];
    uint16_t optLen = pktLen - 4;

    LCP_DEBUG_F("LCP: Received code=%d id=%d len=%d\n", code, id, pktLen);

    switch (code) {
        case LCP_CONFIGURE_REQUEST:
            lcpHandleConfigRequest(ctx, ppp, id, options, optLen);
            break;

        case LCP_CONFIGURE_ACK:
            lcpHandleConfigAck(ctx, ppp, id, options, optLen);
            break;

        case LCP_CONFIGURE_NAK:
            lcpHandleConfigNak(ctx, ppp, id, options, optLen);
            break;

        case LCP_CONFIGURE_REJECT:
            lcpHandleConfigReject(ctx, ppp, id, options, optLen);
            break;

        case LCP_TERMINATE_REQUEST:
            lcpSendTerminateAck(ctx, ppp, id);
            ctx->state = LCP_STATE_STOPPED;
            LCP_DEBUG("LCP: Link terminated by peer");
            break;

        case LCP_TERMINATE_ACK:
            if (ctx->state == LCP_STATE_CLOSING || ctx->state == LCP_STATE_STOPPING) {
                ctx->state = LCP_STATE_CLOSED;
                LCP_DEBUG("LCP: Link closed");
            }
            break;

        case LCP_ECHO_REQUEST:
            if (ctx->state == LCP_STATE_OPENED) {
                lcpSendEchoReply(ctx, ppp, id, options, optLen);
            }
            break;

        case LCP_ECHO_REPLY:
            // Could track for keepalive, but we ignore for simplicity
            break;

        case LCP_DISCARD_REQUEST:
            // Silently discard
            break;

        case LCP_CODE_REJECT:
        case LCP_PROTOCOL_REJECT:
            // Log and continue
            LCP_DEBUG_F("LCP: Received Code/Protocol-Reject code=%d\n", code);
            break;

        default:
            // Unknown code - send Code-Reject
            {
                uint8_t reject[256];
                uint16_t rejLen = (length > 248) ? 248 : length;
                reject[0] = LCP_CODE_REJECT;
                reject[1] = ++ctx->identifier;
                uint16_t totalLen = 4 + rejLen;
                reject[2] = (totalLen >> 8) & 0xFF;
                reject[3] = totalLen & 0xFF;
                memcpy(&reject[4], data, rejLen);
                pppSendFrame(ppp, PPP_PROTO_LCP, reject, 4 + rejLen);
            }
            break;
    }
}

// ============================================================================
// Handle timeout
// ============================================================================

bool lcpTimeout(LcpContext* ctx, PppContext* ppp) {
    if (ctx->lastSendTime == 0) {
        return false;
    }

    unsigned long now = millis();
    if (now - ctx->lastSendTime < LcpContext::RESTART_TIMER_MS) {
        return false;  // Not timed out yet
    }

    switch (ctx->state) {
        case LCP_STATE_REQ_SENT:
        case LCP_STATE_ACK_RCVD:
        case LCP_STATE_ACK_SENT:
            if (ctx->configRetries < LcpContext::MAX_CONFIGURE) {
                lcpSendConfigRequest(ctx, ppp);
                return true;
            } else {
                // Too many retries - give up
                ctx->state = LCP_STATE_STOPPED;
                LCP_DEBUG("LCP: Max retries exceeded");
                return true;
            }
            break;

        case LCP_STATE_CLOSING:
        case LCP_STATE_STOPPING:
            if (ctx->termRetries < LcpContext::MAX_TERMINATE) {
                // Resend Terminate-Request
                uint8_t packet[4];
                packet[0] = LCP_TERMINATE_REQUEST;
                packet[1] = ctx->identifier;  // Same ID for retransmit
                packet[2] = 0;
                packet[3] = 4;
                pppSendFrame(ppp, PPP_PROTO_LCP, packet, 4);
                ctx->termRetries++;
                ctx->lastSendTime = now;
                return true;
            } else {
                // Give up on graceful termination
                ctx->state = LCP_STATE_CLOSED;
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

// ============================================================================
// Get state name for debugging
// ============================================================================

const char* lcpStateName(LcpState state) {
    switch (state) {
        case LCP_STATE_INITIAL: return "INITIAL";
        case LCP_STATE_STARTING: return "STARTING";
        case LCP_STATE_CLOSED: return "CLOSED";
        case LCP_STATE_STOPPED: return "STOPPED";
        case LCP_STATE_CLOSING: return "CLOSING";
        case LCP_STATE_STOPPING: return "STOPPING";
        case LCP_STATE_REQ_SENT: return "REQ-SENT";
        case LCP_STATE_ACK_RCVD: return "ACK-RCVD";
        case LCP_STATE_ACK_SENT: return "ACK-SENT";
        case LCP_STATE_OPENED: return "OPENED";
        default: return "UNKNOWN";
    }
}

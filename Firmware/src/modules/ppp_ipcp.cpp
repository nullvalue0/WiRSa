// ============================================================================
// PPP IP Control Protocol (IPCP) Implementation - RFC 1332
// ============================================================================

#include "ppp_ipcp.h"
#include "globals.h"

// ============================================================================
// Debug output
// ============================================================================

#ifdef PPP_DEBUG
#define IPCP_DEBUG(msg) Serial.print(msg); Serial.print("\r\n")
#define IPCP_DEBUG_F(fmt, ...) Serial.printf(fmt "\r\n", ##__VA_ARGS__)
#else
#define IPCP_DEBUG(msg)
#define IPCP_DEBUG_F(fmt, ...)
#endif

// ============================================================================
// Initialize IPCP Context
// ============================================================================

void ipcpInit(IpcpContext* ctx) {
    ctx->state = IPCP_STATE_INITIAL;

    // Default gateway configuration
    ctx->ourIP = IPAddress(192, 168, 8, 1);
    ctx->subnetMask = IPAddress(255, 255, 255, 0);
    ctx->primaryDns = IPAddress(8, 8, 8, 8);       // Google DNS
    ctx->secondaryDns = IPAddress(8, 8, 4, 4);     // Google DNS secondary

    // Client IP (not assigned yet)
    ctx->peerIP = IPAddress(0, 0, 0, 0);
    ctx->peerIPAssigned = false;

    // IP pool defaults
    ctx->pool.poolStart = IPAddress(192, 168, 8, 2);
    for (int i = 0; i < PPP_IP_POOL_SIZE; i++) {
        ctx->pool.allocated[i] = false;
    }
    ctx->pool.currentIndex = -1;

    // Request tracking
    ctx->identifier = 0;
    ctx->configRetries = 0;
    ctx->lastSendTime = 0;
}

// ============================================================================
// Configuration functions
// ============================================================================

void ipcpSetGatewayIP(IpcpContext* ctx, IPAddress ip) {
    ctx->ourIP = ip;
}

void ipcpSetPoolStart(IpcpContext* ctx, IPAddress ip) {
    ctx->pool.poolStart = ip;
}

void ipcpSetDns(IpcpContext* ctx, IPAddress primary, IPAddress secondary) {
    ctx->primaryDns = primary;
    ctx->secondaryDns = secondary;
}

// ============================================================================
// IP Pool Management
// ============================================================================

IPAddress ipcpAllocateIP(IpcpContext* ctx) {
    for (int i = 0; i < PPP_IP_POOL_SIZE; i++) {
        if (!ctx->pool.allocated[i]) {
            ctx->pool.allocated[i] = true;
            ctx->pool.currentIndex = i;

            // Calculate IP from pool start
            IPAddress ip = ctx->pool.poolStart;
            ip[3] += i;  // Add offset to last octet

            IPCP_DEBUG_F("IPCP: Allocated IP %d.%d.%d.%d from pool\n",
                         ip[0], ip[1], ip[2], ip[3]);
            return ip;
        }
    }

    IPCP_DEBUG("IPCP: No IPs available in pool!");
    return IPAddress(0, 0, 0, 0);
}

void ipcpReleaseIP(IpcpContext* ctx) {
    if (ctx->pool.currentIndex >= 0 && ctx->pool.currentIndex < PPP_IP_POOL_SIZE) {
        ctx->pool.allocated[ctx->pool.currentIndex] = false;
        IPCP_DEBUG_F("IPCP: Released IP at pool index %d\n", ctx->pool.currentIndex);
    }
    ctx->pool.currentIndex = -1;
    ctx->peerIP = IPAddress(0, 0, 0, 0);
    ctx->peerIPAssigned = false;
}

// ============================================================================
// Send Configure-Request
// ============================================================================

static void ipcpSendConfigRequest(IpcpContext* ctx, PppContext* ppp) {
    uint8_t packet[32];
    uint16_t pos = 0;

    // IPCP header
    packet[pos++] = IPCP_CONFIGURE_REQUEST;
    packet[pos++] = ++ctx->identifier;
    uint16_t lenPos = pos;
    pos += 2;  // Reserve for length

    // Option: Our IP address
    packet[pos++] = IPCP_OPT_IP_ADDRESS;
    packet[pos++] = 6;
    packet[pos++] = ctx->ourIP[0];
    packet[pos++] = ctx->ourIP[1];
    packet[pos++] = ctx->ourIP[2];
    packet[pos++] = ctx->ourIP[3];

    // Fill in length
    packet[lenPos] = (pos >> 8) & 0xFF;
    packet[lenPos + 1] = pos & 0xFF;

    pppSendFrame(ppp, PPP_PROTO_IPCP, packet, pos);
    ctx->lastSendTime = millis();
    ctx->configRetries++;

    IPCP_DEBUG_F("IPCP: Sent Configure-Request id=%d (our IP=%d.%d.%d.%d)\n",
                 ctx->identifier, ctx->ourIP[0], ctx->ourIP[1],
                 ctx->ourIP[2], ctx->ourIP[3]);
}

// ============================================================================
// Send Configure-Ack
// ============================================================================

static void ipcpSendConfigAck(IpcpContext* ctx, PppContext* ppp,
                               uint8_t id, uint8_t* options, uint16_t optLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = IPCP_CONFIGURE_ACK;
    packet[pos++] = id;
    uint16_t totalLen = 4 + optLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    memcpy(&packet[pos], options, optLen);
    pos += optLen;

    pppSendFrame(ppp, PPP_PROTO_IPCP, packet, pos);
    IPCP_DEBUG_F("IPCP: Sent Configure-Ack id=%d\n", id);
}

// ============================================================================
// Send Configure-Nak (with our suggested values)
// ============================================================================

static void ipcpSendConfigNak(IpcpContext* ctx, PppContext* ppp,
                               uint8_t id, uint8_t* nakOptions, uint16_t nakLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = IPCP_CONFIGURE_NAK;
    packet[pos++] = id;
    uint16_t totalLen = 4 + nakLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    memcpy(&packet[pos], nakOptions, nakLen);
    pos += nakLen;

    pppSendFrame(ppp, PPP_PROTO_IPCP, packet, pos);
    IPCP_DEBUG_F("IPCP: Sent Configure-Nak id=%d\n", id);
}

// ============================================================================
// Send Configure-Reject
// ============================================================================

static void ipcpSendConfigReject(IpcpContext* ctx, PppContext* ppp,
                                  uint8_t id, uint8_t* rejOptions, uint16_t rejLen) {
    uint8_t packet[256];
    uint16_t pos = 0;

    packet[pos++] = IPCP_CONFIGURE_REJECT;
    packet[pos++] = id;
    uint16_t totalLen = 4 + rejLen;
    packet[pos++] = (totalLen >> 8) & 0xFF;
    packet[pos++] = totalLen & 0xFF;

    memcpy(&packet[pos], rejOptions, rejLen);
    pos += rejLen;

    pppSendFrame(ppp, PPP_PROTO_IPCP, packet, pos);
    IPCP_DEBUG_F("IPCP: Sent Configure-Reject id=%d\n", id);
}

// ============================================================================
// Handle incoming Configure-Request
// ============================================================================

static void ipcpHandleConfigRequest(IpcpContext* ctx, PppContext* ppp,
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
            break;  // Malformed
        }

        switch (optType) {
            case IPCP_OPT_IP_ADDRESS: {
                // Client's requested IP address
                IPAddress requestedIP(options[pos + 2], options[pos + 3],
                                      options[pos + 4], options[pos + 5]);

                IPCP_DEBUG_F("IPCP: Client requests IP %d.%d.%d.%d\n",
                             requestedIP[0], requestedIP[1],
                             requestedIP[2], requestedIP[3]);
                IPCP_DEBUG_F("IPCP: Pool start is %d.%d.%d.%d\n",
                             ctx->pool.poolStart[0], ctx->pool.poolStart[1],
                             ctx->pool.poolStart[2], ctx->pool.poolStart[3]);

                // If client requests 0.0.0.0, assign from pool
                if (requestedIP == IPAddress(0, 0, 0, 0)) {
                    IPCP_DEBUG("IPCP: Branch: Client requests 0.0.0.0");
                    // Allocate IP from our pool
                    if (!ctx->peerIPAssigned) {
                        ctx->peerIP = ipcpAllocateIP(ctx);
                        ctx->peerIPAssigned = true;
                    }

                    if (ctx->peerIP != IPAddress(0, 0, 0, 0)) {
                        // NAK with our assigned IP
                        nakOptions[nakLen++] = IPCP_OPT_IP_ADDRESS;
                        nakOptions[nakLen++] = 6;
                        nakOptions[nakLen++] = ctx->peerIP[0];
                        nakOptions[nakLen++] = ctx->peerIP[1];
                        nakOptions[nakLen++] = ctx->peerIP[2];
                        nakOptions[nakLen++] = ctx->peerIP[3];
                    } else {
                        // No IPs available - still NAK with a suggestion
                        nakOptions[nakLen++] = IPCP_OPT_IP_ADDRESS;
                        nakOptions[nakLen++] = 6;
                        nakOptions[nakLen++] = ctx->pool.poolStart[0];
                        nakOptions[nakLen++] = ctx->pool.poolStart[1];
                        nakOptions[nakLen++] = ctx->pool.poolStart[2];
                        nakOptions[nakLen++] = ctx->pool.poolStart[3];
                    }
                }
                // If client requests an IP in our pool range, accept it
                else if (requestedIP[0] == ctx->pool.poolStart[0] &&
                         requestedIP[1] == ctx->pool.poolStart[1] &&
                         requestedIP[2] == ctx->pool.poolStart[2]) {
                    IPCP_DEBUG("IPCP: Branch: Client requests IP in pool range");
                    // Check if it's a valid pool address
                    int offset = requestedIP[3] - ctx->pool.poolStart[3];
                    IPCP_DEBUG_F("IPCP: Offset=%d, PPP_IP_POOL_SIZE=%d\n", offset, PPP_IP_POOL_SIZE);
                    if (offset >= 0 && offset < PPP_IP_POOL_SIZE) {
                        IPCP_DEBUG("IPCP: Offset valid, ACKing IP");
                        ctx->peerIP = requestedIP;
                        ctx->peerIPAssigned = true;
                        ctx->pool.allocated[offset] = true;
                        ctx->pool.currentIndex = offset;

                        // ACK it
                        memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                        ackLen += optLen2;
                    } else {
                        IPCP_DEBUG("IPCP: Offset invalid, NAKing");
                        // NAK with our pool address
                        if (!ctx->peerIPAssigned) {
                            ctx->peerIP = ipcpAllocateIP(ctx);
                            ctx->peerIPAssigned = true;
                        }
                        nakOptions[nakLen++] = IPCP_OPT_IP_ADDRESS;
                        nakOptions[nakLen++] = 6;
                        nakOptions[nakLen++] = ctx->peerIP[0];
                        nakOptions[nakLen++] = ctx->peerIP[1];
                        nakOptions[nakLen++] = ctx->peerIP[2];
                        nakOptions[nakLen++] = ctx->peerIP[3];
                    }
                }
                // Client requests arbitrary IP - NAK with our assigned IP
                else {
                    IPCP_DEBUG("IPCP: Branch: Client requests arbitrary IP, NAKing");
                    if (!ctx->peerIPAssigned) {
                        ctx->peerIP = ipcpAllocateIP(ctx);
                        ctx->peerIPAssigned = true;
                    }
                    nakOptions[nakLen++] = IPCP_OPT_IP_ADDRESS;
                    nakOptions[nakLen++] = 6;
                    nakOptions[nakLen++] = ctx->peerIP[0];
                    nakOptions[nakLen++] = ctx->peerIP[1];
                    nakOptions[nakLen++] = ctx->peerIP[2];
                    nakOptions[nakLen++] = ctx->peerIP[3];
                }
                break;
            }

            case IPCP_OPT_PRIMARY_DNS: {
                // Client wants primary DNS
                IPAddress requestedDns(options[pos + 2], options[pos + 3],
                                       options[pos + 4], options[pos + 5]);

                IPCP_DEBUG_F("IPCP: Client requests Primary DNS %d.%d.%d.%d\n",
                             requestedDns[0], requestedDns[1],
                             requestedDns[2], requestedDns[3]);

                if (requestedDns == IPAddress(0, 0, 0, 0)) {
                    // NAK with our DNS
                    IPCP_DEBUG("IPCP: NAKing Primary DNS with our value");
                    nakOptions[nakLen++] = IPCP_OPT_PRIMARY_DNS;
                    nakOptions[nakLen++] = 6;
                    nakOptions[nakLen++] = ctx->primaryDns[0];
                    nakOptions[nakLen++] = ctx->primaryDns[1];
                    nakOptions[nakLen++] = ctx->primaryDns[2];
                    nakOptions[nakLen++] = ctx->primaryDns[3];
                } else {
                    // Accept any DNS they request (they might want a specific one)
                    IPCP_DEBUG("IPCP: ACKing Primary DNS");
                    memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                    ackLen += optLen2;
                }
                break;
            }

            case IPCP_OPT_SECONDARY_DNS: {
                // Client wants secondary DNS
                IPAddress requestedDns(options[pos + 2], options[pos + 3],
                                       options[pos + 4], options[pos + 5]);

                IPCP_DEBUG_F("IPCP: Client requests Secondary DNS %d.%d.%d.%d\n",
                             requestedDns[0], requestedDns[1],
                             requestedDns[2], requestedDns[3]);

                if (requestedDns == IPAddress(0, 0, 0, 0)) {
                    // NAK with our secondary DNS
                    IPCP_DEBUG("IPCP: NAKing Secondary DNS with our value");
                    nakOptions[nakLen++] = IPCP_OPT_SECONDARY_DNS;
                    nakOptions[nakLen++] = 6;
                    nakOptions[nakLen++] = ctx->secondaryDns[0];
                    nakOptions[nakLen++] = ctx->secondaryDns[1];
                    nakOptions[nakLen++] = ctx->secondaryDns[2];
                    nakOptions[nakLen++] = ctx->secondaryDns[3];
                } else {
                    IPCP_DEBUG("IPCP: ACKing Secondary DNS");
                    memcpy(&ackOptions[ackLen], &options[pos], optLen2);
                    ackLen += optLen2;
                }
                break;
            }

            case IPCP_OPT_IP_COMPRESSION:
                // Reject VJ compression - too complex for our needs
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                IPCP_DEBUG("IPCP: Rejecting IP compression");
                break;

            case IPCP_OPT_IP_ADDRESSES:
                // Deprecated old option - reject
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                break;

            case IPCP_OPT_PRIMARY_NBNS:
            case IPCP_OPT_SECONDARY_NBNS:
                // WINS/NBNS - we could support this but reject for simplicity
                // Some Windows versions may request this but work without it
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                break;

            default:
                // Unknown option - reject
                memcpy(&rejOptions[rejLen], &options[pos], optLen2);
                rejLen += optLen2;
                IPCP_DEBUG_F("IPCP: Rejecting unknown option %d\n", optType);
                break;
        }

        pos += optLen2;
    }

    // Send response (priority: Reject > Nak > Ack)
    IPCP_DEBUG_F("IPCP: Response decision: ackLen=%d, nakLen=%d, rejLen=%d\n",
                 ackLen, nakLen, rejLen);

    if (rejLen > 0) {
        IPCP_DEBUG("IPCP: Sending Configure-Reject (some options rejected)");
        ipcpSendConfigReject(ctx, ppp, id, rejOptions, rejLen);
    } else if (nakLen > 0) {
        IPCP_DEBUG("IPCP: Sending Configure-Nak (some options need different values)");
        ipcpSendConfigNak(ctx, ppp, id, nakOptions, nakLen);
    } else {
        IPCP_DEBUG("IPCP: Sending Configure-Ack (all options acceptable)");
        ipcpSendConfigAck(ctx, ppp, id, ackOptions, ackLen);

        // Update state
        switch (ctx->state) {
            case IPCP_STATE_REQ_SENT:
                ctx->state = IPCP_STATE_ACK_SENT;
                break;
            case IPCP_STATE_ACK_RCVD:
                ctx->state = IPCP_STATE_OPENED;
                IPCP_DEBUG_F("IPCP: OPENED! Client IP=%d.%d.%d.%d\n",
                             ctx->peerIP[0], ctx->peerIP[1],
                             ctx->peerIP[2], ctx->peerIP[3]);
                break;
            default:
                break;
        }
    }
}

// ============================================================================
// Handle incoming Configure-Ack
// ============================================================================

static void ipcpHandleConfigAck(IpcpContext* ctx, PppContext* ppp,
                                 uint8_t id, uint8_t* options, uint16_t optLen) {
    if (id != ctx->identifier) {
        IPCP_DEBUG_F("IPCP: Ignoring Configure-Ack with wrong id=%d\n", id);
        return;
    }

    ctx->configRetries = 0;

    switch (ctx->state) {
        case IPCP_STATE_REQ_SENT:
            ctx->state = IPCP_STATE_ACK_RCVD;
            break;
        case IPCP_STATE_ACK_SENT:
            ctx->state = IPCP_STATE_OPENED;
            IPCP_DEBUG_F("IPCP: OPENED! Client IP=%d.%d.%d.%d\n",
                         ctx->peerIP[0], ctx->peerIP[1],
                         ctx->peerIP[2], ctx->peerIP[3]);
            break;
        default:
            break;
    }
}

// ============================================================================
// Handle incoming Configure-Nak
// ============================================================================

static void ipcpHandleConfigNak(IpcpContext* ctx, PppContext* ppp,
                                 uint8_t id, uint8_t* options, uint16_t optLen) {
    if (id != ctx->identifier) {
        return;
    }

    // Parse NAKed options and adjust
    uint16_t pos = 0;
    while (pos < optLen) {
        uint8_t optType = options[pos];
        uint8_t optLen2 = options[pos + 1];

        switch (optType) {
            case IPCP_OPT_IP_ADDRESS:
                // Peer wants us to use a different IP
                ctx->ourIP = IPAddress(options[pos + 2], options[pos + 3],
                                       options[pos + 4], options[pos + 5]);
                IPCP_DEBUG_F("IPCP: Peer NAKed our IP, using %d.%d.%d.%d\n",
                             ctx->ourIP[0], ctx->ourIP[1],
                             ctx->ourIP[2], ctx->ourIP[3]);
                break;
        }

        pos += optLen2;
    }

    // Resend with adjusted values
    ctx->configRetries = 0;
    ipcpSendConfigRequest(ctx, ppp);
}

// ============================================================================
// Handle incoming Configure-Reject
// ============================================================================

static void ipcpHandleConfigReject(IpcpContext* ctx, PppContext* ppp,
                                    uint8_t id, uint8_t* options, uint16_t optLen) {
    if (id != ctx->identifier) {
        return;
    }

    // For our minimal implementation, if IP address is rejected, that's a problem
    // Just try again without the rejected options (though IP is required)
    ctx->configRetries = 0;
    ipcpSendConfigRequest(ctx, ppp);
}

// ============================================================================
// Start IPCP negotiation
// ============================================================================

void ipcpOpen(IpcpContext* ctx, PppContext* ppp) {
    ctx->state = IPCP_STATE_REQ_SENT;
    ctx->configRetries = 0;
    ctx->identifier = 0;
    ctx->peerIPAssigned = false;

    ipcpSendConfigRequest(ctx, ppp);
    IPCP_DEBUG("IPCP: Started negotiation");
}

// ============================================================================
// Close IPCP
// ============================================================================

void ipcpClose(IpcpContext* ctx, PppContext* ppp) {
    // Release IP back to pool
    ipcpReleaseIP(ctx);

    uint8_t packet[4];
    packet[0] = IPCP_TERMINATE_REQUEST;
    packet[1] = ++ctx->identifier;
    packet[2] = 0;
    packet[3] = 4;

    pppSendFrame(ppp, PPP_PROTO_IPCP, packet, 4);
    ctx->state = IPCP_STATE_CLOSING;
    ctx->lastSendTime = millis();

    IPCP_DEBUG("IPCP: Sent Terminate-Request");
}

// ============================================================================
// Process incoming IPCP packet
// ============================================================================

void ipcpProcessPacket(IpcpContext* ctx, PppContext* ppp,
                       uint8_t* data, uint16_t length) {
    if (length < 4) {
        return;
    }

    uint8_t code = data[0];
    uint8_t id = data[1];
    uint16_t pktLen = ((uint16_t)data[2] << 8) | data[3];

    if (pktLen > length) {
        return;
    }

    uint8_t* options = &data[4];
    uint16_t optLen = pktLen - 4;

    IPCP_DEBUG_F("IPCP: Received code=%d id=%d len=%d\n", code, id, pktLen);

    switch (code) {
        case IPCP_CONFIGURE_REQUEST:
            ipcpHandleConfigRequest(ctx, ppp, id, options, optLen);
            break;

        case IPCP_CONFIGURE_ACK:
            ipcpHandleConfigAck(ctx, ppp, id, options, optLen);
            break;

        case IPCP_CONFIGURE_NAK:
            ipcpHandleConfigNak(ctx, ppp, id, options, optLen);
            break;

        case IPCP_CONFIGURE_REJECT:
            ipcpHandleConfigReject(ctx, ppp, id, options, optLen);
            break;

        case IPCP_TERMINATE_REQUEST:
            // Send ACK and transition to stopped
            {
                uint8_t packet[4];
                packet[0] = IPCP_TERMINATE_ACK;
                packet[1] = id;
                packet[2] = 0;
                packet[3] = 4;
                pppSendFrame(ppp, PPP_PROTO_IPCP, packet, 4);
            }
            ipcpReleaseIP(ctx);
            ctx->state = IPCP_STATE_STOPPED;
            IPCP_DEBUG("IPCP: Terminated by peer");
            break;

        case IPCP_TERMINATE_ACK:
            if (ctx->state == IPCP_STATE_CLOSING) {
                ctx->state = IPCP_STATE_CLOSED;
                IPCP_DEBUG("IPCP: Closed");
            }
            break;

        default:
            IPCP_DEBUG_F("IPCP: Unknown code %d\n", code);
            break;
    }
}

// ============================================================================
// Handle timeout
// ============================================================================

bool ipcpTimeout(IpcpContext* ctx, PppContext* ppp) {
    if (ctx->lastSendTime == 0) {
        return false;
    }

    unsigned long now = millis();
    if (now - ctx->lastSendTime < IpcpContext::RESTART_TIMER_MS) {
        return false;
    }

    switch (ctx->state) {
        case IPCP_STATE_REQ_SENT:
        case IPCP_STATE_ACK_RCVD:
        case IPCP_STATE_ACK_SENT:
            if (ctx->configRetries < IpcpContext::MAX_CONFIGURE) {
                ipcpSendConfigRequest(ctx, ppp);
                return true;
            } else {
                ctx->state = IPCP_STATE_STOPPED;
                IPCP_DEBUG("IPCP: Max retries exceeded");
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

// ============================================================================
// Get state name
// ============================================================================

const char* ipcpStateName(IpcpState state) {
    switch (state) {
        case IPCP_STATE_INITIAL: return "INITIAL";
        case IPCP_STATE_STARTING: return "STARTING";
        case IPCP_STATE_CLOSED: return "CLOSED";
        case IPCP_STATE_STOPPED: return "STOPPED";
        case IPCP_STATE_CLOSING: return "CLOSING";
        case IPCP_STATE_STOPPING: return "STOPPING";
        case IPCP_STATE_REQ_SENT: return "REQ-SENT";
        case IPCP_STATE_ACK_RCVD: return "ACK-RCVD";
        case IPCP_STATE_ACK_SENT: return "ACK-SENT";
        case IPCP_STATE_OPENED: return "OPENED";
        default: return "UNKNOWN";
    }
}

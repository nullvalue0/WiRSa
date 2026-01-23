// ============================================================================
// PPP NAT Engine Implementation
// ============================================================================
// Handles NAT translation between PPP client and WiFi
// Independent implementation (not shared with SLIP)
// ============================================================================

#include "ppp_nat.h"
#include "ppp.h"
#include "globals.h"
#include "serial_io.h"

// lwIP includes for raw ICMP socket
extern "C" {
#include "lwip/raw.h"
#include "lwip/ip_addr.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
}

// Global context pointer for ICMP callback (lwIP callbacks don't have user data)
static PppNatContext* g_icmpNatCtx = nullptr;
static PppContext* g_icmpPppCtx = nullptr;

// ============================================================================
// Debug output (controlled by usbDebug setting)
// ============================================================================

extern bool usbDebug;

#ifdef PPP_DEBUG
#define NAT_DEBUG(msg) if (usbDebug) { UsbDebugPrintLn(msg); }
#define NAT_DEBUG_F(fmt, ...) if (usbDebug) { UsbDebugPrint(""); Serial.printf(fmt "\r\n", ##__VA_ARGS__); }
#else
#define NAT_DEBUG(msg)
#define NAT_DEBUG_F(fmt, ...)
#endif

// ============================================================================
// Forward declarations
// ============================================================================

static void pppNatProcessTcp(PppNatContext* ctx, PppContext* ppp,
                              uint8_t* packet, uint16_t length,
                              PppIpHeader* ip, uint8_t ipHeaderLen);
static void pppNatProcessUdp(PppNatContext* ctx, PppContext* ppp,
                              uint8_t* packet, uint16_t length,
                              PppIpHeader* ip, uint8_t ipHeaderLen);
static void pppNatProcessIcmp(PppNatContext* ctx, PppContext* ppp,
                               uint8_t* packet, uint16_t length,
                               PppIpHeader* ip, uint8_t ipHeaderLen);

static PppNatTcpEntry* pppNatFindTcpEntry(PppNatContext* ctx, IPAddress srcIP,
                                           uint16_t srcPort, IPAddress dstIP,
                                           uint16_t dstPort);
static PppNatTcpEntry* pppNatCreateTcpEntry(PppNatContext* ctx, IPAddress srcIP,
                                             uint16_t srcPort, IPAddress dstIP,
                                             uint16_t dstPort);
static PppNatUdpEntry* pppNatFindOrCreateUdp(PppNatContext* ctx, IPAddress srcIP,
                                              uint16_t srcPort, IPAddress dstIP,
                                              uint16_t dstPort);
static void pppNatSendTcpToClient(PppNatContext* ctx, PppContext* ppp,
                                   PppNatTcpEntry* entry, uint8_t* data,
                                   uint16_t length, uint8_t flags);
static void pppNatSendUdpToClient(PppNatContext* ctx, PppContext* ppp,
                                   PppNatUdpEntry* entry, uint8_t* data,
                                   uint16_t length);
static void pppNatSendIcmpToClient(PppNatContext* ctx, PppContext* ppp,
                                    PppNatIcmpEntry* entry, uint8_t type,
                                    uint8_t code, uint8_t* data, uint16_t length);
static u8_t pppNatIcmpRecvCallback(void* arg, struct raw_pcb* pcb,
                                    struct pbuf* p, const ip_addr_t* addr);

// ============================================================================
// Initialize NAT Context
// ============================================================================

void pppNatInit(PppNatContext* ctx) {
    ctx->gatewayIP = IPAddress(192, 168, 8, 1);
    ctx->clientIP = IPAddress(192, 168, 8, 2);
    ctx->subnetMask = IPAddress(255, 255, 255, 0);
    ctx->dnsServer = IPAddress(8, 8, 8, 8);

    // Initialize TCP table
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        ctx->tcpTable[i].active = false;
        ctx->tcpTable[i].client = nullptr;
    }

    // Initialize UDP table
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        ctx->udpTable[i].active = false;
    }

    // Initialize ICMP table
    for (int i = 0; i < PPP_NAT_MAX_ICMP; i++) {
        ctx->icmpTable[i].active = false;
    }

    ctx->udpSocketBound = false;
    ctx->nextPort = PPP_NAT_PORT_START;

    // Create ICMP raw socket for ping NAT
    ctx->icmpPcb = nullptr;
    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (pcb) {
        raw_recv(pcb, pppNatIcmpRecvCallback, ctx);
        raw_bind(pcb, IP_ADDR_ANY);
        ctx->icmpPcb = pcb;
        NAT_DEBUG("NAT: ICMP raw socket created");
    } else {
        NAT_DEBUG("NAT: Failed to create ICMP raw socket");
    }

    // Set global context for ICMP callback
    g_icmpNatCtx = ctx;

    // Statistics
    ctx->packetsToInternet = 0;
    ctx->packetsFromInternet = 0;
    ctx->droppedPackets = 0;
    ctx->tcpConnections = 0;
    ctx->udpSessions = 0;
    ctx->icmpPackets = 0;
}

// ============================================================================
// Set IP Configuration
// ============================================================================

void pppNatSetIPs(PppNatContext* ctx, IPAddress gateway, IPAddress client,
                  IPAddress subnet, IPAddress dns) {
    ctx->gatewayIP = gateway;
    ctx->clientIP = client;
    ctx->subnetMask = subnet;
    ctx->dnsServer = dns;
}

// ============================================================================
// Shutdown NAT
// ============================================================================

void pppNatShutdown(PppNatContext* ctx) {
    // Close all TCP connections
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        if (ctx->tcpTable[i].active) {
            if (ctx->tcpTable[i].client) {
                ctx->tcpTable[i].client->stop();
                delete ctx->tcpTable[i].client;
                ctx->tcpTable[i].client = nullptr;
            }
            ctx->tcpTable[i].active = false;
        }
    }

    // Clear UDP sessions
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        ctx->udpTable[i].active = false;
    }

    // Clear ICMP sessions
    for (int i = 0; i < PPP_NAT_MAX_ICMP; i++) {
        ctx->icmpTable[i].active = false;
    }

    if (ctx->udpSocketBound) {
        ctx->udpSocket.stop();
        ctx->udpSocketBound = false;
    }

    // Close ICMP raw socket
    if (ctx->icmpPcb) {
        raw_remove((struct raw_pcb*)ctx->icmpPcb);
        ctx->icmpPcb = nullptr;
    }
    g_icmpNatCtx = nullptr;
    g_icmpPppCtx = nullptr;
}

// ============================================================================
// IP Checksum Calculation
// ============================================================================

uint16_t pppIpChecksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0;

    for (uint16_t i = 0; i < length - 1; i += 2) {
        sum += (data[i] << 8) | data[i + 1];
    }

    if (length & 1) {
        sum += data[length - 1] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// ============================================================================
// Recalculate IP Header Checksum
// ============================================================================

void pppIpRecalcChecksum(PppIpHeader* ip) {
    uint8_t headerLen = (ip->versionIhl & 0x0F) * 4;
    ip->checksum = 0;
    ip->checksum = htons(pppIpChecksum((uint8_t*)ip, headerLen));
}

// ============================================================================
// TCP/UDP Pseudo-header Checksum
// ============================================================================

uint16_t pppTcpUdpChecksum(PppIpHeader* ip, const uint8_t* payload, uint16_t payloadLen) {
    uint32_t sum = 0;

    // Pseudo-header: source IP
    sum += (ntohl(ip->srcIP) >> 16) & 0xFFFF;
    sum += ntohl(ip->srcIP) & 0xFFFF;

    // Pseudo-header: destination IP
    sum += (ntohl(ip->dstIP) >> 16) & 0xFFFF;
    sum += ntohl(ip->dstIP) & 0xFFFF;

    // Pseudo-header: protocol and length
    sum += ip->protocol;
    sum += payloadLen;

    // Sum payload
    for (uint16_t i = 0; i < payloadLen - 1; i += 2) {
        sum += (payload[i] << 8) | payload[i + 1];
    }
    if (payloadLen & 1) {
        sum += payload[payloadLen - 1] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// ============================================================================
// Process Incoming IP Packet from PPP Client
// ============================================================================

void pppNatProcessPacket(PppNatContext* ctx, PppContext* ppp,
                         uint8_t* packet, uint16_t length) {
    if (length < 20) {
        ctx->droppedPackets++;
        return;
    }

    PppIpHeader* ip = (PppIpHeader*)packet;

    // Check IP version
    uint8_t version = (ip->versionIhl >> 4) & 0x0F;
    if (version != 4) {
        ctx->droppedPackets++;
        return;
    }

    // Get header length
    uint8_t headerLen = (ip->versionIhl & 0x0F) * 4;
    if (headerLen < 20 || headerLen > length) {
        ctx->droppedPackets++;
        return;
    }

    // Verify checksum
    uint16_t originalChecksum = ip->checksum;
    ip->checksum = 0;
    uint16_t calculatedChecksum = htons(pppIpChecksum(packet, headerLen));
    ip->checksum = originalChecksum;

    if (originalChecksum != calculatedChecksum) {
        ctx->droppedPackets++;
        NAT_DEBUG("NAT: IP checksum error");
        return;
    }

    // Get total length
    uint16_t totalLength = ntohs(ip->totalLength);
    if (totalLength > length) {
        ctx->droppedPackets++;
        return;
    }

    // Debug: show packet info (IPs are in network byte order in header)
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    NAT_DEBUG_F("NAT: proto=%d src=%d.%d.%d.%d dst=%d.%d.%d.%d len=%d",
                ip->protocol,
                srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3],
                dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3],
                totalLength);

    // Route based on protocol
    switch (ip->protocol) {
        case PPP_IP_PROTO_TCP:
            pppNatProcessTcp(ctx, ppp, packet, totalLength, ip, headerLen);
            break;
        case PPP_IP_PROTO_UDP:
            pppNatProcessUdp(ctx, ppp, packet, totalLength, ip, headerLen);
            break;
        case PPP_IP_PROTO_ICMP:
            NAT_DEBUG_F("NAT: ICMP type=%d, our gateway=%d.%d.%d.%d",
                        packet[headerLen],  // ICMP type is first byte after IP header
                        ctx->gatewayIP[0], ctx->gatewayIP[1],
                        ctx->gatewayIP[2], ctx->gatewayIP[3]);
            pppNatProcessIcmp(ctx, ppp, packet, totalLength, ip, headerLen);
            break;
        default:
            NAT_DEBUG_F("NAT: Unknown protocol %d dropped", ip->protocol);
            ctx->droppedPackets++;
            break;
    }
}

// ============================================================================
// Find TCP Entry
// ============================================================================

static PppNatTcpEntry* pppNatFindTcpEntry(PppNatContext* ctx, IPAddress srcIP,
                                           uint16_t srcPort, IPAddress dstIP,
                                           uint16_t dstPort) {
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        PppNatTcpEntry* e = &ctx->tcpTable[i];
        if (e->active &&
            e->srcIP == srcIP && e->srcPort == srcPort &&
            e->dstIP == dstIP && e->dstPort == dstPort) {
            return e;
        }
    }
    return nullptr;
}

// ============================================================================
// Create TCP Entry
// ============================================================================

static PppNatTcpEntry* pppNatCreateTcpEntry(PppNatContext* ctx, IPAddress srcIP,
                                             uint16_t srcPort, IPAddress dstIP,
                                             uint16_t dstPort) {
    // Find free slot
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        if (!ctx->tcpTable[i].active) {
            PppNatTcpEntry* e = &ctx->tcpTable[i];
            e->active = true;
            e->state = PPP_NAT_TCP_CLOSED;
            e->srcIP = srcIP;
            e->srcPort = srcPort;
            e->dstIP = dstIP;
            e->dstPort = dstPort;
            e->natPort = ctx->nextPort++;
            if (ctx->nextPort > PPP_NAT_PORT_END) {
                ctx->nextPort = PPP_NAT_PORT_START;
            }
            e->client = nullptr;
            e->clientSeq = 0;
            e->clientAck = 0;
            e->serverSeq = random(1, 0x7FFFFFFF);
            e->serverAck = 0;
            e->created = millis();
            e->lastActivity = millis();
            e->lastKeepalive = millis();
            e->lastServerData = millis();
            return e;
        }
    }
    return nullptr;
}

// ============================================================================
// Process TCP Packet
// ============================================================================

static void pppNatProcessTcp(PppNatContext* ctx, PppContext* ppp,
                              uint8_t* packet, uint16_t length,
                              PppIpHeader* ip, uint8_t ipHeaderLen) {
    if (length < ipHeaderLen + 20) {
        ctx->droppedPackets++;
        return;
    }

    PppTcpHeader* tcp = (PppTcpHeader*)(packet + ipHeaderLen);
    uint16_t srcPort = ntohs(tcp->srcPort);
    uint16_t dstPort = ntohs(tcp->dstPort);
    // Construct IPs from network byte order
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    IPAddress dstIP(dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3]);
    IPAddress srcIP(srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3]);

    uint8_t tcpHeaderLen = ((tcp->dataOffset >> 4) & 0x0F) * 4;
    if (tcpHeaderLen < 20 || ipHeaderLen + tcpHeaderLen > length) {
        ctx->droppedPackets++;
        return;
    }

    uint8_t flags = tcp->flags;
    uint16_t payloadLen = length - ipHeaderLen - tcpHeaderLen;
    uint8_t* payload = packet + ipHeaderLen + tcpHeaderLen;

    PppNatTcpEntry* entry = pppNatFindTcpEntry(ctx, srcIP, srcPort, dstIP, dstPort);

    // Handle SYN - new connection
    if (flags & PPP_TCP_FLAG_SYN) {
        if (!entry) {
            // Check if there's a stuck connection to the same destination from same client
            // This helps with reconnection scenarios where the old connection is stuck
            for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
                PppNatTcpEntry* e = &ctx->tcpTable[i];
                if (e->active && e->srcIP == srcIP && e->dstIP == dstIP && e->dstPort == dstPort) {
                    // Found old connection to same destination, close it
                    uint32_t inFlight = e->serverSeq - e->serverAck;
                    NAT_DEBUG_F("NAT: Closing stuck connection to same dest (port %d, inFlight=%u)",
                                e->srcPort, inFlight);
                    if (e->client) {
                        e->client->stop();
                        delete e->client;
                        e->client = nullptr;
                    }
                    e->active = false;
                }
            }

            entry = pppNatCreateTcpEntry(ctx, srcIP, srcPort, dstIP, dstPort);
            if (!entry) {
                ctx->droppedPackets++;
                NAT_DEBUG("NAT: TCP table full");
                return;
            }
        }

        entry->clientSeq = ntohl(tcp->seqNum) + 1;  // +1 because SYN consumes 1 seq number
        entry->clientAck = ntohl(tcp->ackNum);

        if (entry->state == PPP_NAT_TCP_CLOSED) {
            entry->state = PPP_NAT_TCP_SYN_SENT;
            entry->created = millis();
            entry->lastActivity = millis();
            entry->lastKeepalive = millis();

            if (!entry->client) {
                entry->client = new WiFiClient();
            }

            NAT_DEBUG_F("NAT: TCP connecting to %d.%d.%d.%d:%d\n",
                        dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort);

            if (entry->client->connect(dstIP, dstPort)) {
                entry->state = PPP_NAT_TCP_ESTABLISHED;
                ctx->packetsToInternet++;
                ctx->tcpConnections++;
                NAT_DEBUG("NAT: TCP connected");

                // Initialize serverAck for flow control (will be updated when client ACKs)
                entry->serverAck = entry->serverSeq;

                // Send SYN-ACK
                pppNatSendTcpToClient(ctx, ppp, entry, nullptr, 0,
                                      PPP_TCP_FLAG_SYN | PPP_TCP_FLAG_ACK);
            } else {
                NAT_DEBUG("NAT: TCP connect failed");
                // Send RST
                pppNatSendTcpToClient(ctx, ppp, entry, nullptr, 0, PPP_TCP_FLAG_RST);
                entry->active = false;
                delete entry->client;
                entry->client = nullptr;
            }
        }
        return;
    }

    if (!entry) {
        ctx->droppedPackets++;
        NAT_DEBUG_F("NAT: TCP no entry for %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d",
                    srcIP[0], srcIP[1], srcIP[2], srcIP[3], srcPort,
                    dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort);
        return;
    }

    entry->lastActivity = millis();

    // Handle FIN
    if (flags & PPP_TCP_FLAG_FIN) {
        entry->state = PPP_NAT_TCP_FIN_WAIT;
        if (entry->client) {
            entry->client->stop();
        }
        pppNatSendTcpToClient(ctx, ppp, entry, nullptr, 0,
                              PPP_TCP_FLAG_FIN | PPP_TCP_FLAG_ACK);
        return;
    }

    // Handle RST
    if (flags & PPP_TCP_FLAG_RST) {
        if (entry->client) {
            entry->client->stop();
            delete entry->client;
            entry->client = nullptr;
        }
        entry->active = false;
        return;
    }

    // Handle data from client (e.g., PONG responses, chat messages)
    if (payloadLen > 0 && entry->state == PPP_NAT_TCP_ESTABLISHED) {
        if (entry->client && entry->client->connected()) {
            size_t written = entry->client->write(payload, payloadLen);
            if (written != payloadLen) {
                NAT_DEBUG_F("NAT: TCP client->server write failed: %d/%d bytes", written, payloadLen);
            } else {
                NAT_DEBUG_F("NAT: TCP client->server: %d bytes", payloadLen);
            }
            entry->clientSeq = ntohl(tcp->seqNum) + payloadLen;
            ctx->packetsToInternet++;
            entry->lastActivity = millis();  // Client activity resets timeout

            // Send ACK
            pppNatSendTcpToClient(ctx, ppp, entry, nullptr, 0, PPP_TCP_FLAG_ACK);
        } else {
            NAT_DEBUG_F("NAT: TCP client->server failed: not connected");
        }
    }

    // Client ACKs also count as activity
    if (flags & PPP_TCP_FLAG_ACK) {
        entry->lastActivity = millis();
        // Client's ACK acknowledges data WE sent (serverSeq)
        uint32_t newAck = ntohl(tcp->ackNum);
        if (newAck != entry->serverAck) {
            NAT_DEBUG_F("NAT: TCP ACK update: serverAck %u -> %u (serverSeq=%u)",
                        entry->serverAck, newAck, entry->serverSeq);
            entry->serverAck = newAck;
        } else {
            // Same ACK received - only a concern if we have data in flight
            uint32_t inFlight = entry->serverSeq - entry->serverAck;
            if (inFlight > 0) {
                // Duplicate ACK with data in flight may indicate packet loss
                NAT_DEBUG_F("NAT: TCP duplicate ACK at %u (inFlight=%u) - possible packet loss",
                            newAck, inFlight);
            }
            // If inFlight == 0, this is normal - client is piggybacking ACK with its data
        }
    }
}

// ============================================================================
// Send TCP Packet to PPP Client
// ============================================================================

static void pppNatSendTcpToClient(PppNatContext* ctx, PppContext* ppp,
                                   PppNatTcpEntry* entry, uint8_t* data,
                                   uint16_t length, uint8_t flags) {
    uint8_t packet[60 + 1460];  // Max IP header + TCP header + data
    uint16_t ipHeaderLen = 20;
    uint16_t tcpHeaderLen = 20;
    uint16_t totalLen = ipHeaderLen + tcpHeaderLen + length;

    if (totalLen > sizeof(packet)) {
        return;
    }

    // Build IP header
    PppIpHeader* ip = (PppIpHeader*)packet;
    ip->versionIhl = 0x45;  // IPv4, 20 byte header
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);  // Don't fragment
    ip->ttl = 64;
    ip->protocol = PPP_IP_PROTO_TCP;
    ip->checksum = 0;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);

    // Build TCP header
    PppTcpHeader* tcp = (PppTcpHeader*)(packet + ipHeaderLen);
    tcp->srcPort = htons(entry->dstPort);
    tcp->dstPort = htons(entry->srcPort);
    tcp->seqNum = htonl(entry->serverSeq);
    tcp->ackNum = htonl(entry->clientSeq);  // clientSeq is already "next expected byte"
    tcp->dataOffset = 0x50;  // 20 byte header
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->checksum = 0;
    tcp->urgentPtr = 0;

    // Copy data if any
    if (data && length > 0) {
        memcpy(packet + ipHeaderLen + tcpHeaderLen, data, length);
    }

    // Update sequence for next packet
    if (flags & PPP_TCP_FLAG_SYN) {
        entry->serverSeq++;
    }
    if (flags & PPP_TCP_FLAG_FIN) {
        entry->serverSeq++;
    }
    if (length > 0) {
        entry->serverSeq += length;
    }

    // Calculate checksums
    pppIpRecalcChecksum(ip);
    tcp->checksum = htons(pppTcpUdpChecksum(ip, (uint8_t*)tcp, tcpHeaderLen + length));

    // Send via PPP
    pppSendFrame(ppp, PPP_PROTO_IP, packet, totalLen);
    ctx->packetsFromInternet++;
}

// ============================================================================
// Find or Create UDP Entry
// ============================================================================

static PppNatUdpEntry* pppNatFindOrCreateUdp(PppNatContext* ctx, IPAddress srcIP,
                                              uint16_t srcPort, IPAddress dstIP,
                                              uint16_t dstPort) {
    // Find existing
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        PppNatUdpEntry* e = &ctx->udpTable[i];
        if (e->active &&
            e->srcIP == srcIP && e->srcPort == srcPort &&
            e->dstIP == dstIP && e->dstPort == dstPort) {
            return e;
        }
    }

    // Create new
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        if (!ctx->udpTable[i].active) {
            PppNatUdpEntry* e = &ctx->udpTable[i];
            e->active = true;
            e->srcIP = srcIP;
            e->srcPort = srcPort;
            e->dstIP = dstIP;
            e->dstPort = dstPort;
            e->natPort = ctx->nextPort++;
            if (ctx->nextPort > PPP_NAT_PORT_END) {
                ctx->nextPort = PPP_NAT_PORT_START;
            }
            e->lastActivity = millis();
            ctx->udpSessions++;
            return e;
        }
    }

    return nullptr;
}

// ============================================================================
// Process UDP Packet
// ============================================================================

static void pppNatProcessUdp(PppNatContext* ctx, PppContext* ppp,
                              uint8_t* packet, uint16_t length,
                              PppIpHeader* ip, uint8_t ipHeaderLen) {
    if (length < ipHeaderLen + 8) {
        ctx->droppedPackets++;
        return;
    }

    PppUdpHeader* udp = (PppUdpHeader*)(packet + ipHeaderLen);
    uint16_t srcPort = ntohs(udp->srcPort);
    uint16_t dstPort = ntohs(udp->dstPort);
    uint16_t udpLen = ntohs(udp->length);
    // Construct IPs from network byte order
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    IPAddress dstIP(dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3]);
    IPAddress srcIP(srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3]);

    if (udpLen < 8 || ipHeaderLen + udpLen > length) {
        ctx->droppedPackets++;
        return;
    }

    uint16_t payloadLen = udpLen - 8;
    uint8_t* payload = packet + ipHeaderLen + 8;

    PppNatUdpEntry* entry = pppNatFindOrCreateUdp(ctx, srcIP, srcPort, dstIP, dstPort);
    if (!entry) {
        ctx->droppedPackets++;
        NAT_DEBUG("NAT: UDP table full");
        return;
    }

    entry->lastActivity = millis();

    // Bind UDP socket if not already
    if (!ctx->udpSocketBound) {
        if (ctx->udpSocket.begin(entry->natPort)) {
            ctx->udpSocketBound = true;
        } else {
            ctx->droppedPackets++;
            return;
        }
    }

    // Send UDP packet
    if (ctx->udpSocket.beginPacket(dstIP, dstPort)) {
        ctx->udpSocket.write(payload, payloadLen);
        ctx->udpSocket.endPacket();
        ctx->packetsToInternet++;
        NAT_DEBUG_F("NAT: UDP sent to %d.%d.%d.%d:%d",
                    dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort);
    }
}

// ============================================================================
// Send UDP Packet to PPP Client
// ============================================================================

static void pppNatSendUdpToClient(PppNatContext* ctx, PppContext* ppp,
                                   PppNatUdpEntry* entry, uint8_t* data,
                                   uint16_t length) {
    uint8_t packet[60 + 1472];  // Max IP + UDP header + data
    uint16_t ipHeaderLen = 20;
    uint16_t udpHeaderLen = 8;
    uint16_t totalLen = ipHeaderLen + udpHeaderLen + length;

    if (totalLen > sizeof(packet)) {
        return;
    }

    // Build IP header
    PppIpHeader* ip = (PppIpHeader*)packet;
    ip->versionIhl = 0x45;
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = PPP_IP_PROTO_UDP;
    ip->checksum = 0;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);

    // Build UDP header
    PppUdpHeader* udp = (PppUdpHeader*)(packet + ipHeaderLen);
    udp->srcPort = htons(entry->dstPort);
    udp->dstPort = htons(entry->srcPort);
    udp->length = htons(udpHeaderLen + length);
    udp->checksum = 0;  // Optional in UDP over IPv4

    // Copy data
    if (data && length > 0) {
        memcpy(packet + ipHeaderLen + udpHeaderLen, data, length);
    }

    // Calculate IP checksum
    pppIpRecalcChecksum(ip);

    // Send via PPP
    pppSendFrame(ppp, PPP_PROTO_IP, packet, totalLen);
    ctx->packetsFromInternet++;
}

// ============================================================================
// Send ICMP Packet to PPP Client
// ============================================================================

static void pppNatSendIcmpToClient(PppNatContext* ctx, PppContext* ppp,
                                    PppNatIcmpEntry* entry, uint8_t type,
                                    uint8_t code, uint8_t* data, uint16_t length) {
    uint8_t packet[60 + 1472];  // Max IP + ICMP header + data
    uint16_t ipHeaderLen = 20;
    uint16_t icmpHeaderLen = 8;
    uint16_t totalLen = ipHeaderLen + icmpHeaderLen + length;

    if (totalLen > sizeof(packet)) {
        return;
    }

    // Build IP header
    PppIpHeader* ip = (PppIpHeader*)packet;
    ip->versionIhl = 0x45;
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = PPP_IP_PROTO_ICMP;
    ip->checksum = 0;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);

    // Build ICMP header
    PppIcmpHeader* icmp = (PppIcmpHeader*)(packet + ipHeaderLen);
    icmp->type = type;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->id = htons(entry->id);
    icmp->sequence = htons(entry->sequence);

    // Copy data (ICMP payload after the 8-byte header)
    if (data && length > 0) {
        memcpy(packet + ipHeaderLen + icmpHeaderLen, data, length);
    }

    // Calculate ICMP checksum (covers header + data)
    uint16_t icmpTotalLen = icmpHeaderLen + length;
    icmp->checksum = htons(pppIpChecksum((uint8_t*)icmp, icmpTotalLen));

    // Calculate IP checksum
    pppIpRecalcChecksum(ip);

    // Send via PPP
    pppSendFrame(ppp, PPP_PROTO_IP, packet, totalLen);
    ctx->packetsFromInternet++;
}

// ============================================================================
// ICMP Raw Socket Receive Callback
// ============================================================================

// Pending ICMP reply buffer (for deferred processing from callback)
static struct {
    bool pending;
    IPAddress srcIP;
    uint16_t id;
    uint16_t seq;
    uint8_t data[64];   // Ping payload (typically 32 bytes)
    uint16_t dataLen;
} g_pendingIcmpReply = {false};

static u8_t pppNatIcmpRecvCallback(void* arg, struct raw_pcb* pcb,
                                    struct pbuf* p, const ip_addr_t* addr) {
    (void)arg;  // Use global context instead to avoid race conditions

    // pbuf includes IP header - we need to skip it
    // IP header is at least 20 bytes, check IHL for actual size
    if (p->tot_len < 28) {  // Min IP (20) + Min ICMP (8)
        return 0;
    }

    uint8_t* ipData = (uint8_t*)p->payload;
    uint8_t ipHeaderLen = (ipData[0] & 0x0F) * 4;

    if (p->tot_len < ipHeaderLen + 8) {
        return 0;
    }

    // Get ICMP header after IP header
    struct icmp_echo_hdr* icmpHdr = (struct icmp_echo_hdr*)(ipData + ipHeaderLen);

    // Only handle echo reply (type 0)
    if (icmpHdr->type != 0) {
        return 0;  // Not an echo reply, don't consume
    }

    uint16_t icmpId = ntohs(icmpHdr->id);
    uint16_t icmpSeq = ntohs(icmpHdr->seqno);

    // Get source IP from addr (ESP32 lwIP uses ip_addr_t with u_addr.ip4)
    const ip4_addr_t* ip4 = &addr->u_addr.ip4;
    IPAddress srcIP((ip4->addr >> 0) & 0xFF, (ip4->addr >> 8) & 0xFF,
                    (ip4->addr >> 16) & 0xFF, (ip4->addr >> 24) & 0xFF);

    // Store reply info for deferred processing (can't call pppSendFrame from callback)
    if (!g_pendingIcmpReply.pending) {
        g_pendingIcmpReply.pending = true;
        g_pendingIcmpReply.srcIP = srcIP;
        g_pendingIcmpReply.id = icmpId;
        g_pendingIcmpReply.seq = icmpSeq;

        // Copy ping payload (after 8-byte ICMP header)
        uint16_t payloadLen = p->tot_len - ipHeaderLen - 8;
        if (payloadLen > sizeof(g_pendingIcmpReply.data)) {
            payloadLen = sizeof(g_pendingIcmpReply.data);
        }
        g_pendingIcmpReply.dataLen = payloadLen;
        if (payloadLen > 0) {
            pbuf_copy_partial(p, g_pendingIcmpReply.data, payloadLen, ipHeaderLen + 8);
        }
    }

    return 1;  // Consume packet
}

// Process pending ICMP reply (called from main loop context)
void pppNatProcessPendingIcmp(PppNatContext* ctx, PppContext* ppp) {
    if (!g_pendingIcmpReply.pending) {
        return;
    }

    NAT_DEBUG_F("NAT: Processing ICMP reply from %d.%d.%d.%d id=%d seq=%d",
                g_pendingIcmpReply.srcIP[0], g_pendingIcmpReply.srcIP[1],
                g_pendingIcmpReply.srcIP[2], g_pendingIcmpReply.srcIP[3],
                g_pendingIcmpReply.id, g_pendingIcmpReply.seq);

    // Find matching NAT entry
    PppNatIcmpEntry* entry = nullptr;
    for (int i = 0; i < PPP_NAT_MAX_ICMP; i++) {
        if (ctx->icmpTable[i].active &&
            ctx->icmpTable[i].dstIP == g_pendingIcmpReply.srcIP &&
            ctx->icmpTable[i].id == g_pendingIcmpReply.id) {
            entry = &ctx->icmpTable[i];
            NAT_DEBUG_F("NAT: Matched ICMP entry %d", i);
            break;
        }
    }

    if (!entry) {
        NAT_DEBUG("NAT: No matching ICMP entry for reply");
        g_pendingIcmpReply.pending = false;
        return;
    }

    // Update entry with sequence from reply
    entry->sequence = g_pendingIcmpReply.seq;

    // Send reply to PPP client
    pppNatSendIcmpToClient(ctx, ppp, entry, PPP_ICMP_ECHO_REPLY, 0,
                            g_pendingIcmpReply.data, g_pendingIcmpReply.dataLen);

    NAT_DEBUG_F("NAT: Forwarded ping reply to client, %d bytes payload",
                g_pendingIcmpReply.dataLen);
    ctx->icmpPackets++;

    // Mark entry inactive after reply
    entry->active = false;

    g_pendingIcmpReply.pending = false;
}

// ============================================================================
// Process ICMP Packet (ping support)
// ============================================================================

static void pppNatProcessIcmp(PppNatContext* ctx, PppContext* ppp,
                               uint8_t* packet, uint16_t length,
                               PppIpHeader* ip, uint8_t ipHeaderLen) {
    if (length < ipHeaderLen + 8) {
        ctx->droppedPackets++;
        return;
    }

    PppIcmpHeader* icmp = (PppIcmpHeader*)(packet + ipHeaderLen);

    // Only handle echo request (ping)
    if (icmp->type != PPP_ICMP_ECHO_REQUEST) {
        NAT_DEBUG_F("NAT: ICMP type %d dropped (not echo request)", icmp->type);
        ctx->droppedPackets++;
        return;
    }

    // Construct destination IP from network byte order
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    IPAddress dstIP(dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3]);

    NAT_DEBUG_F("NAT: ICMP echo request to %d.%d.%d.%d, gateway=%d.%d.%d.%d",
                dstIP[0], dstIP[1], dstIP[2], dstIP[3],
                ctx->gatewayIP[0], ctx->gatewayIP[1], ctx->gatewayIP[2], ctx->gatewayIP[3]);

    // Check if ping is for our gateway IP
    if (dstIP == ctx->gatewayIP) {
        // Respond directly
        uint16_t icmpLen = length - ipHeaderLen;

        // Swap IPs
        uint32_t tmp = ip->srcIP;
        ip->srcIP = ip->dstIP;
        ip->dstIP = tmp;

        // Change to echo reply
        icmp->type = PPP_ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = htons(pppIpChecksum((uint8_t*)icmp, icmpLen));

        // Recalc IP checksum
        ip->ttl = 64;
        pppIpRecalcChecksum(ip);

        // Send back
        pppSendFrame(ppp, PPP_PROTO_IP, packet, length);
        ctx->packetsFromInternet++;
        ctx->icmpPackets++;

        NAT_DEBUG("NAT: Responded to ping");
        return;
    }

    // External ping - forward via ICMP NAT
    if (!ctx->icmpPcb) {
        NAT_DEBUG("NAT: No ICMP socket, dropping ping");
        ctx->droppedPackets++;
        return;
    }

    // Get source IP
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    IPAddress srcIP(srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3]);

    uint16_t icmpId = ntohs(icmp->id);
    uint16_t icmpSeq = ntohs(icmp->sequence);

    // Find or create ICMP NAT entry
    PppNatIcmpEntry* entry = nullptr;
    int freeSlot = -1;

    for (int i = 0; i < PPP_NAT_MAX_ICMP; i++) {
        if (ctx->icmpTable[i].active &&
            ctx->icmpTable[i].dstIP == dstIP &&
            ctx->icmpTable[i].id == icmpId) {
            entry = &ctx->icmpTable[i];
            break;
        }
        if (!ctx->icmpTable[i].active && freeSlot < 0) {
            freeSlot = i;
        }
    }

    if (!entry && freeSlot >= 0) {
        entry = &ctx->icmpTable[freeSlot];
        entry->active = true;
        entry->srcIP = srcIP;
        entry->dstIP = dstIP;
        entry->id = icmpId;
        NAT_DEBUG_F("NAT: Created ICMP entry %d for ping to %d.%d.%d.%d",
                    freeSlot, dstIP[0], dstIP[1], dstIP[2], dstIP[3]);
    }

    if (!entry) {
        NAT_DEBUG("NAT: ICMP table full");
        ctx->droppedPackets++;
        return;
    }

    entry->sequence = icmpSeq;
    entry->lastActivity = millis();

    // Store ppp context for callback
    g_icmpPppCtx = ppp;

    // Build ICMP packet to send via raw socket
    uint16_t icmpLen = length - ipHeaderLen;
    struct pbuf* p = pbuf_alloc(PBUF_IP, icmpLen, PBUF_RAM);
    if (!p) {
        NAT_DEBUG("NAT: Failed to allocate pbuf for ICMP");
        ctx->droppedPackets++;
        return;
    }

    // Copy ICMP data (header + payload)
    memcpy(p->payload, icmp, icmpLen);

    // Send via raw socket (ESP32 lwIP uses ip_addr_t with u_addr.ip4)
    ip_addr_t dest;
    IP_ADDR4(&dest, dstIP[0], dstIP[1], dstIP[2], dstIP[3]);

    err_t err = raw_sendto((struct raw_pcb*)ctx->icmpPcb, p, &dest);
    pbuf_free(p);

    if (err == ERR_OK) {
        NAT_DEBUG_F("NAT: Forwarded ping to %d.%d.%d.%d id=%d seq=%d",
                    dstIP[0], dstIP[1], dstIP[2], dstIP[3], icmpId, icmpSeq);
        ctx->packetsToInternet++;
        ctx->icmpPackets++;
    } else {
        NAT_DEBUG_F("NAT: Failed to send ICMP, err=%d", err);
        ctx->droppedPackets++;
    }
}

// ============================================================================
// Poll Connections for Incoming Data
// ============================================================================

int pppNatPollConnections(PppNatContext* ctx, PppContext* ppp) {
    int packetsSent = 0;

    // Poll TCP connections
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        PppNatTcpEntry* e = &ctx->tcpTable[i];
        if (!e->active || !e->client) {
            continue;
        }

        // Check for incoming data from remote server
        // Uses strict stop-and-wait flow control for reliability on vintage serial links
        // Only sends when ALL previous data has been ACKed (no retransmission buffer)
        // Hardware flow control (CTS) prevents buffer overflow

        int avail = e->client->available();
        if (avail > 0 && e->state == PPP_NAT_TCP_ESTABLISHED) {
            // Calculate bytes in flight (sent but not yet ACKed by client)
            uint32_t inFlight = e->serverSeq - e->serverAck;

            // STRICT: Only send if NO data is in flight (stop-and-wait)
            if (inFlight == 0) {
                // 1000 byte segments - balance between throughput and reliability
                // Larger segments (1400) can overflow receiver buffer at 9600 baud
                uint32_t canSend = 1000;
                if (canSend > (uint32_t)avail) canSend = avail;

                uint8_t buffer[1000];
                int len = e->client->read(buffer, canSend);
                if (len > 0) {
                    static uint32_t totalBytesSent = 0;
                    totalBytesSent += len;
                    NAT_DEBUG_F("NAT: TCP[%d] sending %d bytes, seq=%u, total=%uKB",
                                i, len, e->serverSeq, totalBytesSent / 1024);
                    pppNatSendTcpToClient(ctx, ppp, e, buffer, len,
                                          PPP_TCP_FLAG_PSH | PPP_TCP_FLAG_ACK);
                    e->lastActivity = millis();
                    e->lastServerData = millis();  // Track server data separately
                    packetsSent++;
                    // No longer limiting to one send per poll - each connection
                    // can send if it has data and no in-flight data
                }
            } else {
                // Data in flight - waiting for ACK from PPP client
                unsigned long now = millis();
                unsigned long waitTime = now - e->lastActivity;

                // Periodic debug every 5 seconds while waiting
                static unsigned long lastWaitDebug = 0;
                if (now - lastWaitDebug > 5000) {
                    int clientAvail = e->client ? e->client->available() : -1;
                    bool clientConn = e->client ? e->client->connected() : false;
                    int cts = digitalRead(CTS_PIN);
                    int serialAvail = PhysicalSerial.available();
                    NAT_DEBUG_F("NAT: TCP[%d] WAITING: inFlight=%u, wait=%lums, avail=%d, conn=%d, CTS=%d, serial=%d",
                                i, inFlight, waitTime, clientAvail, clientConn, cts, serialAvail);
                    lastWaitDebug = now;
                }

                // Stall detection - close if no ACKs for too long
                // Use generous timeout to handle slow serial links
                if (waitTime > PPP_NAT_TCP_STALL_TIMEOUT_MS) {
                    NAT_DEBUG_F("NAT: TCP[%d] stalled (no ACK for %lums, inFlight=%u), closing",
                                i, waitTime, inFlight);
                    if (e->client) {
                        e->client->stop();
                        delete e->client;
                        e->client = nullptr;
                    }
                    e->active = false;
                    continue;  // Skip rest of processing for this closed entry
                }
            }
        }

        // Check if connection closed by remote
        if (!e->client->connected() && e->state == PPP_NAT_TCP_ESTABLISHED) {
            pppNatSendTcpToClient(ctx, ppp, e, nullptr, 0,
                                  PPP_TCP_FLAG_FIN | PPP_TCP_FLAG_ACK);
            e->state = PPP_NAT_TCP_CLOSING;
        }

        // TCP keepalive - send periodic ACK to keep connection alive
        if (e->state == PPP_NAT_TCP_ESTABLISHED) {
            unsigned long now = millis();
            if (now - e->lastKeepalive > PPP_NAT_TCP_KEEPALIVE_MS) {
                NAT_DEBUG_F("NAT: TCP[%d] sending keepalive", i);
                pppNatSendTcpToClient(ctx, ppp, e, nullptr, 0, PPP_TCP_FLAG_ACK);
                e->lastKeepalive = now;
                e->lastActivity = now;  // Keepalive counts as activity
            }

            // Check for server silence - warn if no data from server for 5+ minutes
            // This helps detect silently dead connections (server disconnected but
            // WiFiClient.connected() still returns true)
            unsigned long serverSilence = now - e->lastServerData;
            if (serverSilence > 300000) {  // 5 minutes
                static unsigned long lastSilenceWarn = 0;
                if (now - lastSilenceWarn > 60000) {  // Warn every minute
                    NAT_DEBUG_F("NAT: TCP[%d] server silent for %lu seconds, conn=%d",
                                i, serverSilence / 1000, e->client->connected());
                    lastSilenceWarn = now;
                }
            }
        }
    }

    // Poll UDP socket
    if (ctx->udpSocketBound) {
        int packetSize = ctx->udpSocket.parsePacket();
        if (packetSize > 0) {
            uint8_t buffer[1472];
            int len = ctx->udpSocket.read(buffer, sizeof(buffer));
            if (len > 0) {
                IPAddress remoteIP = ctx->udpSocket.remoteIP();
                uint16_t remotePort = ctx->udpSocket.remotePort();

                NAT_DEBUG_F("NAT: UDP response from %d.%d.%d.%d:%d len=%d",
                            remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3],
                            remotePort, len);

                // Find matching UDP session
                bool found = false;
                for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
                    PppNatUdpEntry* e = &ctx->udpTable[i];
                    if (e->active && e->dstIP == remoteIP && e->dstPort == remotePort) {
                        NAT_DEBUG_F("NAT: Matched UDP entry %d, forwarding to client", i);
                        pppNatSendUdpToClient(ctx, ppp, e, buffer, len);
                        packetsSent++;
                        found = true;
                        // For DNS (port 53), mark entry inactive after response
                        // DNS is simple request-response, no need to keep the slot
                        if (e->dstPort == 53) {
                            e->active = false;
                            NAT_DEBUG_F("NAT: Released DNS UDP entry %d", i);
                        } else {
                            e->lastActivity = millis();
                        }
                        break;
                    }
                }
                if (!found) {
                    NAT_DEBUG_F("NAT: No matching UDP entry for response");
                }
            }
        }
    }

    // Process any pending ICMP replies
    pppNatProcessPendingIcmp(ctx, ppp);

    return packetsSent;
}

// ============================================================================
// Clean Up Expired Connections
// ============================================================================

void pppNatCleanupExpired(PppNatContext* ctx) {
    unsigned long now = millis();

    // Clean up TCP
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        PppNatTcpEntry* e = &ctx->tcpTable[i];
        if (!e->active) continue;

        unsigned long timeout = PPP_NAT_TCP_TIMEOUT_MS;
        if (e->state == PPP_NAT_TCP_SYN_SENT) {
            timeout = PPP_NAT_TCP_SYN_TIMEOUT_MS;
        } else if (e->state == PPP_NAT_TCP_CLOSING || e->state == PPP_NAT_TCP_FIN_WAIT) {
            timeout = 10000;  // 10 seconds for closing
        }

        if (now - e->lastActivity > timeout) {
            NAT_DEBUG_F("NAT: Closing expired TCP %d\n", i);
            if (e->client) {
                e->client->stop();
                delete e->client;
                e->client = nullptr;
            }
            e->active = false;
        }
    }

    // Clean up UDP
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        PppNatUdpEntry* e = &ctx->udpTable[i];
        if (!e->active) continue;

        if (now - e->lastActivity > PPP_NAT_UDP_TIMEOUT_MS) {
            NAT_DEBUG_F("NAT: Closing expired UDP %d\n", i);
            e->active = false;
        }
    }

    // Clean up ICMP
    for (int i = 0; i < PPP_NAT_MAX_ICMP; i++) {
        PppNatIcmpEntry* e = &ctx->icmpTable[i];
        if (!e->active) continue;

        if (now - e->lastActivity > PPP_NAT_ICMP_TIMEOUT_MS) {
            NAT_DEBUG_F("NAT: Closing expired ICMP %d\n", i);
            e->active = false;
        }
    }
}

// ============================================================================
// Send IP Packet to PPP Client
// ============================================================================

void pppNatSendToClient(PppNatContext* ctx, PppContext* ppp,
                        uint8_t* packet, uint16_t length) {
    pppSendFrame(ppp, PPP_PROTO_IP, packet, length);
    ctx->packetsFromInternet++;
}

// ============================================================================
// Get Active Connection Counts
// ============================================================================

int pppNatGetActiveTcpCount(PppNatContext* ctx) {
    int count = 0;
    for (int i = 0; i < PPP_NAT_MAX_TCP; i++) {
        if (ctx->tcpTable[i].active) count++;
    }
    return count;
}

int pppNatGetActiveUdpCount(PppNatContext* ctx) {
    int count = 0;
    for (int i = 0; i < PPP_NAT_MAX_UDP; i++) {
        if (ctx->udpTable[i].active) count++;
    }
    return count;
}

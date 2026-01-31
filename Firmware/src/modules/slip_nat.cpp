// ============================================================================
// SLIP NAT Engine Implementation
// ============================================================================
// Handles NAT translation between vintage computer (SLIP) and WiFi
// Uses TCP proxy approach - WiFiClient handles actual TCP connections
// ============================================================================

#include "slip_nat.h"
#include "slip.h"
#include "globals.h"
#include "serial_io.h"
#include <EEPROM.h>

// lwIP includes for raw ICMP socket
extern "C" {
#include "lwip/raw.h"
#include "lwip/ip_addr.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
}

// Global context pointer for ICMP callback (lwIP callbacks don't have user data)
static NatContext* g_icmpNatCtx = nullptr;

// ============================================================================
// Debug output (controlled by usbDebug setting)
// ============================================================================

extern bool usbDebug;

#define NAT_DEBUG(msg) if (usbDebug) { UsbDebugPrintLn(msg); }
#define NAT_DEBUG_F(fmt, ...) if (usbDebug) { UsbDebugPrint(""); Serial.printf(fmt "\r\n", ##__VA_ARGS__); }

// Forward declarations for internal functions
static void natProcessTcp(NatContext* ctx, uint8_t* packet, uint16_t length,
                          IpHeader* ip, uint8_t ipHeaderLen);
static void natProcessUdp(NatContext* ctx, uint8_t* packet, uint16_t length,
                          IpHeader* ip, uint8_t ipHeaderLen);
static void natProcessIcmp(NatContext* ctx, uint8_t* packet, uint16_t length,
                           IpHeader* ip, uint8_t ipHeaderLen);
static NatTcpEntry* natFindTcpEntry(NatContext* ctx, IPAddress srcIP,
                                     uint16_t srcPort, IPAddress dstIP,
                                     uint16_t dstPort);
static NatTcpEntry* natCreateTcpEntry(NatContext* ctx, IPAddress srcIP,
                                       uint16_t srcPort, IPAddress dstIP,
                                       uint16_t dstPort);
static NatUdpEntry* natFindOrCreateUdp(NatContext* ctx, IPAddress srcIP,
                                        uint16_t srcPort, IPAddress dstIP,
                                        uint16_t dstPort);
static void natSendTcpToClient(NatContext* ctx, NatTcpEntry* entry,
                                uint8_t* data, uint16_t length, uint8_t flags);
static void natSendUdpToClient(NatContext* ctx, NatUdpEntry* entry,
                                uint8_t* data, uint16_t length);
static void natSendIcmpToClient(NatContext* ctx, NatIcmpEntry* entry,
                                 uint8_t type, uint8_t code,
                                 uint8_t* data, uint16_t length);
static u8_t natIcmpRecvCallback(void* arg, struct raw_pcb* pcb,
                                 struct pbuf* p, const ip_addr_t* addr);

// External SLIP context (defined in slip_mode.cpp)
extern SlipContext slipCtx;

// ============================================================================
// Initialize NAT Context
// ============================================================================

void natInit(NatContext* ctx) {
    // Set default SLIP subnet (192.168.7.0/24)
    ctx->slipIP = IPAddress(192, 168, 7, 1);
    ctx->clientIP = IPAddress(192, 168, 7, 2);
    ctx->subnetMask = IPAddress(255, 255, 255, 0);
    ctx->dnsServer = IPAddress(8, 8, 8, 8);  // Google DNS

    // Initialize TCP table
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        ctx->tcpTable[i].active = false;
        ctx->tcpTable[i].client = nullptr;
        ctx->tcpTable[i].rxBuffer = nullptr;
        ctx->tcpTable[i].rxBufferLen = 0;
        ctx->tcpTable[i].rxBufferSize = 0;
    }

    // Initialize UDP table
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        ctx->udpTable[i].active = false;
    }

    // Initialize ICMP table
    for (int i = 0; i < NAT_MAX_ICMP_SESSIONS; i++) {
        ctx->icmpTable[i].active = false;
    }

    // Initialize port forwards
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        ctx->portForwards[i].active = false;
        ctx->tcpForwardServers[i] = nullptr;
    }

    ctx->udpSocketBound = false;
    ctx->nextPort = NAT_PORT_START;

    // Create ICMP raw socket for ping NAT
    ctx->icmpPcb = nullptr;
    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (pcb) {
        raw_recv(pcb, natIcmpRecvCallback, ctx);
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
// Shutdown NAT - Clean up all connections
// ============================================================================

void natShutdown(NatContext* ctx) {
    // Close all TCP connections
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        if (ctx->tcpTable[i].active) {
            if (ctx->tcpTable[i].client) {
                ctx->tcpTable[i].client->stop();
                delete ctx->tcpTable[i].client;
                ctx->tcpTable[i].client = nullptr;
            }
            if (ctx->tcpTable[i].rxBuffer) {
                free(ctx->tcpTable[i].rxBuffer);
                ctx->tcpTable[i].rxBuffer = nullptr;
            }
            ctx->tcpTable[i].active = false;
        }
    }

    // Clear UDP sessions
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        ctx->udpTable[i].active = false;
    }

    // Clear ICMP sessions
    for (int i = 0; i < NAT_MAX_ICMP_SESSIONS; i++) {
        ctx->icmpTable[i].active = false;
    }

    // Stop port forward servers
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (ctx->tcpForwardServers[i]) {
            ctx->tcpForwardServers[i]->stop();
            delete ctx->tcpForwardServers[i];
            ctx->tcpForwardServers[i] = nullptr;
        }
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
}

// ============================================================================
// IP Checksum Calculation
// ============================================================================

uint16_t ipChecksum(const uint8_t* data, uint16_t length) {
    uint32_t sum = 0;

    // Sum all 16-bit words
    for (uint16_t i = 0; i < length - 1; i += 2) {
        sum += (data[i] << 8) | data[i + 1];
    }

    // Handle odd byte
    if (length & 1) {
        sum += data[length - 1] << 8;
    }

    // Fold 32-bit sum to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// ============================================================================
// Recalculate IP Header Checksum
// ============================================================================

void ipRecalcChecksum(IpHeader* ip) {
    uint8_t headerLen = (ip->versionIhl & 0x0F) * 4;
    ip->checksum = 0;
    ip->checksum = htons(ipChecksum((uint8_t*)ip, headerLen));
}

// ============================================================================
// TCP/UDP Pseudo-header Checksum
// ============================================================================

uint16_t tcpUdpChecksum(IpHeader* ip, const uint8_t* payload, uint16_t payloadLen) {
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

    // Fold and complement
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// ============================================================================
// Process Incoming IP Packet from Vintage Computer
// ============================================================================

void natProcessPacket(NatContext* ctx, uint8_t* packet, uint16_t length) {
    // Minimum IP header is 20 bytes
    if (length < 20) {
        NAT_DEBUG_F("NAT: Packet too short (%d bytes)", length);
        ctx->droppedPackets++;
        return;
    }

    IpHeader* ip = (IpHeader*)packet;

    // Check IP version (must be 4)
    uint8_t version = (ip->versionIhl >> 4) & 0x0F;
    if (version != 4) {
        NAT_DEBUG_F("NAT: Invalid IP version %d", version);
        ctx->droppedPackets++;
        return;
    }

    // Get header length
    uint8_t headerLen = (ip->versionIhl & 0x0F) * 4;
    if (headerLen < 20 || headerLen > length) {
        NAT_DEBUG_F("NAT: Invalid header length %d", headerLen);
        ctx->droppedPackets++;
        return;
    }

    // Verify checksum
    uint16_t originalChecksum = ip->checksum;
    ip->checksum = 0;
    uint16_t calculatedChecksum = htons(ipChecksum(packet, headerLen));
    ip->checksum = originalChecksum;

    if (originalChecksum != calculatedChecksum) {
        // Bad checksum - drop packet
        NAT_DEBUG_F("NAT: IP checksum mismatch: got 0x%04X, calc 0x%04X",
                    ntohs(originalChecksum), ntohs(calculatedChecksum));
        ctx->droppedPackets++;
        return;
    }

    // Log incoming packet info
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    NAT_DEBUG_F("NAT: RX proto=%d %d.%d.%d.%d -> %d.%d.%d.%d len=%d",
                ip->protocol,
                srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3],
                dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3],
                length);

    // Get total length from header
    uint16_t totalLength = ntohs(ip->totalLength);
    if (totalLength > length) {
        ctx->droppedPackets++;
        return;
    }

    // Route based on protocol
    switch (ip->protocol) {
        case IP_PROTO_TCP:
            natProcessTcp(ctx, packet, totalLength, ip, headerLen);
            break;
        case IP_PROTO_UDP:
            natProcessUdp(ctx, packet, totalLength, ip, headerLen);
            break;
        case IP_PROTO_ICMP:
            natProcessIcmp(ctx, packet, totalLength, ip, headerLen);
            break;
        default:
            // Unknown protocol - drop
            ctx->droppedPackets++;
            break;
    }
}

// ============================================================================
// Process TCP Packet
// ============================================================================

static void natProcessTcp(NatContext* ctx, uint8_t* packet, uint16_t length,
                          IpHeader* ip, uint8_t ipHeaderLen) {
    // Minimum TCP header is 20 bytes
    if (length < ipHeaderLen + 20) {
        ctx->droppedPackets++;
        return;
    }

    TcpHeader* tcp = (TcpHeader*)(packet + ipHeaderLen);
    uint16_t srcPort = ntohs(tcp->srcPort);
    uint16_t dstPort = ntohs(tcp->dstPort);
    // Construct IPs from network byte order (bytes are already in correct order)
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    IPAddress dstIP(dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3]);
    IPAddress srcIP(srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3]);

    // Get TCP header length
    uint8_t tcpHeaderLen = ((tcp->dataOffset >> 4) & 0x0F) * 4;
    if (tcpHeaderLen < 20 || ipHeaderLen + tcpHeaderLen > length) {
        ctx->droppedPackets++;
        return;
    }

    // Get flags
    uint8_t flags = tcp->flags;

    // Calculate payload length
    uint16_t payloadLen = length - ipHeaderLen - tcpHeaderLen;
    uint8_t* payload = packet + ipHeaderLen + tcpHeaderLen;

    // Find existing connection or create new one on SYN
    NatTcpEntry* entry = natFindTcpEntry(ctx, srcIP, srcPort, dstIP, dstPort);

    // Debug: always log TCP packet details
    NAT_DEBUG_F("NAT: TCP %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d flags=0x%02X entry=%s",
                srcIP[0], srcIP[1], srcIP[2], srcIP[3], srcPort,
                dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort,
                flags, entry ? "found" : "null");

    if (flags & TCP_FLAG_SYN) {
        // Debug: log SYN packet details
        NAT_DEBUG_F("NAT: TCP SYN state=%d", entry ? entry->state : -1);

        // Check if this is a SYN-ACK response (for port forwarding)
        if ((flags & TCP_FLAG_ACK) && entry && entry->state == NAT_TCP_ESTABLISHED) {
            // This is a SYN-ACK from the vintage computer responding to our SYN
            // (port forwarding case). Complete the 3-way handshake.
            entry->clientSeq = ntohl(tcp->seqNum) + 1;
            entry->clientAck = ntohl(tcp->ackNum);

            // Update serverAck - vintage computer acknowledged our SYN
            // This is critical for flow control to work
            entry->serverAck = ntohl(tcp->ackNum);

            entry->lastActivity = millis();
            entry->lastKeepalive = millis();

            NAT_DEBUG_F("NAT: Port forward SYN-ACK received, sending ACK (clientSeq=%u, serverAck=%u)",
                        entry->clientSeq, entry->serverAck);

            // Send ACK to complete 3-way handshake with vintage computer
            natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_ACK);
            ctx->tcpConnections++;
            return;
        }

        // New connection request
        if (!entry) {
            // Check for stuck connection to same destination - helps with reconnection
            for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
                NatTcpEntry* e = &ctx->tcpTable[i];
                if (e->active && e->srcIP == srcIP && e->dstIP == dstIP && e->dstPort == dstPort) {
                    NAT_DEBUG_F("NAT: Closing stuck connection to same dest (port %d)", e->srcPort);
                    if (e->client) {
                        e->client->stop();
                        delete e->client;
                        e->client = nullptr;
                    }
                    e->active = false;
                }
            }

            entry = natCreateTcpEntry(ctx, srcIP, srcPort, dstIP, dstPort);
            if (!entry) {
                // Table full
                ctx->droppedPackets++;
                NAT_DEBUG("NAT: TCP table full");
                return;
            }
        }

        // Store initial sequence number (+1 because SYN consumes 1 seq number)
        entry->clientSeq = ntohl(tcp->seqNum) + 1;
        entry->clientAck = ntohl(tcp->ackNum);

        // Initiate connection to remote server
        if (entry->state == NAT_TCP_CLOSED) {
            entry->state = NAT_TCP_SYN_SENT;
            entry->created = millis();
            entry->lastActivity = millis();
            entry->lastKeepalive = millis();

            // Create WiFiClient and connect
            if (!entry->client) {
                entry->client = new WiFiClient();
            }

            NAT_DEBUG_F("NAT: TCP connecting to %d.%d.%d.%d:%d",
                        dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort);

            if (entry->client->connect(dstIP, dstPort)) {
                entry->state = NAT_TCP_ESTABLISHED;
                ctx->packetsToInternet++;
                ctx->tcpConnections++;
                NAT_DEBUG("NAT: TCP connected");

                // Initialize serverAck for flow control
                entry->serverAck = entry->serverSeq;

                // Send SYN-ACK back to client
                natSendTcpToClient(ctx, entry, nullptr, 0,
                                   TCP_FLAG_SYN | TCP_FLAG_ACK);
            } else {
                NAT_DEBUG("NAT: TCP connect failed");
                // Connection failed - send RST
                natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_RST);
                entry->active = false;
                delete entry->client;
                entry->client = nullptr;
            }
        }
        return;
    }

    if (!entry) {
        // No connection for this packet
        ctx->droppedPackets++;
        NAT_DEBUG_F("NAT: TCP no entry for %d.%d.%d.%d:%d",
                    dstIP[0], dstIP[1], dstIP[2], dstIP[3], dstPort);
        return;
    }

    entry->lastActivity = millis();

    // Handle FIN
    if (flags & TCP_FLAG_FIN) {
        entry->state = NAT_TCP_FIN_WAIT;
        if (entry->client) {
            entry->client->stop();
        }
        // Send FIN-ACK
        natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
        return;
    }

    // Handle RST
    if (flags & TCP_FLAG_RST) {
        if (entry->client) {
            entry->client->stop();
            delete entry->client;
            entry->client = nullptr;
        }
        entry->active = false;
        return;
    }

    // Handle data from client
    if (payloadLen > 0 && entry->state == NAT_TCP_ESTABLISHED) {
        if (entry->client && entry->client->connected()) {
            size_t written = entry->client->write(payload, payloadLen);
            if (written != payloadLen) {
                NAT_DEBUG_F("NAT: TCP client->server write failed: %d/%d bytes", written, payloadLen);
            } else {
                NAT_DEBUG_F("NAT: TCP client->server: %d bytes", payloadLen);
            }
            entry->clientSeq = ntohl(tcp->seqNum) + payloadLen;
            ctx->packetsToInternet++;
            entry->lastActivity = millis();

            // Send ACK back
            natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_ACK);
        } else {
            NAT_DEBUG("NAT: TCP client->server failed: not connected");
        }
    }

    // Update ACK tracking - client's ACK acknowledges data WE sent
    if (flags & TCP_FLAG_ACK) {
        entry->lastActivity = millis();
        uint32_t newAck = ntohl(tcp->ackNum);
        if (newAck != entry->serverAck) {
            NAT_DEBUG_F("NAT: TCP ACK update: serverAck %u -> %u", entry->serverAck, newAck);
            entry->serverAck = newAck;
        }
    }
}

// ============================================================================
// Process UDP Packet
// ============================================================================

static void natProcessUdp(NatContext* ctx, uint8_t* packet, uint16_t length,
                          IpHeader* ip, uint8_t ipHeaderLen) {
    // UDP header is 8 bytes
    if (length < ipHeaderLen + 8) {
        ctx->droppedPackets++;
        return;
    }

    UdpHeader* udp = (UdpHeader*)(packet + ipHeaderLen);
    uint16_t srcPort = ntohs(udp->srcPort);
    uint16_t dstPort = ntohs(udp->dstPort);
    uint16_t udpLen = ntohs(udp->length);
    // Construct IPs from network byte order (bytes are already in correct order)
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

    // Find or create UDP session
    NatUdpEntry* entry = natFindOrCreateUdp(ctx, srcIP, srcPort, dstIP, dstPort);
    if (!entry) {
        ctx->droppedPackets++;
        return;
    }

    entry->lastActivity = millis();

    // Bind UDP socket if not already
    if (!ctx->udpSocketBound) {
        ctx->udpSocket.begin(entry->natPort);
        ctx->udpSocketBound = true;
    }

    // Send UDP packet to remote
    ctx->udpSocket.beginPacket(dstIP, dstPort);
    ctx->udpSocket.write(payload, payloadLen);
    ctx->udpSocket.endPacket();

    ctx->packetsToInternet++;
}

// ============================================================================
// Pending ICMP Reply Buffer (for deferred processing from callback)
// ============================================================================

static struct {
    bool pending;
    IPAddress srcIP;
    uint16_t id;
    uint16_t seq;
    uint8_t data[64];   // Ping payload (typically 32 bytes)
    uint16_t dataLen;
} g_pendingIcmpReply = {false};

// ============================================================================
// ICMP Raw Socket Receive Callback
// ============================================================================

static u8_t natIcmpRecvCallback(void* arg, struct raw_pcb* pcb,
                                 struct pbuf* p, const ip_addr_t* addr) {
    (void)arg;  // Use global context instead

    // pbuf includes IP header - we need to skip it
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
        return 0;
    }

    uint16_t icmpId = ntohs(icmpHdr->id);
    uint16_t icmpSeq = ntohs(icmpHdr->seqno);

    // Get source IP from addr
    const ip4_addr_t* ip4 = &addr->u_addr.ip4;
    IPAddress srcIP((ip4->addr >> 0) & 0xFF, (ip4->addr >> 8) & 0xFF,
                    (ip4->addr >> 16) & 0xFF, (ip4->addr >> 24) & 0xFF);

    // Store reply info for deferred processing
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

// ============================================================================
// Process Pending ICMP Reply (called from main loop)
// ============================================================================

void natProcessPendingIcmp(NatContext* ctx) {
    if (!g_pendingIcmpReply.pending) {
        return;
    }

    NAT_DEBUG_F("NAT: Processing ICMP reply from %d.%d.%d.%d id=%d seq=%d",
                g_pendingIcmpReply.srcIP[0], g_pendingIcmpReply.srcIP[1],
                g_pendingIcmpReply.srcIP[2], g_pendingIcmpReply.srcIP[3],
                g_pendingIcmpReply.id, g_pendingIcmpReply.seq);

    // Find matching NAT entry
    NatIcmpEntry* entry = nullptr;
    for (int i = 0; i < NAT_MAX_ICMP_SESSIONS; i++) {
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

    // Send reply to SLIP client
    natSendIcmpToClient(ctx, entry, ICMP_ECHO_REPLY, 0,
                         g_pendingIcmpReply.data, g_pendingIcmpReply.dataLen);

    NAT_DEBUG_F("NAT: Forwarded ping reply to client, %d bytes payload",
                g_pendingIcmpReply.dataLen);
    ctx->icmpPackets++;

    // Mark entry inactive after reply
    entry->active = false;

    g_pendingIcmpReply.pending = false;
}

// ============================================================================
// Process ICMP Packet (Echo Request/Reply)
// ============================================================================

static void natProcessIcmp(NatContext* ctx, uint8_t* packet, uint16_t length,
                           IpHeader* ip, uint8_t ipHeaderLen) {
    // ICMP header is 8 bytes minimum
    if (length < ipHeaderLen + 8) {
        ctx->droppedPackets++;
        return;
    }

    IcmpHeader* icmp = (IcmpHeader*)(packet + ipHeaderLen);

    // Only handle Echo Request (ping)
    if (icmp->type != ICMP_ECHO_REQUEST) {
        NAT_DEBUG_F("NAT: ICMP type %d dropped", icmp->type);
        ctx->droppedPackets++;
        return;
    }

    // Construct IPs from network byte order (bytes are already in correct order)
    uint8_t* dstBytes = (uint8_t*)&ip->dstIP;
    uint8_t* srcBytes = (uint8_t*)&ip->srcIP;
    IPAddress dstIP(dstBytes[0], dstBytes[1], dstBytes[2], dstBytes[3]);
    IPAddress srcIP(srcBytes[0], srcBytes[1], srcBytes[2], srcBytes[3]);

    NAT_DEBUG_F("NAT: ICMP echo request to %d.%d.%d.%d",
                dstIP[0], dstIP[1], dstIP[2], dstIP[3]);

    // Check if ping is for our gateway IP
    if (dstIP == ctx->slipIP) {
        // Respond directly
        uint16_t icmpLen = length - ipHeaderLen;

        // Swap IPs
        uint32_t tmp = ip->srcIP;
        ip->srcIP = ip->dstIP;
        ip->dstIP = tmp;

        // Change to echo reply
        icmp->type = ICMP_ECHO_REPLY;
        icmp->checksum = 0;
        icmp->checksum = htons(ipChecksum((uint8_t*)icmp, icmpLen));

        // Recalc IP checksum
        ip->ttl = 64;
        ipRecalcChecksum(ip);

        // Send back
        natSendToClient(ctx, packet, length);
        ctx->packetsFromInternet++;
        ctx->icmpPackets++;

        NAT_DEBUG("NAT: Responded to gateway ping");
        return;
    }

    // External ping - forward via ICMP NAT
    if (!ctx->icmpPcb) {
        NAT_DEBUG("NAT: No ICMP socket, dropping ping");
        ctx->droppedPackets++;
        return;
    }

    uint16_t icmpId = ntohs(icmp->id);
    uint16_t icmpSeq = ntohs(icmp->sequence);

    // Find or create ICMP NAT entry
    NatIcmpEntry* entry = nullptr;
    int freeSlot = -1;

    for (int i = 0; i < NAT_MAX_ICMP_SESSIONS; i++) {
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

    // Send via raw socket
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
// Send ICMP Packet to Client (Vintage Computer)
// ============================================================================

static void natSendIcmpToClient(NatContext* ctx, NatIcmpEntry* entry,
                                 uint8_t type, uint8_t code,
                                 uint8_t* data, uint16_t length) {
    uint8_t packet[60 + 1472];  // Max IP + ICMP header + data
    uint16_t ipHeaderLen = 20;
    uint16_t icmpHeaderLen = 8;
    uint16_t totalLen = ipHeaderLen + icmpHeaderLen + length;

    if (totalLen > sizeof(packet)) {
        return;
    }

    // Build IP header
    IpHeader* ip = (IpHeader*)packet;
    ip->versionIhl = 0x45;
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->checksum = 0;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);

    // Build ICMP header
    IcmpHeader* icmp = (IcmpHeader*)(packet + ipHeaderLen);
    icmp->type = type;
    icmp->code = code;
    icmp->checksum = 0;
    icmp->id = htons(entry->id);
    icmp->sequence = htons(entry->sequence);

    // Copy data
    if (data && length > 0) {
        memcpy(packet + ipHeaderLen + icmpHeaderLen, data, length);
    }

    // Calculate ICMP checksum
    uint16_t icmpTotalLen = icmpHeaderLen + length;
    icmp->checksum = htons(ipChecksum((uint8_t*)icmp, icmpTotalLen));

    // Calculate IP checksum
    ipRecalcChecksum(ip);

    // Send via SLIP
    natSendToClient(ctx, packet, totalLen);
    ctx->packetsFromInternet++;
}

// ============================================================================
// Find TCP Entry
// ============================================================================

static NatTcpEntry* natFindTcpEntry(NatContext* ctx, IPAddress srcIP,
                                     uint16_t srcPort, IPAddress dstIP,
                                     uint16_t dstPort) {
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        if (ctx->tcpTable[i].active &&
            ctx->tcpTable[i].srcIP == srcIP &&
            ctx->tcpTable[i].srcPort == srcPort &&
            ctx->tcpTable[i].dstIP == dstIP &&
            ctx->tcpTable[i].dstPort == dstPort) {
            return &ctx->tcpTable[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Create TCP Entry
// ============================================================================

static NatTcpEntry* natCreateTcpEntry(NatContext* ctx, IPAddress srcIP,
                                       uint16_t srcPort, IPAddress dstIP,
                                       uint16_t dstPort) {
    // Find free slot
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        if (!ctx->tcpTable[i].active) {
            NatTcpEntry* entry = &ctx->tcpTable[i];
            entry->active = true;
            entry->state = NAT_TCP_CLOSED;
            entry->srcIP = srcIP;
            entry->srcPort = srcPort;
            entry->dstIP = dstIP;
            entry->dstPort = dstPort;
            entry->natPort = ctx->nextPort++;
            if (ctx->nextPort > NAT_PORT_END) {
                ctx->nextPort = NAT_PORT_START;
            }
            entry->client = nullptr;
            entry->clientSeq = 0;
            entry->clientAck = 0;
            entry->serverSeq = random(1, 0x7FFFFFFF);  // Random initial sequence
            entry->serverAck = 0;
            entry->rxBuffer = nullptr;
            entry->rxBufferLen = 0;
            entry->rxBufferSize = 0;
            entry->lastActivity = millis();
            entry->lastKeepalive = millis();
            entry->lastServerData = millis();
            entry->created = millis();
            return entry;
        }
    }
    return nullptr;
}

// ============================================================================
// Find or Create UDP Entry
// ============================================================================

static NatUdpEntry* natFindOrCreateUdp(NatContext* ctx, IPAddress srcIP,
                                        uint16_t srcPort, IPAddress dstIP,
                                        uint16_t dstPort) {
    // Find existing
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        if (ctx->udpTable[i].active &&
            ctx->udpTable[i].srcIP == srcIP &&
            ctx->udpTable[i].srcPort == srcPort &&
            ctx->udpTable[i].dstIP == dstIP &&
            ctx->udpTable[i].dstPort == dstPort) {
            return &ctx->udpTable[i];
        }
    }

    // Create new
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        if (!ctx->udpTable[i].active) {
            ctx->udpTable[i].active = true;
            ctx->udpTable[i].srcIP = srcIP;
            ctx->udpTable[i].srcPort = srcPort;
            ctx->udpTable[i].dstIP = dstIP;
            ctx->udpTable[i].dstPort = dstPort;
            ctx->udpTable[i].natPort = ctx->nextPort++;
            if (ctx->nextPort > NAT_PORT_END) {
                ctx->nextPort = NAT_PORT_START;
            }
            ctx->udpTable[i].lastActivity = millis();
            ctx->udpSessions++;
            return &ctx->udpTable[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Poll Connections for Incoming Data
// ============================================================================

int natPollConnections(NatContext* ctx) {
    int packetsSent = 0;

    // Poll TCP connections
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        NatTcpEntry* entry = &ctx->tcpTable[i];
        if (!entry->active || !entry->client) continue;

        // Check for incoming data from remote server
        // Uses strict stop-and-wait flow control for reliability on vintage serial links
        int avail = entry->client->available();
        if (avail > 0 && entry->state == NAT_TCP_ESTABLISHED) {
            // Calculate bytes in flight (sent but not yet ACKed by client)
            uint32_t inFlight = entry->serverSeq - entry->serverAck;

            // STRICT: Only send if NO data is in flight (stop-and-wait)
            if (inFlight == 0) {
                // 1000 byte segments - balance between throughput and reliability
                uint32_t canSend = 1000;
                if (canSend > (uint32_t)avail) canSend = avail;

                uint8_t buffer[1000];
                int len = entry->client->read(buffer, canSend);
                if (len > 0) {
                    NAT_DEBUG_F("NAT: TCP[%d] sending %d bytes, seq=%u", i, len, entry->serverSeq);
                    natSendTcpToClient(ctx, entry, buffer, len, TCP_FLAG_PSH | TCP_FLAG_ACK);
                    entry->lastActivity = millis();
                    entry->lastServerData = millis();
                    packetsSent++;
                }
            } else {
                // Data in flight - waiting for ACK from client
                unsigned long now = millis();
                unsigned long waitTime = now - entry->lastActivity;

                // Periodic debug every 5 seconds while waiting
                static unsigned long lastWaitDebug = 0;
                if (now - lastWaitDebug > 5000) {
                    int clientAvail = entry->client ? entry->client->available() : -1;
                    bool clientConn = entry->client ? entry->client->connected() : false;
                    NAT_DEBUG_F("NAT: TCP[%d] WAITING: inFlight=%u, wait=%lums, avail=%d, conn=%d",
                                i, inFlight, waitTime, clientAvail, clientConn);
                    lastWaitDebug = now;
                }

                // Stall detection - close if no ACKs for too long
                if (waitTime > NAT_TCP_STALL_TIMEOUT_MS) {
                    NAT_DEBUG_F("NAT: TCP[%d] stalled (no ACK for %lums), closing", i, waitTime);
                    if (entry->client) {
                        entry->client->stop();
                        delete entry->client;
                        entry->client = nullptr;
                    }
                    entry->active = false;
                    continue;
                }
            }
        }

        // Check if connection closed by remote
        if (!entry->client->connected() && entry->state == NAT_TCP_ESTABLISHED) {
            // Send FIN to client
            natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
            entry->state = NAT_TCP_CLOSING;
        }

        // TCP keepalive - send periodic ACK to keep connection alive
        if (entry->state == NAT_TCP_ESTABLISHED) {
            unsigned long now = millis();
            if (now - entry->lastKeepalive > NAT_TCP_KEEPALIVE_MS) {
                NAT_DEBUG_F("NAT: TCP[%d] sending keepalive", i);
                natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_ACK);
                entry->lastKeepalive = now;
                entry->lastActivity = now;
            }

            // Check for server silence - warn if no data from server for 5+ minutes
            unsigned long serverSilence = now - entry->lastServerData;
            if (serverSilence > 300000) {
                static unsigned long lastSilenceWarn = 0;
                if (now - lastSilenceWarn > 60000) {
                    NAT_DEBUG_F("NAT: TCP[%d] server silent for %lu seconds", i, serverSilence / 1000);
                    lastSilenceWarn = now;
                }
            }
        }
    }

    // Poll UDP socket for replies
    if (ctx->udpSocketBound) {
        int packetSize = ctx->udpSocket.parsePacket();
        if (packetSize > 0) {
            uint8_t buf[1472];
            int len = ctx->udpSocket.read(buf, sizeof(buf));
            if (len > 0) {
                IPAddress remoteIP = ctx->udpSocket.remoteIP();
                uint16_t remotePort = ctx->udpSocket.remotePort();

                NAT_DEBUG_F("NAT: UDP response from %d.%d.%d.%d:%d len=%d",
                            remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3],
                            remotePort, len);

                // Find matching UDP session
                bool found = false;
                for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
                    NatUdpEntry* entry = &ctx->udpTable[i];
                    if (entry->active &&
                        entry->dstIP == remoteIP &&
                        entry->dstPort == remotePort) {
                        NAT_DEBUG_F("NAT: Matched UDP entry %d", i);
                        natSendUdpToClient(ctx, entry, buf, len);
                        packetsSent++;
                        found = true;
                        // For DNS (port 53), mark entry inactive after response
                        if (entry->dstPort == 53) {
                            entry->active = false;
                            NAT_DEBUG_F("NAT: Released DNS UDP entry %d", i);
                        } else {
                            entry->lastActivity = millis();
                        }
                        break;
                    }
                }
                if (!found) {
                    NAT_DEBUG("NAT: No matching UDP entry for response");
                }
            }
        }
    }

    // Process any pending ICMP replies
    natProcessPendingIcmp(ctx);

    return packetsSent;
}

// ============================================================================
// Clean Up Expired Connections
// ============================================================================

void natCleanupExpired(NatContext* ctx) {
    unsigned long now = millis();

    // Clean TCP connections
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        NatTcpEntry* entry = &ctx->tcpTable[i];
        if (!entry->active) continue;

        unsigned long timeout = NAT_TCP_TIMEOUT_MS;
        if (entry->state == NAT_TCP_SYN_SENT) {
            timeout = NAT_TCP_SYN_TIMEOUT_MS;
        } else if (entry->state == NAT_TCP_CLOSING || entry->state == NAT_TCP_FIN_WAIT) {
            timeout = 10000;  // 10 seconds for closing
        }

        if (now - entry->lastActivity > timeout) {
            NAT_DEBUG_F("NAT: Closing expired TCP %d", i);
            if (entry->client) {
                entry->client->stop();
                delete entry->client;
                entry->client = nullptr;
            }
            if (entry->rxBuffer) {
                free(entry->rxBuffer);
                entry->rxBuffer = nullptr;
            }
            entry->active = false;
        }
    }

    // Clean UDP sessions
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        NatUdpEntry* entry = &ctx->udpTable[i];
        if (entry->active && now - entry->lastActivity > NAT_UDP_TIMEOUT_MS) {
            NAT_DEBUG_F("NAT: Closing expired UDP %d", i);
            entry->active = false;
        }
    }

    // Clean ICMP sessions
    for (int i = 0; i < NAT_MAX_ICMP_SESSIONS; i++) {
        NatIcmpEntry* entry = &ctx->icmpTable[i];
        if (entry->active && now - entry->lastActivity > NAT_ICMP_TIMEOUT_MS) {
            NAT_DEBUG_F("NAT: Closing expired ICMP %d", i);
            entry->active = false;
        }
    }
}

// ============================================================================
// Send TCP Packet to Client (Vintage Computer)
// ============================================================================

static void natSendTcpToClient(NatContext* ctx, NatTcpEntry* entry,
                                uint8_t* data, uint16_t length, uint8_t flags) {
    // Build IP + TCP packet
    uint16_t ipHeaderLen = 20;
    uint16_t tcpHeaderLen = 20;
    uint16_t totalLen = ipHeaderLen + tcpHeaderLen + length;
    uint8_t packet[60 + 1460];  // Max IP header + TCP header + data

    if (totalLen > sizeof(packet)) {
        return;
    }

    memset(packet, 0, totalLen);

    // Build IP header
    IpHeader* ip = (IpHeader*)packet;
    ip->versionIhl = 0x45;  // IPv4, 5 words header
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);  // Don't fragment
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);

    // Build TCP header
    TcpHeader* tcp = (TcpHeader*)(packet + ipHeaderLen);
    tcp->srcPort = htons(entry->dstPort);
    tcp->dstPort = htons(entry->srcPort);
    tcp->seqNum = htonl(entry->serverSeq);
    tcp->ackNum = htonl(entry->clientSeq);  // clientSeq is already "next expected byte"
    tcp->dataOffset = 0x50;  // 5 words header
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->urgentPtr = 0;

    // Copy data
    if (data && length > 0) {
        memcpy(packet + ipHeaderLen + tcpHeaderLen, data, length);
    }

    // Update sequence for next packet
    if (flags & TCP_FLAG_SYN) {
        entry->serverSeq++;
    }
    if (flags & TCP_FLAG_FIN) {
        entry->serverSeq++;
    }
    if (length > 0) {
        entry->serverSeq += length;
    }

    // Calculate checksums
    ipRecalcChecksum(ip);
    tcp->checksum = 0;
    tcp->checksum = htons(tcpUdpChecksum(ip, (uint8_t*)tcp, tcpHeaderLen + length));

    // Send via SLIP
    natSendToClient(ctx, packet, totalLen);
    ctx->packetsFromInternet++;
}

// ============================================================================
// Send UDP Packet to Client (Vintage Computer)
// ============================================================================

static void natSendUdpToClient(NatContext* ctx, NatUdpEntry* entry,
                                uint8_t* data, uint16_t length) {
    // Build IP + UDP packet
    uint16_t totalLen = 20 + 8 + length;  // IP header + UDP header + data
    uint8_t packet[totalLen];
    memset(packet, 0, totalLen);

    // Build IP header
    IpHeader* ip = (IpHeader*)packet;
    ip->versionIhl = 0x45;
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(random(1, 65535));
    ip->flagsFragment = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    // Construct IP addresses in network byte order from IPAddress bytes
    ip->srcIP = entry->dstIP[0] | ((uint32_t)entry->dstIP[1] << 8) |
                ((uint32_t)entry->dstIP[2] << 16) | ((uint32_t)entry->dstIP[3] << 24);
    ip->dstIP = entry->srcIP[0] | ((uint32_t)entry->srcIP[1] << 8) |
                ((uint32_t)entry->srcIP[2] << 16) | ((uint32_t)entry->srcIP[3] << 24);
    ipRecalcChecksum(ip);

    // Build UDP header
    UdpHeader* udp = (UdpHeader*)(packet + 20);
    udp->srcPort = htons(entry->dstPort);
    udp->dstPort = htons(entry->srcPort);
    udp->length = htons(8 + length);

    // Copy data
    if (data && length > 0) {
        memcpy(packet + 28, data, length);
    }

    // Calculate UDP checksum
    udp->checksum = 0;
    udp->checksum = htons(tcpUdpChecksum(ip, (uint8_t*)udp, 8 + length));

    // Send via SLIP
    natSendToClient(ctx, packet, totalLen);
}

// ============================================================================
// Send IP Packet to Client via SLIP
// ============================================================================

void natSendToClient(NatContext* ctx, uint8_t* packet, uint16_t length) {
    extern SlipContext slipCtx;
    slipSendFrame(&slipCtx, packet, length);
}

// ============================================================================
// Port Forwarding Management
// ============================================================================

int natAddPortForward(NatContext* ctx, uint8_t proto, uint16_t extPort,
                      IPAddress intIP, uint16_t intPort, bool startServer) {
    // Find free slot
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (!ctx->portForwards[i].active) {
            ctx->portForwards[i].active = true;
            ctx->portForwards[i].protocol = proto;
            ctx->portForwards[i].externalPort = extPort;
            ctx->portForwards[i].internalIP = intIP;
            ctx->portForwards[i].internalPort = intPort;

            // Start listener for TCP port forwards only if requested
            if (startServer && proto == IP_PROTO_TCP) {
                ctx->tcpForwardServers[i] = new WiFiServer(extPort);
                ctx->tcpForwardServers[i]->begin();
            }
            return i;
        }
    }
    return -1;  // Table full
}

void natStartPortForwardServers(NatContext* ctx) {
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (ctx->portForwards[i].active &&
            ctx->portForwards[i].protocol == IP_PROTO_TCP &&
            !ctx->tcpForwardServers[i]) {
            ctx->tcpForwardServers[i] = new WiFiServer(ctx->portForwards[i].externalPort);
            ctx->tcpForwardServers[i]->begin();
        }
    }
}

void natStopPortForwardServers(NatContext* ctx) {
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (ctx->tcpForwardServers[i]) {
            ctx->tcpForwardServers[i]->stop();
            delete ctx->tcpForwardServers[i];
            ctx->tcpForwardServers[i] = nullptr;
        }
    }
}

void natRemovePortForward(NatContext* ctx, int index) {
    if (index < 0 || index >= NAT_MAX_PORT_FORWARDS) return;
    if (!ctx->portForwards[index].active) return;

    if (ctx->tcpForwardServers[index]) {
        ctx->tcpForwardServers[index]->stop();
        delete ctx->tcpForwardServers[index];
        ctx->tcpForwardServers[index] = nullptr;
    }
    ctx->portForwards[index].active = false;
}

// ============================================================================
// Check Port Forwards for Incoming Connections
// ============================================================================

void natCheckPortForwards(NatContext* ctx) {
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (!ctx->portForwards[i].active) continue;
        if (ctx->portForwards[i].protocol != IP_PROTO_TCP) continue;
        if (!ctx->tcpForwardServers[i]) continue;

        WiFiClient newClient = ctx->tcpForwardServers[i]->available();
        if (newClient) {
            // Create NAT entry for this inbound connection
            NatTcpEntry* entry = natCreateTcpEntry(ctx,
                ctx->portForwards[i].internalIP,
                ctx->portForwards[i].internalPort,
                newClient.remoteIP(),
                newClient.remotePort());

            if (entry) {
                entry->client = new WiFiClient(newClient);
                entry->state = NAT_TCP_ESTABLISHED;

                NAT_DEBUG_F("NAT: Port forward entry created: src=%d.%d.%d.%d:%d dst=%d.%d.%d.%d:%d",
                            entry->srcIP[0], entry->srcIP[1], entry->srcIP[2], entry->srcIP[3], entry->srcPort,
                            entry->dstIP[0], entry->dstIP[1], entry->dstIP[2], entry->dstIP[3], entry->dstPort);

                // Send SYN to internal host
                natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_SYN);
            } else {
                // No room - reject connection
                newClient.stop();
            }
        }
    }
}

// ============================================================================
// Get Connection Counts
// ============================================================================

int natGetActiveTcpCount(NatContext* ctx) {
    int count = 0;
    for (int i = 0; i < NAT_MAX_TCP_CONNECTIONS; i++) {
        if (ctx->tcpTable[i].active) count++;
    }
    return count;
}

int natGetActiveUdpCount(NatContext* ctx) {
    int count = 0;
    for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
        if (ctx->udpTable[i].active) count++;
    }
    return count;
}

// ============================================================================
// Port Forward Persistence (EEPROM)
// ============================================================================

void savePortForwards(NatContext* ctx) {
    for (int i = 0; i < PORTFWD_COUNT; i++) {
        int addr = PORTFWD_BASE + (i * PORTFWD_SIZE);

        if (ctx->portForwards[i].active) {
            EEPROM.write(addr, 1);  // active flag
            EEPROM.write(addr + 1, ctx->portForwards[i].protocol);
            EEPROM.write(addr + 2, (ctx->portForwards[i].externalPort >> 8) & 0xFF);
            EEPROM.write(addr + 3, ctx->portForwards[i].externalPort & 0xFF);
            EEPROM.write(addr + 4, (ctx->portForwards[i].internalPort >> 8) & 0xFF);
            EEPROM.write(addr + 5, ctx->portForwards[i].internalPort & 0xFF);
            EEPROM.write(addr + 6, ctx->portForwards[i].internalIP[0]);
            EEPROM.write(addr + 7, ctx->portForwards[i].internalIP[1]);
            EEPROM.write(addr + 8, ctx->portForwards[i].internalIP[2]);
            EEPROM.write(addr + 9, ctx->portForwards[i].internalIP[3]);
        } else {
            EEPROM.write(addr, 0);  // inactive
        }
    }
    EEPROM.commit();
}

void loadPortForwards(NatContext* ctx) {
    for (int i = 0; i < PORTFWD_COUNT; i++) {
        int addr = PORTFWD_BASE + (i * PORTFWD_SIZE);

        uint8_t active = EEPROM.read(addr);
        if (active == 1) {
            ctx->portForwards[i].active = true;
            ctx->portForwards[i].protocol = EEPROM.read(addr + 1);
            ctx->portForwards[i].externalPort = (EEPROM.read(addr + 2) << 8) | EEPROM.read(addr + 3);
            ctx->portForwards[i].internalPort = (EEPROM.read(addr + 4) << 8) | EEPROM.read(addr + 5);
            ctx->portForwards[i].internalIP = IPAddress(
                EEPROM.read(addr + 6),
                EEPROM.read(addr + 7),
                EEPROM.read(addr + 8),
                EEPROM.read(addr + 9)
            );
        } else {
            ctx->portForwards[i].active = false;
        }
    }
}

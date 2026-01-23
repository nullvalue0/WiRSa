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

    // Initialize port forwards
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        ctx->portForwards[i].active = false;
        ctx->tcpForwardServers[i] = nullptr;
    }

    ctx->udpSocketBound = false;
    ctx->nextPort = NAT_PORT_START;

    // Statistics
    ctx->packetsToInternet = 0;
    ctx->packetsFromInternet = 0;
    ctx->droppedPackets = 0;
    ctx->tcpConnections = 0;
    ctx->udpSessions = 0;
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
        ctx->droppedPackets++;
        return;
    }

    IpHeader* ip = (IpHeader*)packet;

    // Check IP version (must be 4)
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
    uint16_t calculatedChecksum = htons(ipChecksum(packet, headerLen));
    ip->checksum = originalChecksum;

    if (originalChecksum != calculatedChecksum) {
        // Bad checksum - drop packet
        ctx->droppedPackets++;
        return;
    }

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
    IPAddress dstIP(ntohl(ip->dstIP));
    IPAddress srcIP(ntohl(ip->srcIP));

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

    if (flags & TCP_FLAG_SYN) {
        // New connection request
        if (!entry) {
            entry = natCreateTcpEntry(ctx, srcIP, srcPort, dstIP, dstPort);
            if (!entry) {
                // Table full
                ctx->droppedPackets++;
                return;
            }
        }

        // Store initial sequence number
        entry->clientSeq = ntohl(tcp->seqNum);
        entry->clientAck = ntohl(tcp->ackNum);

        // Initiate connection to remote server
        if (entry->state == NAT_TCP_CLOSED) {
            entry->state = NAT_TCP_SYN_SENT;
            entry->created = millis();
            entry->lastActivity = millis();

            // Create WiFiClient and connect
            if (!entry->client) {
                entry->client = new WiFiClient();
            }

            if (entry->client->connect(dstIP, dstPort)) {
                entry->state = NAT_TCP_ESTABLISHED;
                ctx->packetsToInternet++;
                ctx->tcpConnections++;

                // Send SYN-ACK back to client
                // We'll simulate this by immediately allowing data flow
                natSendTcpToClient(ctx, entry, nullptr, 0,
                                   TCP_FLAG_SYN | TCP_FLAG_ACK);
            } else {
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
        // No connection for this packet - send RST
        ctx->droppedPackets++;
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

    // Handle data
    if (payloadLen > 0 && entry->state == NAT_TCP_ESTABLISHED) {
        if (entry->client && entry->client->connected()) {
            entry->client->write(payload, payloadLen);
            entry->clientSeq = ntohl(tcp->seqNum) + payloadLen;
            ctx->packetsToInternet++;

            // Send ACK back
            natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_ACK);
        }
    }

    // Update ACK tracking
    if (flags & TCP_FLAG_ACK) {
        entry->clientAck = ntohl(tcp->ackNum);
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
    IPAddress dstIP(ntohl(ip->dstIP));
    IPAddress srcIP(ntohl(ip->srcIP));

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

    // We only handle Echo Request - respond locally
    if (icmp->type == ICMP_ECHO_REQUEST) {
        // Check if destination is our SLIP IP
        IPAddress dstIP(ntohl(ip->dstIP));
        if (dstIP == ctx->slipIP) {
            // Respond to ping to gateway
            uint16_t icmpLen = length - ipHeaderLen;

            // Build reply packet
            uint8_t reply[length];
            memcpy(reply, packet, length);

            IpHeader* replyIp = (IpHeader*)reply;
            IcmpHeader* replyIcmp = (IcmpHeader*)(reply + ipHeaderLen);

            // Swap source and destination IP
            replyIp->srcIP = ip->dstIP;
            replyIp->dstIP = ip->srcIP;

            // Change to Echo Reply
            replyIcmp->type = ICMP_ECHO_REPLY;
            replyIcmp->code = 0;

            // Recalculate ICMP checksum
            replyIcmp->checksum = 0;
            replyIcmp->checksum = htons(ipChecksum((uint8_t*)replyIcmp, icmpLen));

            // Recalculate IP checksum
            ipRecalcChecksum(replyIp);

            // Send back via SLIP
            natSendToClient(ctx, reply, length);
            ctx->packetsFromInternet++;
        }
        // For pings to external hosts, we would need raw socket support
        // which isn't available in Arduino WiFi library
    }
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
            entry->serverSeq = 1000;  // Initial server sequence
            entry->serverAck = 0;
            entry->rxBuffer = nullptr;
            entry->rxBufferLen = 0;
            entry->rxBufferSize = 0;
            entry->lastActivity = millis();
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

        // Check for incoming data
        while (entry->client->available()) {
            uint8_t buf[512];
            int len = entry->client->read(buf, sizeof(buf));
            if (len > 0) {
                natSendTcpToClient(ctx, entry, buf, len, TCP_FLAG_PSH | TCP_FLAG_ACK);
                entry->serverSeq += len;
                packetsSent++;
                ctx->packetsFromInternet++;
            }
        }

        // Check if connection closed by remote
        if (!entry->client->connected() && entry->state == NAT_TCP_ESTABLISHED) {
            // Send FIN to client
            natSendTcpToClient(ctx, entry, nullptr, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
            entry->state = NAT_TCP_FIN_WAIT;
        }
    }

    // Poll UDP socket for replies
    if (ctx->udpSocketBound) {
        int packetSize = ctx->udpSocket.parsePacket();
        if (packetSize > 0) {
            uint8_t buf[1500];
            int len = ctx->udpSocket.read(buf, sizeof(buf));
            if (len > 0) {
                IPAddress remoteIP = ctx->udpSocket.remoteIP();
                uint16_t remotePort = ctx->udpSocket.remotePort();

                // Find matching UDP session
                for (int i = 0; i < NAT_MAX_UDP_SESSIONS; i++) {
                    NatUdpEntry* entry = &ctx->udpTable[i];
                    if (entry->active &&
                        entry->dstIP == remoteIP &&
                        entry->dstPort == remotePort) {
                        natSendUdpToClient(ctx, entry, buf, len);
                        packetsSent++;
                        ctx->packetsFromInternet++;
                        break;
                    }
                }
            }
        }
    }

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
        }

        if (now - entry->lastActivity > timeout) {
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
    uint16_t totalLen = 20 + 20 + length;  // IP header + TCP header + data
    uint8_t packet[totalLen];
    memset(packet, 0, totalLen);

    // Build IP header
    IpHeader* ip = (IpHeader*)packet;
    ip->versionIhl = 0x45;  // IPv4, 5 words header
    ip->tos = 0;
    ip->totalLength = htons(totalLen);
    ip->id = htons(rand() & 0xFFFF);
    ip->flagsFragment = htons(0x4000);  // Don't fragment
    ip->ttl = 64;
    ip->protocol = IP_PROTO_TCP;
    ip->srcIP = htonl((uint32_t)entry->dstIP);
    ip->dstIP = htonl((uint32_t)entry->srcIP);
    ipRecalcChecksum(ip);

    // Build TCP header
    TcpHeader* tcp = (TcpHeader*)(packet + 20);
    tcp->srcPort = htons(entry->dstPort);
    tcp->dstPort = htons(entry->srcPort);
    tcp->seqNum = htonl(entry->serverSeq);
    tcp->ackNum = htonl(entry->clientSeq + 1);
    tcp->dataOffset = 0x50;  // 5 words header
    tcp->flags = flags;
    tcp->window = htons(8192);
    tcp->urgentPtr = 0;

    // Copy data
    if (data && length > 0) {
        memcpy(packet + 40, data, length);
    }

    // Calculate TCP checksum
    tcp->checksum = 0;
    tcp->checksum = htons(tcpUdpChecksum(ip, (uint8_t*)tcp, 20 + length));

    // Send via SLIP
    natSendToClient(ctx, packet, totalLen);
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
    ip->id = htons(rand() & 0xFFFF);
    ip->flagsFragment = htons(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->srcIP = htonl((uint32_t)entry->dstIP);
    ip->dstIP = htonl((uint32_t)entry->srcIP);
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
                      IPAddress intIP, uint16_t intPort) {
    // Find free slot
    for (int i = 0; i < NAT_MAX_PORT_FORWARDS; i++) {
        if (!ctx->portForwards[i].active) {
            ctx->portForwards[i].active = true;
            ctx->portForwards[i].protocol = proto;
            ctx->portForwards[i].externalPort = extPort;
            ctx->portForwards[i].internalIP = intIP;
            ctx->portForwards[i].internalPort = intPort;

            // Start listener for TCP port forwards
            if (proto == IP_PROTO_TCP) {
                ctx->tcpForwardServers[i] = new WiFiServer(extPort);
                ctx->tcpForwardServers[i]->begin();
            }
            return i;
        }
    }
    return -1;  // Table full
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

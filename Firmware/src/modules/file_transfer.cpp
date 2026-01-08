// File Transfer Module
// Handles XModem, YModem, ZModem, and Raw file transfers

#include "Arduino.h"
#include "file_transfer.h"
#include "globals.h"
#include "serial_io.h"
#include "display_menu.h"
#include "playback.h"
#include <SD.h>
#include <WiFi.h>
#include <LinkedList.h>
#include "CRC16.h"
#include "CRC.h"

// External global variables and arrays
extern String fileName;
extern String xferMsg;
extern String waitTime;
extern byte xBuf[];
extern byte zBuf[];
extern bool xModemMode;
extern bool yModemMode;
extern LinkedList<String> logList;
extern unsigned long fileSize;
extern int packetNo;
extern int packetSize;
extern byte packetNo_LSB, packetNo_MSB;
extern int packetNumber;
extern bool lastPacket;
extern int eotSent;
extern bool readyToReceive;
extern bool zeroPacket;
extern int blockSize;
extern int finalFileSent;
extern int fileIndex;
extern File xferFile;
extern Timer<> timer;
extern CRC16 crc;
extern bool EscapeTelnet;

// External functions from modem.h (not yet extracted)
extern String prompt(String msg, String dflt="");
extern String getLine();
extern void led_on();

// CRC tables are defined in main.cpp, declared extern in globals.h

// Check if user wants to cancel the transfer
bool checkCancel() {
  readBackSwitch();
  bool cncl = BTNBK;
  waitBackSwitch();
  return cncl;
}

// ============================================================================
// ZMODEM Implementation - Complete protocol-compliant implementation
// Based on official ZModem spec and modern implementations (SyncTerm, Tera Term)
// ============================================================================

// ============================================================================
// CRC Functions
// ============================================================================

// Update CRC-16 CCITT with a single byte
uint16_t zmCRC16Update(uint16_t crc, uint8_t b) {
  return (crc << 8) ^ pgm_read_word(&crc16_ccitt_table[(crc >> 8) ^ b]);
}

// Calculate CRC-16 CCITT for a buffer
uint16_t zmCRC16(uint8_t* data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc = zmCRC16Update(crc, data[i]);
  }
  return crc;
}

// Update CRC-32 with a single byte
uint32_t zmCRC32Update(uint32_t crc, uint8_t b) {
  return pgm_read_dword(&crc32tab[(crc ^ b) & 0xFF]) ^ (crc >> 8);
}

// Calculate CRC-32 for a buffer
uint32_t zmCRC32(uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc = zmCRC32Update(crc, data[i]);
  }
  return ~crc;
}

// Legacy function - kept for compatibility
uint32_t zmCalcCRC32(uint8_t *buf, int len) {
  return zmCRC32(buf, len);
}

// ============================================================================
// Debug Logging System
// ============================================================================

void zmOpenDebugLog() {
  if (zmCtx.debugLevel == ZM_DEBUG_OFF) return;

  // Close any existing log file
  if (zmCtx.debugFile) {
    zmCtx.debugFile->close();
    delete zmCtx.debugFile;
    zmCtx.debugFile = nullptr;
  }

  // Open debug log file
  File f = SD.open("/zmodem_debug.txt", FILE_WRITE);
  if (f) {
    zmCtx.debugFile = new File(f);
    zmCtx.debugFile->println("=== ZMODEM Debug Log ===");
    zmCtx.debugFile->print("Timestamp: ");
    zmCtx.debugFile->println(millis());
    zmCtx.debugFile->flush();
  }
}

void zmCloseDebugLog() {
  if (zmCtx.debugFile) {
    zmCtx.debugFile->println("=== End of Log ===");
    zmCtx.debugFile->flush();
    zmCtx.debugFile->close();
    delete zmCtx.debugFile;
    zmCtx.debugFile = nullptr;
  }
}

void zmDebugLog(const char* fmt, ...) {
  if (zmCtx.debugLevel == ZM_DEBUG_OFF || !zmCtx.debugFile) return;

  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  zmCtx.debugFile->print("[");
  zmCtx.debugFile->print(millis());
  zmCtx.debugFile->print("] ");
  zmCtx.debugFile->println(buf);
  zmCtx.debugFile->flush();
}

void zmDebugHex(const char* prefix, uint8_t* data, size_t len) {
  if (zmCtx.debugLevel != ZM_DEBUG_VERBOSE || !zmCtx.debugFile) return;

  zmCtx.debugFile->print("[");
  zmCtx.debugFile->print(millis());
  zmCtx.debugFile->print("] ");
  zmCtx.debugFile->print(prefix);
  zmCtx.debugFile->print(": ");

  for (size_t i = 0; i < len && i < 64; i++) {
    if (data[i] < 0x10) zmCtx.debugFile->print("0");
    zmCtx.debugFile->print(data[i], HEX);
    zmCtx.debugFile->print(" ");
  }
  if (len > 64) zmCtx.debugFile->print("...");
  zmCtx.debugFile->println();
  zmCtx.debugFile->flush();
}

// ============================================================================
// Low-Level I/O Functions
// ============================================================================

// Check if a byte needs ZDLE escaping
bool zmNeedsEscape(uint8_t c) {
  switch (c) {
    case ZDLE:        // Always escape ZDLE
    case DLE:         // 0x10
    case DLE | 0x80:  // 0x90
    case XON:         // 0x11
    case XON | 0x80:  // 0x91
    case XOFF:        // 0x13
    case XOFF | 0x80: // 0x93
      return true;
    default:
      // Escape all control chars if requested
      if (zmCtx.escapeCtrlChars && (c & 0x60) == 0) {
        return true;
      }
      return false;
  }
}

// Send a raw byte (no escaping)
void zmSendRaw(uint8_t b) {
  SerialWrite(b);
  zmCtx.lastSent = b;
}

// Send a ZDLE-escaped byte
void zmSendEsc(uint8_t b) {
  zmSendRaw(ZDLE);
  zmSendRaw(b ^ 0x40);
}

// Send a byte with automatic ZDLE escaping where needed
void zmTx(uint8_t b) {
  if (zmNeedsEscape(b)) {
    zmSendEsc(b);
  } else {
    zmSendRaw(b);
  }
  updateXferMessage();
}

// Legacy function - kept for compatibility
void zmSendZDLE(uint8_t c) {
  zmTx(c);
}

// ============================================================================
// Header Send Functions
// ============================================================================

// Send hex nibble (0-15) as ASCII hex digit
static void zmSendHexNibble(uint8_t n) {
  static const char hex[] = "0123456789abcdef";
  zmSendRaw(hex[n & 0x0F]);
}

// Send byte as two hex ASCII digits
static void zmSendHexByte(uint8_t b) {
  zmSendHexNibble(b >> 4);
  zmSendHexNibble(b & 0x0F);
}

// Send ZPAD ZPAD ZDLE prefix
static void zmSendPaddedZDLE() {
  zmSendRaw(ZPAD);
  zmSendRaw(ZPAD);
  zmSendRaw(ZDLE);
}

// Send HEX format header (ZHEX)
// Format: ZPAD ZPAD ZDLE ZHEX type[2] p0[2] p1[2] p2[2] p3[2] crc[4] CR LF [XON]
void zmSendHexHdr(uint8_t type, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {
  zmDebugLog("TX HEX HDR: type=0x%02X pos=%lu", type,
             p0 | ((uint32_t)p1 << 8) | ((uint32_t)p2 << 16) | ((uint32_t)p3 << 24));

  // Build header for CRC calculation
  uint8_t hdr[5] = {type, (uint8_t)p0, (uint8_t)p1, (uint8_t)p2, (uint8_t)p3};
  uint16_t crc = zmCRC16(hdr, 5);

  // Send header
  zmSendPaddedZDLE();
  zmSendRaw(ZHEX);

  // Send header bytes as hex
  for (int i = 0; i < 5; i++) {
    zmSendHexByte(hdr[i]);
  }

  // Send CRC as hex (big-endian)
  zmSendHexByte((crc >> 8) & 0xFF);
  zmSendHexByte(crc & 0xFF);

  // Send line ending
  zmSendRaw(CR);
  zmSendRaw(LF);

  // Send XON for most header types (not ZFIN, ZACK)
  if (type != ZFIN && type != ZACK) {
    zmSendRaw(XON);
  }

  SerialFlush();
}

// Send BINARY 16-bit CRC header (ZBIN)
// Format: ZPAD ZPAD ZDLE ZBIN type p0 p1 p2 p3 crc_hi crc_lo
void zmSendBin16Hdr(uint8_t type, uint32_t pos) {
  zmDebugLog("TX BIN16 HDR: type=0x%02X pos=%lu", type, pos);

  // Build header
  uint8_t hdr[5] = {
    type,
    (uint8_t)(pos & 0xFF),
    (uint8_t)((pos >> 8) & 0xFF),
    (uint8_t)((pos >> 16) & 0xFF),
    (uint8_t)((pos >> 24) & 0xFF)
  };

  uint16_t crc = zmCRC16(hdr, 5);

  // Send sync and format
  zmSendPaddedZDLE();
  zmSendRaw(ZBIN);

  // Send header bytes with ZDLE escaping
  for (int i = 0; i < 5; i++) {
    zmTx(hdr[i]);
  }

  // Send CRC with ZDLE escaping (big-endian)
  zmTx((crc >> 8) & 0xFF);
  zmTx(crc & 0xFF);

  SerialFlush();
}

// Send BINARY 32-bit CRC header (ZBIN32)
// Format: ZPAD ZPAD ZDLE ZBIN32 type p0 p1 p2 p3 crc0 crc1 crc2 crc3
void zmSendBin32Hdr(uint8_t type, uint32_t pos) {
  zmDebugLog("TX BIN32 HDR: type=0x%02X pos=%lu", type, pos);

  // Build header
  uint8_t hdr[5] = {
    type,
    (uint8_t)(pos & 0xFF),
    (uint8_t)((pos >> 8) & 0xFF),
    (uint8_t)((pos >> 16) & 0xFF),
    (uint8_t)((pos >> 24) & 0xFF)
  };

  // Calculate CRC-32
  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < 5; i++) {
    crc = zmCRC32Update(crc, hdr[i]);
  }
  crc = ~crc;

  // Send sync and format
  zmSendPaddedZDLE();
  zmSendRaw(ZBIN32);

  // Send header bytes with ZDLE escaping
  for (int i = 0; i < 5; i++) {
    zmTx(hdr[i]);
  }

  // Send CRC with ZDLE escaping (little-endian)
  zmTx(crc & 0xFF);
  zmTx((crc >> 8) & 0xFF);
  zmTx((crc >> 16) & 0xFF);
  zmTx((crc >> 24) & 0xFF);

  SerialFlush();
}

// Send binary header - auto-select CRC-16 or CRC-32 based on receiver capabilities
void zmSendBinHeader(uint8_t type, uint32_t pos) {
  if (zmCtx.useCRC32) {
    zmSendBin32Hdr(type, pos);
  } else {
    zmSendBin16Hdr(type, pos);
  }
}

// Legacy function - kept for compatibility
void zmSendBinHdr(uint8_t type, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3) {
  uint32_t pos = p0 | ((uint32_t)p1 << 8) | ((uint32_t)p2 << 16) | ((uint32_t)p3 << 24);
  zmSendBin16Hdr(type, pos);
}
// ============================================================================
// Header Receive Functions
// ============================================================================

// Receive a single raw byte with timeout
// Returns: byte value (0-255) on success, ZM_TIMEOUT on timeout, ZM_CANCELLED on cancel
int zmRecvByte(unsigned long tout) {
  unsigned long st = millis();
  while (millis() - st < tout) {
    if (SerialAvailable() > 0) {
      int c = SerialRead();
      zmCtx.lastActivity = millis();

      // Check for CAN sequence (5 consecutive CANs = abort)
      if (c == CAN) {
        zmCtx.canCount++;
        if (zmCtx.canCount >= 5) {
          zmDebugLog("RX: 5 CANs received - remote abort");
          return ZM_CANCELLED;
        }
      } else {
        zmCtx.canCount = 0;
      }

      return c;
    }
    if (checkCancel()) {
      zmDebugLog("RX: User cancelled");
      return ZM_CANCELLED;
    }
    yield();
  }
  return ZM_TIMEOUT;
}

// Debug counters
static int xonxoffDropped = 0;
static int zdleEscapeCount = 0;
static int rawBytesReceived = 0;

// Receive a byte with ZDLE unescaping
// Returns: byte value (0-255), frame end marker |0x100, or negative error code
// NOTE: Does NOT drop XON/XOFF bytes - in ZModem, if data contains 0x11/0x13,
// the sender will ZDLE-escape them. Raw XON/XOFF should be passed through.
int zmRecvZDLE(unsigned long tout) {
  int c = zmRecvByte(tout);
  if (c < 0) return c;
  rawBytesReceived++;

  if (c == ZDLE) {
    zdleEscapeCount++;
    int escaped = c;  // Save for debugging
    c = zmRecvByte(tout);
    if (c < 0) return c;

    // Check for frame end markers after ZDLE escape
    // In binary mode, frame-end markers MUST be ZDLE-escaped to avoid confusion with data
    if (c == ZCRCE || c == ZCRCG || c == ZCRCQ || c == ZCRCW) {
      zmDebugLog("zmRecvZDLE: ZDLE 0x%02X detected as frame-end marker", c);
      return c | 0x100;  // Mark as frame end
    }

    // Check for special sequences
    if (c == ZRUB0) return 0x7F;
    if (c == ZRUB1) return 0xFF;

    // Normal escaped character - ALWAYS XOR with 0x40 to get original byte
    // Sender sends (byte XOR 0x40), so receiver must XOR again to recover
    int orig = c;
    c ^= 0x40;
    // Log escape: raw byte after ZDLE, result after XOR
    if (zdleEscapeCount <= 10) {
      zmDebugLog("ZDLE escape #%d: raw=0x%02X -> result=0x%02X", zdleEscapeCount, orig, c);
    }
  }

  return c & 0xFF;
}

// Receive a ZDLE-escaped byte for CRC reading (doesn't recognize frame end markers)
// Returns: byte value (0-255) or negative error code
// NOTE: Does NOT drop XON/XOFF - CRC bytes are critical and must not be skipped
static int crcByteCount = 0;  // Debug counter for CRC bytes

int zmRecvZDLE_CRC(unsigned long tout) {
  int c = zmRecvByte(tout);
  if (c < 0) return c;

  crcByteCount++;

  // DO NOT drop XON/XOFF here - CRC bytes are critical
  // In ZModem, if the actual CRC byte is 0x11 or 0x13, it will be ZDLE-escaped,
  // so we will only see it as ZDLE followed by escaped form. We cannot skip any bytes.

  int rawByte = c;  // Debug: save raw byte

  if (c == ZDLE) {
    c = zmRecvByte(tout);
    if (c < 0) return c;

    // DO NOT drop escaped XON/XOFF - unescape them if present

    // Check for special sequences
    if (c == ZRUB0) return 0x7F;
    if (c == ZRUB1) return 0xFF;

    // Normal escaped character - ALWAYS XOR with 0x40 to get original byte
    int escaped = c;
    c ^= 0x40;
    zmDebugLog("zmRecvZDLE_CRC: ZDLE escaped 0x%02X -> 0x%02X", escaped, c);
  } else {
    zmDebugLog("zmRecvZDLE_CRC: Raw byte 0x%02X", rawByte);
  }

  return c & 0xFF;
}

// Receive a hex nibble (0-15) from ASCII
static int zmRecvHexNibble(unsigned long tout) {
  int c = zmRecvByte(tout);
  if (c < 0) return c;

  c &= 0x7F;  // Strip parity
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return ZM_ERROR;
}

// Receive a hex byte (two ASCII digits)
static int zmRecvHexByte(unsigned long tout) {
  int hi = zmRecvHexNibble(tout);
  if (hi < 0) return hi;
  int lo = zmRecvHexNibble(tout);
  if (lo < 0) return lo;
  return (hi << 4) | lo;
}

// Receive and parse HEX format header
// Returns: frame type on success, negative error code on failure
// NOTE: Assumes ZPAD ZDLE ZHEX preamble already consumed by caller (zmRecvHeader)
int zmRecvHexHdr(uint8_t *hdr, unsigned long tout) {
  // Read 5 header bytes as hex
  for (int i = 0; i < 5; i++) {
    int c = zmRecvHexByte(tout);
    if (c < 0) return c;
    hdr[i] = c;
  }

  // Read CRC as hex (big-endian)
  int crc_hi = zmRecvHexByte(tout);
  int crc_lo = zmRecvHexByte(tout);
  if (crc_hi < 0 || crc_lo < 0) return ZM_TIMEOUT;
  uint16_t rxd_crc = (crc_hi << 8) | crc_lo;

  // Read and discard CR LF
  zmRecvByte(100);  // CR or LF
  if (hdr[0] != ZFIN && hdr[0] != ZACK) {
    zmRecvByte(100);  // LF or XON
  }

  // Verify CRC
  uint16_t calc_crc = zmCRC16(hdr, 5);
  if (rxd_crc != calc_crc) {
    zmDebugLog("RX HEX HDR: CRC error (rx=0x%04X, calc=0x%04X)", rxd_crc, calc_crc);
    return ZM_CRC_ERROR;
  }

  // Store header position
  zmCtx.rxdHeaderPos = hdr[ZP0] | ((uint32_t)hdr[ZP1] << 8) |
                       ((uint32_t)hdr[ZP2] << 16) | ((uint32_t)hdr[ZP3] << 24);

  zmDebugLog("RX HEX HDR: type=0x%02X pos=%lu", hdr[0], zmCtx.rxdHeaderPos);
  return hdr[0];
}

// Receive and parse BINARY 16-bit CRC header
int zmRecvBin16Hdr(uint8_t *hdr, unsigned long tout) {
  // Read 5 header bytes with ZDLE unescaping
  for (int i = 0; i < 5; i++) {
    int c = zmRecvZDLE(tout);
    if (c < 0) return c;
    hdr[i] = c & 0xFF;
  }

  // Read CRC (big-endian)
  int crc_hi = zmRecvZDLE(tout);
  int crc_lo = zmRecvZDLE(tout);
  if (crc_hi < 0 || crc_lo < 0) return ZM_TIMEOUT;
  uint16_t rxd_crc = (crc_hi << 8) | crc_lo;

  // Verify CRC
  uint16_t calc_crc = zmCRC16(hdr, 5);
  if (rxd_crc != calc_crc) {
    zmDebugLog("RX BIN16 HDR: CRC error (rx=0x%04X, calc=0x%04X)", rxd_crc, calc_crc);
    return ZM_CRC_ERROR;
  }

  // Sender is using CRC-16, so we should too
  zmCtx.receive32BitData = false;
  zmCtx.useCRC32 = false;
  return hdr[0];
}

// Receive and parse BINARY 32-bit CRC header
int zmRecvBin32Hdr(uint8_t *hdr, unsigned long tout) {
  // Read 5 header bytes with ZDLE unescaping
  for (int i = 0; i < 5; i++) {
    int c = zmRecvZDLE(tout);
    if (c < 0) return c;
    hdr[i] = c & 0xFF;
  }

  // Read CRC (little-endian)
  uint32_t rxd_crc = 0;
  for (int i = 0; i < 4; i++) {
    int c = zmRecvZDLE(tout);
    if (c < 0) return ZM_TIMEOUT;
    rxd_crc |= ((uint32_t)(c & 0xFF) << (i * 8));
  }

  // Calculate and verify CRC
  uint32_t calc_crc = 0xFFFFFFFF;
  for (int i = 0; i < 5; i++) {
    calc_crc = zmCRC32Update(calc_crc, hdr[i]);
  }
  calc_crc = ~calc_crc;

  if (rxd_crc != calc_crc) {
    zmDebugLog("RX BIN32 HDR: CRC error (rx=0x%08lX, calc=0x%08lX)", rxd_crc, calc_crc);
    return ZM_CRC_ERROR;
  }

  // Sender is using CRC-32, so we should too
  zmCtx.receive32BitData = true;
  zmCtx.useCRC32 = true;
  return hdr[0];
}

// Receive any header format
// Returns: frame type on success, negative error code on failure
int zmRecvHeader(unsigned long tout) {
  int c;
  unsigned long st = millis();

  // Clear header
  memset(zmCtx.rxdHeader, 0, sizeof(zmCtx.rxdHeader));

  // Look for ZPAD [ZPAD] ZDLE pattern
  while (millis() - st < tout) {
    c = zmRecvByte(100);
    if (c == ZPAD) {
      c = zmRecvByte(tout);
      if (c == ZPAD) {
        c = zmRecvByte(tout);
      }
      if (c == ZDLE) break;
    }
    if (c < 0 && c != ZM_TIMEOUT) return c;
    yield();
  }

  if (c != ZDLE) {
    zmDebugLog("RX HDR: Timeout waiting for header");
    return ZM_TIMEOUT;
  }

  // Read format byte
  c = zmRecvByte(tout);
  if (c < 0) return c;

  int result;
  switch (c) {
    case ZBIN:
      result = zmRecvBin16Hdr(zmCtx.rxdHeader, tout);
      break;
    case ZBIN32:
      result = zmRecvBin32Hdr(zmCtx.rxdHeader, tout);
      break;
    case ZHEX:
      result = zmRecvHexHdr(zmCtx.rxdHeader, tout);
      return result;  // zmRecvHexHdr already sets rxdHeaderPos
    default:
      zmDebugLog("RX HDR: Unknown format 0x%02X", c);
      return ZM_ERROR;
  }

  if (result >= 0) {
    // Store header position
    zmCtx.rxdHeaderPos = zmCtx.rxdHeader[ZP0] |
                         ((uint32_t)zmCtx.rxdHeader[ZP1] << 8) |
                         ((uint32_t)zmCtx.rxdHeader[ZP2] << 16) |
                         ((uint32_t)zmCtx.rxdHeader[ZP3] << 24);
    zmDebugLog("RX HDR: type=0x%02X pos=%lu", result, zmCtx.rxdHeaderPos);
  }

  return result;
}

// Legacy function - get position from header bytes
uint32_t zmGetPos(uint8_t *h) {
  return h[ZP0] | ((uint32_t)h[ZP1] << 8) | ((uint32_t)h[ZP2] << 16) | ((uint32_t)h[ZP3] << 24);
}
// ============================================================================
// Data Subpacket Functions
// ============================================================================

// Send data subpacket with frame end type and CRC
void zmSendDataSubpacket(uint8_t *data, size_t len, uint8_t frameEnd) {
  // Send escaped data bytes
  for (size_t i = 0; i < len; i++) {
    zmTx(data[i]);
  }

  // Send frame end marker (ZDLE + frameEnd)
  zmSendRaw(ZDLE);
  zmSendRaw(frameEnd);

  // Calculate and send CRC (including frame end type)
  // CRC bytes are ZDLE-escaped
  if (zmCtx.useCRC32) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
      crc = zmCRC32Update(crc, data[i]);
    }
    crc = zmCRC32Update(crc, frameEnd);
    crc = ~crc;

    // Send CRC32 (little-endian, ZDLE-escaped)
    for (int i = 0; i < 4; i++) {
      zmTx((crc >> (i * 8)) & 0xFF);
    }
  } else {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
      crc = zmCRC16Update(crc, data[i]);
    }
    crc = zmCRC16Update(crc, frameEnd);

    // Send CRC16 (big-endian, ZDLE-escaped)
    zmTx((crc >> 8) & 0xFF);
    zmTx(crc & 0xFF);
  }

  SerialFlush();
  zmDebugLog("TX DATA: %d bytes, frameEnd=0x%02X", (int)len, frameEnd);
}

// Receive data subpacket, returns length received, sets frameEnd
// Returns negative on error
extern int xonxoffDropped;  // Debug counter from zmRecvZDLE
extern int zdleEscapeCount;  // Debug counter from zmRecvZDLE
extern int rawBytesReceived;  // Debug counter from zmRecvZDLE
extern int crcByteCount;  // Debug counter from zmRecvZDLE_CRC

int zmRecvDataSubpacket(uint8_t *buf, size_t maxLen, uint8_t *frameEnd) {
  size_t idx = 0;
  int c;

  // Reset debug counters for this subpacket
  xonxoffDropped = 0;
  zdleEscapeCount = 0;
  rawBytesReceived = 0;
  crcByteCount = 0;

  while (idx < maxLen) {
    c = zmRecvZDLE(ZMODEM_TIMEOUT);
    if (c < 0) {
      zmDebugLog("RX DATA: Timeout after %d bytes", (int)idx);
      return ZM_TIMEOUT;
    }

    // Check for frame end marker
    if (c & 0x100) {
      *frameEnd = c & 0xFF;
      // Log the last few data bytes before frame end for debugging
      if (idx >= 3) {
        zmDebugLog("RX DATA: Last 3 bytes before frameEnd 0x%02X: 0x%02X 0x%02X 0x%02X",
                   *frameEnd, buf[idx-3], buf[idx-2], buf[idx-1]);
      }
      break;
    }

    buf[idx++] = c;
  }

  // Log if we exited due to buffer full
  if (idx >= maxLen) {
    zmDebugLog("RX DATA: Buffer full (%d bytes), no frameEnd detected!", (int)idx);
  }

  zmDebugLog("RX DATA: frameEnd=0x%02X, accumulated %d bytes from zmRecvZDLE calls", *frameEnd, idx);

  // Log XON/XOFF dropped count
  if (xonxoffDropped > 0) {
    zmDebugLog("RX DATA: WARNING! Dropped %d XON/XOFF bytes - this causes CRC mismatch!", xonxoffDropped);
  }

  // Calculate XOR checksum for debugging (simple data integrity check)
  uint8_t xorSum = 0;
  for (size_t i = 0; i < idx; i++) {
    xorSum ^= buf[i];
  }

  // Calculate and log what CRC we expect
  // CRC includes frame end marker
  uint16_t debugCrc = 0;
  for (size_t i = 0; i < idx; i++) {
    debugCrc = zmCRC16Update(debugCrc, buf[i]);
  }
  debugCrc = zmCRC16Update(debugCrc, *frameEnd);
  zmDebugLog("RX DATA: %d bytes, frameEnd=0x%02X, expected CRC=0x%04X", (int)idx, *frameEnd, debugCrc);
  zmDebugLog("RX DATA stats: raw=%d, ZDLE=%d, XON/XOFF=%d, XOR=0x%02X",
             rawBytesReceived, zdleEscapeCount, xonxoffDropped, xorSum);

  // ZModem protocol: ALL frame-end markers have CRC bytes
  // - ZCRCG (0x69) = frame continues nonstop (HAS CRC, skip verification)
  // - ZCRCQ (0x6A) = frame continues, ZACK expected (HAS CRC, skip verification)
  // - ZCRCW (0x6B) = end frame, ZACK expected (HAS CRC, VERIFY it)
  // - ZCRCE (0x68) = end frame, header follows (HAS CRC, skip verification)

  bool isContinuation = (*frameEnd == ZCRCG || *frameEnd == ZCRCQ);
  bool isZCRCE = (*frameEnd == ZCRCE);
  bool verifyCRC = (*frameEnd == ZCRCW);  // Only ZCRCW gets verified

  zmDebugLog("RX DATA: Frame marker 0x%02X %s", *frameEnd,
             verifyCRC ? "verify CRC" : "skip CRC");

  // Only verify CRC for ZCRCW frames
  if (verifyCRC && zmCtx.receive32BitData) {
    uint32_t rxCrc = 0;
    for (int i = 0; i < 4; i++) {
      c = zmRecvByte(ZMODEM_TIMEOUT);
      if (c < 0) return ZM_TIMEOUT;
      rxCrc |= ((uint32_t)(c & 0xFF)) << (i * 8);
    }

    uint32_t calcCrc = 0xFFFFFFFF;
    for (size_t i = 0; i < idx; i++) {
      calcCrc = zmCRC32Update(calcCrc, buf[i]);
    }
    calcCrc = zmCRC32Update(calcCrc, *frameEnd);
    calcCrc = ~calcCrc;

    if (rxCrc != calcCrc) {
      zmDebugLog("RX DATA: CRC32 error (rx=0x%08lX, calc=0x%08lX)", rxCrc, calcCrc);
      return ZM_CRC_ERROR;
    }
    zmDebugLog("RX DATA: CRC32 OK, frame ends (frameEnd=0x%02X)", *frameEnd);
  } else if (verifyCRC) {
    // CRC-16: read with ZDLE escaping (big-endian) - only for frame-ending subpackets
    int hi = zmRecvZDLE_CRC(ZMODEM_TIMEOUT);
    if (hi < 0) return ZM_TIMEOUT;

    int lo = zmRecvZDLE_CRC(ZMODEM_TIMEOUT);
    if (lo < 0) return ZM_TIMEOUT;

    uint16_t rxCrc = ((hi & 0xFF) << 8) | (lo & 0xFF);
    uint16_t calcCrc = 0;
    for (size_t i = 0; i < idx; i++) {
      calcCrc = zmCRC16Update(calcCrc, buf[i]);
    }
    calcCrc = zmCRC16Update(calcCrc, *frameEnd);

    zmDebugLog("RX DATA: CRC16 bytes: hi=0x%02X, lo=0x%02X, rxCrc=0x%04X, calcCrc=0x%04X",
               hi & 0xFF, lo & 0xFF, rxCrc, calcCrc);

    if (rxCrc != calcCrc) {
      zmDebugLog("RX DATA: CRC16 error (rx=0x%04X, calc=0x%04X)", rxCrc, calcCrc);
      return ZM_CRC_ERROR;
    }

    zmDebugLog("RX DATA: CRC16 OK, frame ends (frameEnd=0x%02X)", *frameEnd);
  } else {
    // Continuation subpackets (ZCRCG/ZCRCQ) and ZCRCE: read but don't verify CRC
    // This keeps the stream aligned for the next subpacket or header
    int hi = zmRecvZDLE_CRC(ZMODEM_TIMEOUT);
    if (hi < 0) return ZM_TIMEOUT;

    int lo = zmRecvZDLE_CRC(ZMODEM_TIMEOUT);
    if (lo < 0) return ZM_TIMEOUT;

    if (isContinuation) {
      zmDebugLog("RX DATA: Continuation - skipping CRC 0x%02X%02X (frameEnd=0x%02X)",
                 hi & 0xFF, lo & 0xFF, *frameEnd);
    } else {
      zmDebugLog("RX DATA: ZCRCE - skipping CRC 0x%02X%02X, header follows", hi & 0xFF, lo & 0xFF);
    }
  }

  return idx;
}

// ============================================================================
// Protocol Helper Functions
// ============================================================================

// Initialize ZModem context for new transfer
void zmInit(bool debugEnabled) {
  // Close any existing debug file first
  if (zmCtx.debugFile) {
    zmCtx.debugFile->close();
    delete zmCtx.debugFile;
    zmCtx.debugFile = nullptr;
  }

  memset(&zmCtx, 0, sizeof(zmCtx));
  zmCtx.state = ZM_IDLE;
  zmCtx.maxErrors = ZMODEM_MAX_ERRORS;
  zmCtx.timeout = ZMODEM_TIMEOUT;
  zmCtx.debugLevel = debugEnabled ? ZM_DEBUG_MINIMAL : ZM_DEBUG_OFF;
  zmCtx.useCRC32 = false;  // Start with CRC16, upgrade if receiver supports
  zmCtx.receive32BitData = false;
  zmCtx.lastSent = 0;
  zmCtx.canCount = 0;
  zmCtx.debugFile = nullptr;  // Initialize pointer to null
}

// Send ZRINIT with our receiver capabilities
void zmSendZRINIT() {
  // Build capability flags - use minimal capabilities to prevent buffer overflow
  // At 115200 baud with ESP32 processing delays, even conservative settings can overflow
  // Set capabilities to 0 = half-duplex, wait for ACKs, most conservative mode
  uint8_t zf0 = 0x00;  // No fancy capabilities - simplest, most reliable mode

  // Don't enable control char escaping - keep protocol simple
  zmCtx.escapeCtrlChars = false;

  // Send as hex header for compatibility
  zmSendHexHdr(ZRINIT, 0, 0, 0, zf0);
  zmDebugLog("TX ZRINIT: capabilities=0x%02X", zf0);
}

// Send ZRQINIT to request receiver init
void zmSendZRQINIT() {
  zmSendHexHdr(ZRQINIT, 0, 0, 0, 0);
  zmDebugLog("TX ZRQINIT");
}

// Send ZRPOS to request data from position
void zmSendZRPOS(uint32_t pos) {
  if (zmCtx.useCRC32) {
    zmSendBin32Hdr(ZRPOS, pos);
  } else {
    zmSendBin16Hdr(ZRPOS, pos);
  }
  zmDebugLog("TX ZRPOS: pos=%lu", pos);
}

// Send ZACK acknowledgment at position
void zmSendZACK(uint32_t pos) {
  if (zmCtx.useCRC32) {
    zmSendBin32Hdr(ZACK, pos);
  } else {
    zmSendBin16Hdr(ZACK, pos);
  }
  zmDebugLog("TX ZACK: pos=%lu", pos);
}

// Send ZNAK negative acknowledgment
void zmSendZNAK() {
  // Send ZNAK in binary format (matches the format of incoming ZDATA headers)
  zmSendBin16Hdr(ZNAK, 0);
  zmDebugLog("TX ZNAK");
}

// Send ZSKIP to skip this file
void zmSendZSKIP() {
  zmSendHexHdr(ZSKIP, 0, 0, 0, 0);
  zmDebugLog("TX ZSKIP");
}

// Send ZFIN to finish session
void zmSendZFIN() {
  zmSendHexHdr(ZFIN, 0, 0, 0, 0);
  zmDebugLog("TX ZFIN");
}

// Send ZABORT to abort transfer
void zmSendZABORT() {
  zmSendHexHdr(ZABORT, 0, 0, 0, 0);
  zmDebugLog("TX ZABORT");
}

// Send ZEOF at end of file
void zmSendZEOF(uint32_t pos) {
  // Send in little-endian byte order (p0=low byte, p3=high byte)
  zmSendHexHdr(ZEOF,
    pos & 0xFF, (pos >> 8) & 0xFF,
    (pos >> 16) & 0xFF, (pos >> 24) & 0xFF);
  zmDebugLog("TX ZEOF: pos=%lu", pos);
}

// Send ZFILE header with file info subpacket
void zmSendZFILE(const char *filename, uint32_t fileSize, uint32_t fileTime) {
  // Send ZFILE header
  zmSendBinHeader(ZFILE, 0);

  // Build file info subpacket: "filename\0size mtime mode\0"
  char infoBuf[256];
  int infoLen;

  if (fileTime > 0) {
    infoLen = snprintf(infoBuf, sizeof(infoBuf), "%s%c%lu %lo 100644",
                       filename, 0, fileSize, fileTime);
  } else {
    infoLen = snprintf(infoBuf, sizeof(infoBuf), "%s%c%lu",
                       filename, 0, fileSize);
  }

  // Ensure null termination
  infoBuf[infoLen++] = 0;

  // Send as data subpacket with ZCRCW (end frame, expect ACK)
  zmSendDataSubpacket((uint8_t*)infoBuf, infoLen, ZCRCW);

  zmDebugLog("TX ZFILE: name=%s size=%lu", filename, fileSize);
}

// Send ZDATA header at position
void zmSendZDATA(uint32_t pos) {
  zmSendBinHeader(ZDATA, pos);
  zmDebugLog("TX ZDATA: pos=%lu", pos);
}

// Send ZCRC response with file CRC-32
void zmSendZCRC(uint32_t crc) {
  // Send CRC in position field (little-endian)
  zmSendHexHdr(ZCRC,
    crc & 0xFF,
    (crc >> 8) & 0xFF,
    (crc >> 16) & 0xFF,
    (crc >> 24) & 0xFF);
  zmDebugLog("TX ZCRC: crc=0x%08lX", crc);
}

// Calculate CRC-32 of entire file
uint32_t zmCalculateFileCRC(File &f) {
  uint32_t crc = 0xFFFFFFFF;
  uint32_t savedPos = f.position();
  f.seek(0);

  // Read file in chunks and calculate CRC
  uint32_t remaining = f.size();
  while (remaining > 0) {
    size_t toRead = min((size_t)remaining, sizeof(zBuf));
    int bytesRead = f.read(zBuf, toRead);
    if (bytesRead <= 0) break;

    for (int i = 0; i < bytesRead; i++) {
      crc = zmCRC32Update(crc, zBuf[i]);
    }
    remaining -= bytesRead;
  }

  f.seek(savedPos);  // Restore position
  return ~crc;
}

// Parse ZRINIT flags from receiver
void zmParseZRINIT() {
  uint8_t zf0 = zmCtx.rxdHeader[ZF0];

  zmCtx.canFullDuplex = (zf0 & ZF0_CANFDX) != 0;
  zmCtx.canOverlapIO = (zf0 & ZF0_CANOVIO) != 0;
  zmCtx.escapeCtrlChars = (zf0 & ZF0_ESCCTL) != 0;

  // Check if receiver supports CRC32
  if (zf0 & ZF0_CANFC32) {
    zmCtx.useCRC32 = true;
    zmDebugLog("Receiver supports CRC32");
  }

  // Get receiver buffer size (if specified)
  zmCtx.recvBufSize = zmCtx.rxdHeader[ZP0] | (zmCtx.rxdHeader[ZP1] << 8);

  zmDebugLog("ZRINIT: fdx=%d ovio=%d esc=%d crc32=%d bufsize=%d",
    zmCtx.canFullDuplex, zmCtx.canOverlapIO, zmCtx.escapeCtrlChars,
    zmCtx.useCRC32, zmCtx.recvBufSize);
}

// Parse ZFILE subpacket to extract file info
// Returns true if valid file info found
bool zmParseZFILE(uint8_t *data, int len, char *filename, uint32_t *fileSize, uint32_t *fileTime) {
  if (len < 2) return false;

  // Find filename (up to first null)
  int i = 0;
  int fnLen = 0;
  while (i < len && data[i] != 0 && fnLen < 63) {
    filename[fnLen++] = data[i++];
  }
  filename[fnLen] = 0;

  if (fnLen == 0) return false;

  i++;  // Skip null terminator

  // Parse size (decimal)
  *fileSize = 0;
  while (i < len && data[i] >= '0' && data[i] <= '9') {
    *fileSize = (*fileSize * 10) + (data[i++] - '0');
  }

  // Skip space
  if (i < len && data[i] == ' ') i++;

  // Parse mtime (octal)
  *fileTime = 0;
  while (i < len && data[i] >= '0' && data[i] <= '7') {
    *fileTime = (*fileTime * 8) + (data[i++] - '0');
  }

  zmDebugLog("ZFILE parse: name=%s size=%lu mtime=%lu", filename, *fileSize, *fileTime);
  return true;
}

// Send cancel sequence (8 CAN chars followed by backspaces)
void zmSendCancel() {
  for (int i = 0; i < 8; i++) {
    zmSendRaw(CAN);
  }
  for (int i = 0; i < 10; i++) {
    zmSendRaw(0x08);  // Backspace
  }
  SerialFlush();
  zmDebugLog("TX CANCEL");
}

// Check for cancel from remote (5 consecutive CAN chars)
bool zmCheckCancel() {
  if (zmCtx.canCount >= 5) {
    zmDebugLog("Cancel detected");
    return true;
  }
  return false;
}

// ============================================================================
// Main Send Function
// ============================================================================

void sendFileZMODEM() {
  resetGlobals();

  // Initialize ZModem context with debug logging
  zmInit(true);
  zmOpenDebugLog();

  zmDebugLog("=== ZMODEM SEND START ===");

  // Get filename from user
  fileName = prompt("Filename to send: ");
  if (fileName.length() == 0) {
    zmDebugLog("No filename provided");
    zmCloseDebugLog();
    return;
  }

  // Open file
  String filePath = "/" + fileName;
  File f = SD.open(filePath);
  if (!f) {
    SerialPrintLn("File not found: " + fileName);
    zmDebugLog("File not found: %s", fileName.c_str());
    zmCloseDebugLog();
    return;
  }

  // Get file info
  zmCtx.fileSize = f.size();
  strncpy(zmCtx.fileName, fileName.c_str(), sizeof(zmCtx.fileName) - 1);
  zmCtx.filePos = 0;

  xferMsg = "ZMODEM SEND\n" + fileName + "\n";
  showMessage(xferMsg + "Waiting...");

  zmDebugLog("Sending file: %s (%lu bytes)", zmCtx.fileName, zmCtx.fileSize);
  SerialPrintLn("Sending: " + fileName + " (" + String(zmCtx.fileSize) + " bytes)");
  SerialPrintLn("Waiting for receiver...");

  // State machine for sending
  zmCtx.state = ZM_SEND_INIT;
  int hdrType;
  bool transferComplete = false;
  int retryCount = 0;

  // Initial delay to let receiver prepare
  delay(100);

  // Send "rz\r" to trigger auto-start on some receivers
  zmSendRaw('r');
  zmSendRaw('z');
  zmSendRaw('\r');
  SerialFlush();

  while (!transferComplete && retryCount < zmCtx.maxErrors) {
    if (checkCancel()) {
      zmDebugLog("User cancelled");
      zmSendCancel();
      break;
    }

    switch (zmCtx.state) {
      case ZM_SEND_INIT:
        // Send ZRQINIT and wait for ZRINIT
        zmSendZRQINIT();

        hdrType = zmRecvHeader(ZMODEM_INIT_TIMEOUT);

        if (hdrType == ZRINIT) {
          zmParseZRINIT();
          zmCtx.state = ZM_SEND_ZFILE;
          retryCount = 0;
        } else if (hdrType == ZRQINIT) {
          // Other end is also sending, that's an error
          zmDebugLog("Collision - other end also sending");
          SerialPrintLn("Error: Both sides trying to send");
          zmSendCancel();
          transferComplete = true;
        } else if (hdrType == ZM_TIMEOUT || hdrType < 0) {
          retryCount++;
          zmDebugLog("Timeout waiting for ZRINIT, retry %d", retryCount);
        } else {
          zmDebugLog("Unexpected header 0x%02X in SEND_INIT", hdrType);
          retryCount++;
        }
        break;

      case ZM_SEND_ZFILE:
        // Send file info and wait for ZRPOS, ZCRC, or ZSKIP
        zmSendZFILE(zmCtx.fileName, zmCtx.fileSize, 0);

        hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

        if (hdrType == ZRPOS) {
          // Receiver wants data from this position
          uint32_t startPos = zmCtx.rxdHeaderPos;
          zmDebugLog("Receiver requested pos %lu", startPos);

          if (startPos > 0 && startPos < zmCtx.fileSize) {
            // Resume transfer
            f.seek(startPos);
            zmCtx.filePos = startPos;
            SerialPrintLn("Resuming at " + String(startPos));
          } else {
            zmCtx.filePos = 0;
          }
          zmCtx.state = ZM_SEND_DATA;
          retryCount = 0;
        } else if (hdrType == ZCRC) {
          // Receiver wants file CRC before accepting
          zmDebugLog("Receiver requested file CRC");
          SerialPrintLn("Calculating file CRC...");
          uint32_t fileCRC = zmCalculateFileCRC(f);
          zmSendZCRC(fileCRC);
          // Stay in SEND_ZFILE state - receiver will send ZRPOS or ZSKIP next
        } else if (hdrType == ZSKIP) {
          zmDebugLog("Receiver skipped file");
          SerialPrintLn("File skipped by receiver");
          zmCtx.state = ZM_SEND_FIN;
        } else if (hdrType == ZRINIT) {
          // Receiver didn't get ZFILE, retry
          zmDebugLog("Got ZRINIT again, retrying ZFILE");
          retryCount++;
        } else if (hdrType == ZM_TIMEOUT || hdrType < 0) {
          retryCount++;
          zmDebugLog("Timeout waiting for ZRPOS, retry %d", retryCount);
        } else {
          zmDebugLog("Unexpected header 0x%02X in SEND_ZFILE", hdrType);
          retryCount++;
        }
        break;

      case ZM_SEND_DATA:
        // Send file data
        showMessage(xferMsg + String(zmCtx.filePos) + "/" + String(zmCtx.fileSize));

        // Send ZDATA header with current position
        zmSendZDATA(zmCtx.filePos);

        // Send data subpackets
        while (zmCtx.filePos < zmCtx.fileSize) {
          if (checkCancel()) {
            zmDebugLog("User cancelled during data send");
            zmSendCancel();
            transferComplete = true;
            break;
          }

          int toRead = min((size_t)(zmCtx.fileSize - zmCtx.filePos), (size_t)ZMODEM_BLOCK_SIZE);
          int bytesRead = f.read(zBuf, toRead);

          if (bytesRead <= 0) {
            zmDebugLog("Read error at pos %lu", zmCtx.filePos);
            break;
          }

          // Determine frame end type
          uint8_t frameEnd;
          bool isLast = (zmCtx.filePos + bytesRead >= zmCtx.fileSize);

          if (isLast) {
            // Last packet - end frame, expect ACK
            frameEnd = ZCRCW;
          } else if (zmCtx.canFullDuplex && zmCtx.canOverlapIO) {
            // Full streaming mode
            frameEnd = ZCRCG;
          } else {
            // Safe mode - end frame, expect ACK every 4KB
            if ((zmCtx.filePos + bytesRead) % 4096 == 0) {
              frameEnd = ZCRCW;
            } else {
              frameEnd = ZCRCG;
            }
          }

          zmSendDataSubpacket(zBuf, bytesRead, frameEnd);
          zmCtx.filePos += bytesRead;
          xferBlock++;

          // If we sent ZCRCW, wait for acknowledgment
          if (frameEnd == ZCRCW) {
            // Update display here since we're pausing for ACK anyway
            // Don't update during ZCRCG streaming to avoid blocking
            showMessage(xferMsg + String(zmCtx.filePos) + "/" + String(zmCtx.fileSize));

            hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

            if (hdrType == ZACK) {
              // Continue
            } else if (hdrType == ZRPOS) {
              // Receiver wants to restart from a position
              uint32_t newPos = zmCtx.rxdHeaderPos;
              zmDebugLog("ZRPOS during data: pos=%lu", newPos);
              f.seek(newPos);
              zmCtx.filePos = newPos;
              zmCtx.errorCount++;
              if (zmCtx.errorCount > zmCtx.maxErrors) {
                zmDebugLog("Too many errors");
                zmSendCancel();
                transferComplete = true;
              }
              break;  // Restart data loop
            } else if (hdrType == ZM_TIMEOUT) {
              zmDebugLog("Timeout waiting for ACK");
              zmCtx.errorCount++;
              if (zmCtx.errorCount > zmCtx.maxErrors) {
                zmSendCancel();
                transferComplete = true;
              }
              break;
            }
          }
        }

        if (zmCtx.filePos >= zmCtx.fileSize && !transferComplete) {
          zmCtx.state = ZM_SEND_EOF;
        }
        break;

      case ZM_SEND_EOF:
        // Send ZEOF
        zmSendZEOF(zmCtx.filePos);

        hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

        if (hdrType == ZRINIT) {
          // File complete, receiver ready for next file
          zmDebugLog("File transfer complete");
          zmCtx.state = ZM_SEND_FIN;
        } else if (hdrType == ZRPOS) {
          // Receiver wants to restart
          uint32_t newPos = zmCtx.rxdHeaderPos;
          zmDebugLog("ZRPOS after EOF: pos=%lu", newPos);
          f.seek(newPos);
          zmCtx.filePos = newPos;
          zmCtx.state = ZM_SEND_DATA;
        } else {
          retryCount++;
          zmDebugLog("Unexpected response to ZEOF: 0x%02X", hdrType);
        }
        break;

      case ZM_SEND_FIN:
        // Send ZFIN and wait for response
        zmSendZFIN();

        hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

        if (hdrType == ZFIN) {
          // Send "OO" to complete
          zmSendRaw('O');
          zmSendRaw('O');
          SerialFlush();
          zmDebugLog("Transfer complete - sent OO");
          transferComplete = true;
          zmCtx.state = ZM_DONE;
        } else {
          retryCount++;
        }
        break;

      default:
        zmDebugLog("Invalid state: %d", zmCtx.state);
        transferComplete = true;
        break;
    }
  }

  f.close();

  if (retryCount >= zmCtx.maxErrors) {
    zmDebugLog("Too many retries, aborting");
    SerialPrintLn("Transfer failed - too many errors");
    zmSendCancel();
  } else if (zmCtx.state == ZM_DONE) {
    SerialPrintLn("Transfer complete: " + String(zmCtx.filePos) + " bytes");
  }

  zmDebugLog("=== ZMODEM SEND END ===");
  zmCloseDebugLog();
}

// ============================================================================
// Main Receive Function
// ============================================================================

void receiveFileZMODEM() {
  resetGlobals();

  // Initialize ZModem context with debug logging
  zmInit(true);
  zmOpenDebugLog();

  zmDebugLog("=== ZMODEM RECEIVE START ===");

  xferMsg = "ZMODEM RECV\nWaiting...\n";
  showMessage(xferMsg);

  // Get start delay from user (default 0 for instant start)
  waitTime = prompt("Start delay (seconds): ", "0");
  int delaySeconds = waitTime.toInt();

  if (delaySeconds > 0) {
    SerialPrintLn("Starting in " + String(delaySeconds) + " seconds...");
    unsigned long st = millis();
    while (millis() - st < (unsigned long)delaySeconds * 1000) {
      yield();
      if (checkCancel()) {
        zmCloseDebugLog();
        return;
      }
    }
  }

  // Clear input buffer
  while (SerialAvailable() > 0) {
    SerialRead();
  }

  SerialPrintLn("Ready to receive...");
  zmDebugLog("Receiver ready");

  // State machine for receiving
  zmCtx.state = ZM_RECV_INIT;
  int hdrType;
  bool transferComplete = false;
  int retryCount = 0;
  File outFile;
  bool fileOpen = false;

  while (!transferComplete && retryCount < zmCtx.maxErrors) {
    if (checkCancel()) {
      zmDebugLog("User cancelled");
      zmSendCancel();
      break;
    }

    switch (zmCtx.state) {
      case ZM_RECV_INIT:
        // Clear any stale data in buffer before starting protocol
        while (SerialAvailable() > 0) {
          SerialRead();
        }

        // Send ZRINIT and wait for ZFILE or ZRQINIT
        zmSendZRINIT();

        hdrType = zmRecvHeader(ZMODEM_INIT_TIMEOUT);

        if (hdrType == ZRQINIT) {
          // Sender is initiating, send ZRINIT again
          zmDebugLog("Got ZRQINIT, sending ZRINIT");
          retryCount = 0;
          // Loop back to send ZRINIT
        } else if (hdrType == ZFILE) {
          zmDebugLog("Got ZFILE header");
          zmCtx.state = ZM_RECV_FILE_INFO;
          retryCount = 0;
        } else if (hdrType == ZFIN) {
          // Sender is done (no files or already sent all)
          zmDebugLog("Got ZFIN - sender has no files");
          zmSendZFIN();
          transferComplete = true;
          zmCtx.state = ZM_DONE;
        } else if (hdrType == ZM_TIMEOUT || hdrType < 0) {
          retryCount++;
          zmDebugLog("Timeout waiting for sender, retry %d", retryCount);
        } else {
          zmDebugLog("Unexpected header 0x%02X in RECV_INIT", hdrType);
          retryCount++;
        }
        break;

      case ZM_RECV_FILE_INFO:
        // Receive file info subpacket
        {
          uint8_t frameEnd;
          int infoLen = zmRecvDataSubpacket(zBuf, sizeof(zBuf), &frameEnd);

          if (infoLen < 0) {
            zmDebugLog("Error receiving file info: %d", infoLen);
            zmSendZNAK();
            zmCtx.state = ZM_RECV_INIT;
            retryCount++;
            break;
          }

          // Parse file info
          char parsedName[64];
          uint32_t parsedSize, parsedTime;

          if (!zmParseZFILE(zBuf, infoLen, parsedName, &parsedSize, &parsedTime)) {
            zmDebugLog("Failed to parse ZFILE info");
            zmSendZNAK();
            zmCtx.state = ZM_RECV_INIT;
            retryCount++;
            break;
          }

          strncpy(zmCtx.fileName, parsedName, sizeof(zmCtx.fileName) - 1);
          zmCtx.fileSize = parsedSize;
          zmCtx.fileTime = parsedTime;
          zmCtx.filePos = 0;

          SerialPrintLn("Receiving: " + String(zmCtx.fileName) + " (" + String(zmCtx.fileSize) + " bytes)");
          xferMsg = "ZMODEM RECV\n" + String(zmCtx.fileName) + "\n";
          showMessage(xferMsg + "0/" + String(zmCtx.fileSize));

          // Check if file exists for resume
          String filePath = "/" + String(zmCtx.fileName);
          if (SD.exists(filePath)) {
            File existing = SD.open(filePath);
            if (existing) {
              uint32_t existingSize = existing.size();
              existing.close();

              if (existingSize > 0 && existingSize < zmCtx.fileSize) {
                // Resume from existing size
                zmCtx.filePos = existingSize;
                zmDebugLog("Resuming from position %lu", zmCtx.filePos);
                SerialPrintLn("Resuming at " + String(zmCtx.filePos));
              } else {
                // Remove and start fresh
                SD.remove(filePath);
              }
            }
          }

          // Open file for writing
          if (zmCtx.filePos > 0) {
            outFile = SD.open(filePath, FILE_APPEND);
          } else {
            outFile = SD.open(filePath, FILE_WRITE);
          }

          if (!outFile) {
            zmDebugLog("Failed to create file: %s", zmCtx.fileName);
            SerialPrintLn("Can't create file");
            zmSendZSKIP();
            zmCtx.state = ZM_RECV_INIT;
            break;
          }

          if (zmCtx.filePos > 0) {
            outFile.seek(zmCtx.filePos);
          }

          fileOpen = true;

          // Send ZRPOS to request data
          zmSendZRPOS(zmCtx.filePos);
          zmCtx.state = ZM_RECV_DATA;
        }
        break;

      case ZM_RECV_DATA:
        // Wait for ZDATA header
        hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

        if (hdrType == ZDATA) {
          uint32_t dataPos = zmCtx.rxdHeaderPos;
          zmDebugLog("Got ZDATA at pos %lu", dataPos);

          // Check if position matches
          if (dataPos != zmCtx.filePos) {
            zmDebugLog("Position mismatch: expected %lu, got %lu", zmCtx.filePos, dataPos);
            zmSendZRPOS(zmCtx.filePos);
            break;
          }

          // Receive data subpackets
          bool moreData = true;
          while (moreData && zmCtx.filePos < zmCtx.fileSize) {
            uint8_t frameEnd;
            int dataLen = zmRecvDataSubpacket(zBuf, sizeof(zBuf), &frameEnd);

            if (dataLen == ZM_CRC_ERROR) {
              zmDebugLog("CRC error in data");
              zmCtx.errorCount++;
              // Flush any remaining data in serial buffer before requesting retransmit
              delay(100);  // Wait for any in-flight data
              int flushed = 0;
              while (PhysicalSerial.available()) {
                PhysicalSerial.read();
                flushed++;
              }
              while (Serial.available()) {
                Serial.read();
                flushed++;
              }
              if (flushed > 0) {
                zmDebugLog("Flushed %d bytes from serial buffer", flushed);
              }
              // Try ZNAK first (resend last block) instead of ZRPOS (resume at position)
              // Some implementations (like older SyncTerm) expect ZNAK on data errors
              zmSendZNAK();
              moreData = false;
              break;
            } else if (dataLen < 0) {
              zmDebugLog("Error receiving data: %d", dataLen);
              zmCtx.errorCount++;
              if (zmCtx.errorCount > zmCtx.maxErrors) {
                zmSendCancel();
                transferComplete = true;
              } else {
                zmSendZRPOS(zmCtx.filePos);
              }
              moreData = false;
              break;
            }

            // Write data to file
            int toWrite = min(dataLen, (int)(zmCtx.fileSize - zmCtx.filePos));
            if (toWrite > 0) {
              int written = outFile.write(zBuf, toWrite);
              if (written != toWrite) {
                zmDebugLog("Write error: wrote %d of %d", written, toWrite);
                zmSendZABORT();
                transferComplete = true;
                break;
              }
              zmCtx.filePos += written;
              xferBytes = zmCtx.filePos;
            }

            // Handle frame end types
            switch (frameEnd) {
              case ZCRCW:
                // End of frame, send ACK
                // Update display here since we're pausing anyway
                showMessage(xferMsg + String(zmCtx.filePos) + "/" + String(zmCtx.fileSize));
                zmSendZACK(zmCtx.filePos);
                moreData = false;
                break;
              case ZCRCQ:
                // Continue, send ACK
                zmSendZACK(zmCtx.filePos);
                break;
              case ZCRCG:
                // Continue, no ACK needed - keep receiving fast!
                break;
              case ZCRCE:
                // End of frame, header follows
                // Exit loop to receive next header - don't block!
                moreData = false;
                break;
            }

            // Reset error count on successful receive
            zmCtx.errorCount = 0;
          }
        } else if (hdrType == ZEOF) {
          uint32_t eofPos = zmCtx.rxdHeaderPos;
          zmDebugLog("Got ZEOF at pos %lu (current pos: %lu)", eofPos, zmCtx.filePos);

          // Only accept ZEOF if position matches our current file position
          // This ensures we've actually received all the bytes
          if (eofPos == zmCtx.filePos) {
            zmCtx.state = ZM_RECV_EOF;
          } else {
            zmDebugLog("ZEOF position mismatch: expected %lu, got %lu", zmCtx.filePos, eofPos);
            zmSendZRPOS(zmCtx.filePos);
          }
        } else if (hdrType == ZFILE) {
          // Sender retransmitted ZFILE, resend ZRPOS
          zmDebugLog("Got ZFILE again, resending ZRPOS");
          zmSendZRPOS(zmCtx.filePos);
        } else if (hdrType == ZM_TIMEOUT) {
          retryCount++;
          zmDebugLog("Timeout in RECV_DATA, retry %d", retryCount);
          zmSendZRPOS(zmCtx.filePos);
        } else {
          zmDebugLog("Unexpected header 0x%02X in RECV_DATA", hdrType);
          retryCount++;
        }
        break;

      case ZM_RECV_EOF:
        // File complete, close and prepare for next
        if (fileOpen) {
          outFile.close();
          fileOpen = false;
          zmDebugLog("File complete: %s (%lu bytes)", zmCtx.fileName, zmCtx.filePos);
          SerialPrintLn("File complete: " + String(zmCtx.filePos) + " bytes");
        }

        // Send ZRINIT to signal ready for next file
        zmSendZRINIT();

        hdrType = zmRecvHeader(ZMODEM_TIMEOUT);

        if (hdrType == ZFILE) {
          // Another file coming
          zmCtx.state = ZM_RECV_FILE_INFO;
        } else if (hdrType == ZFIN) {
          // Sender is done
          zmDebugLog("Got ZFIN - transfer complete");
          zmSendZFIN();

          // Wait for OO
          int c1 = zmRecvByte(1000);
          int c2 = zmRecvByte(1000);
          if (c1 == 'O' && c2 == 'O') {
            zmDebugLog("Got OO - session complete");
          }

          transferComplete = true;
          zmCtx.state = ZM_DONE;
        } else {
          retryCount++;
          zmDebugLog("Unexpected header after ZEOF: 0x%02X", hdrType);
        }
        break;

      default:
        zmDebugLog("Invalid state: %d", zmCtx.state);
        transferComplete = true;
        break;
    }
  }

  // Cleanup
  if (fileOpen) {
    outFile.close();
  }

  if (retryCount >= zmCtx.maxErrors) {
    zmDebugLog("Too many retries, aborting");
    SerialPrintLn("Transfer failed - too many errors");
    zmSendCancel();
  } else if (zmCtx.state == ZM_DONE) {
    SerialPrintLn("Transfer complete");
  }

  zmDebugLog("=== ZMODEM RECEIVE END ===");
  zmCloseDebugLog();
}

/**
   Arduino main init function (setup() is in main.cpp, not here)
*/

void clearInputBuffer()
{
  while(SerialAvailable()>0)
  {
    char discard = SerialRead();
  }
}

void resetGlobals()
{
  packetNumber = 0;
  lastPacket = false;
  eotSent = 0;
  readyToReceive = false;
  zeroPacket = true;
  blockSize = 1024;
  packetSize = 1029;
  finalFileSent = false;
  fileIndex = 0;
  xferBytes = 0;
}
void SerialWriteLog(char c, String log)
{
  if (log!="")
    addLog("> [" + log + "] 0x" + String(c, HEX) + " " + String(c) + "\r\n");
  updateXferMessage();
  SerialWrite(c);
}
void addLog(String logmsg)
{
  //logFile = SD.open("logfile.txt", FILE_WRITE);
  //logFile.println(logmsg);
  //logFile.flush();
  //logFile.close();
}
void sendFileXMODEM()
{
  resetGlobals();
  packetNumber = 1;
  fileName=prompt("Please enter the filename to send: ");
  SerialPrintLn("Sending " + fileName);
  SerialPrintLn("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //SerialPrintLn("Couldn't open log file");
  File dataFile = SD.open("/"+fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING XMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopXMODEM(dataFile, fileName, fileSize);
    SerialPrintLn("Download Complete");
    dataFile.close();
  } else {
    SerialPrintLn("Invalid File");
  }
}
void sendFileYMODEM()
{
  resetGlobals();
  fileName=prompt("Please enter the filename to send: ");
  SerialPrintLn("Sending " + fileName);
  SerialPrintLn("WAITING FOR TRANSFER START...");
  SD.remove("logfile.txt");
  //if (!logFile)
    //SerialPrintLn("Couldn't open log file");
  File dataFile = SD.open("/"+fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING YMODEM\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    sendLoopYMODEM(dataFile, fileName, fileSize);
    SerialPrintLn("Download Complete");
    dataFile.close();
  } else {
    SerialPrintLn("Invalid File");
  }
}
void sendFileRaw()
{
  digitalWrite(LED_PIN, HIGH);
  resetGlobals();
  fileName=prompt("Please enter the filename: ");
  File dataFile = SD.open("/"+fileName);
  if (dataFile) { //make sure the file is valid
    xferMsg = "SENDING RAW\nBytes Sent:\n";
    String fileSize = getFileSize(fileName);
    waitTime = prompt("Start time in seconds: ", "30");  
    SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
    digitalWrite(LED_PIN, LOW);
    delay(waitTime.toInt()*1000);
    digitalWrite(LED_PIN, HIGH);
    int c=0;
    for (int i=0; i<fileSize.toInt(); i++)
    {
      if (dataFile.available())
      {
        SerialWrite(dataFile.read());
        if (c==512)
        { //flush the buffer every so often to avoid a WDT reset
          SerialFlush();
          led_on();
          yield();
          c=0;
        }
        else
          c++;
      }
    }
    SerialFlush();
    SerialWrite((char)26); //DOS EOF
    SerialFlush();
    yield();
    digitalWrite(LED_PIN, LOW);
    dataFile.close();
  } else {
    SerialPrintLn("\r\nInvalid File");
  }
}
void receiveFileRaw()
{
  fileName=prompt("Please enter the filename: ");
  xferFile = SD.open("/"+fileName, FILE_WRITE);
  if (xferFile) {
    xferMsg = "RECEIVE RAW\nBytes Recevied:\n";
    while (true)
    {
      if (SerialAvailable() > 0) {
        char c = SerialRead();
        if (c==27)
          break;
        else
        {
          updateXferMessage();
          xferFile.write(c);
        }
      }
    }
    xferFile.flush();
    xferFile.close();
  } else
    SerialPrintLn("Transfer Cancelled");
}
void sendLoopXMODEM(File dataFile, String fileName, String fileSize)
{
  bool forceStart=false;
 
  while(true)
  {
    //readSwitches();
    //if (SW3 && packetNumber==1) {
    //  waitSwitches();
    //  forceStart=true;
    //}
    if (SerialAvailable() > 0 || forceStart) {
      char c = SerialRead();
      if (forceStart)
        c=0x15; //NAK will start the transfer
       
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          if (lastPacket)
          {
            SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
            eotSent++;
            if (eotSent==2)
              return;
          }
          else
          {
            packetNumber++;
            addLog("packetNumber: " + String(packetNumber));
            addLog("sending packet after ACK");
            sendPacket(dataFile);
          }
          break;
        case 0x15: //NAK
          blockSize = 128;
        case 0x43: //'C'
          //SerialPrintLn("NAK");
          if (lastPacket)
          {
              SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //SerialPrintLn("EOT");
              eotSent++;
              if (eotSent==2)
                return;
          }
          else
          {
            addLog("Sending Packet after C");
            sendPacket(dataFile);
          }
          break;
      }
    }
  }
}
void sendLoopYMODEM(File dataFile, String fileName, String fileSize)
{
  while(true)
  {
    if (SerialAvailable() > 0) {
      char c = SerialRead();
      addLog("< " + String(c, HEX) + "\r\n");
      switch (c)
      {
        case 0x06: //ACK
          //SerialPrintLn("ACK");
          if (lastPacket && eotSent<2)
          {
            SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
            //SerialPrintLn("EOT");
            eotSent++;
          }
          else if (lastPacket && (eotSent==2) && finalFileSent)
          {
            return;
          }
          else
          {
            packetNumber++;
            addLog("packetNumber: " + String(packetNumber));
            if (zeroPacket == true)
              zeroPacket = false; //wait for 'C' then send first actual packet
            else
            {
              addLog("sending packet after ACK");
              sendPacket(dataFile);
            }
          }
          break;
        case 0x15: //NAK
        case 0x43: //'C'
          //SerialPrintLn("NAK");
          finalFileSent = false;
          if (lastPacket && eotSent<2)
          {
              SerialWriteLog(0x04, "EOT after last packet " + String(eotSent)); //EOT
              //SerialPrintLn("EOT");
              eotSent++;
          }
          else if (lastPacket && (eotSent == 2))
          {
            addLog("Sending Final Packet");
            sendFinalPacket();
            finalFileSent = true;
          }
          else if (packetNumber==0)
          {
            addLog("Sending Zero Packet");
            sendZeroPacket(fileName, fileSize);
          }
          else
          {
            addLog("Sending Packet after C");
            sendPacket(dataFile);
          }
          break;
      }
    }
  }
}

// getFileSize() moved to src/modules/playback.cpp
void sendZeroPacket(String fileName, String fileSize)
{
  LinkedList<byte> packetData = LinkedList<byte>();
  for(int i = 0; i<fileName.length(); i++)
  {
    packetData.add(fileName[i]); //add the filename
  }
  packetData.add(0x00); //add NUL as delimiter
  //add the file size as an ascii-converted hex number
  for (int i = 0; i<fileSize.length(); i++)
  {
      packetData.add(fileSize[i]);
  }
  packetData.add(0x00);
  while (packetData.size() < 128)
  {
      packetData.add(0x00);
  }
  //uint16_t crc = ComputeCRC(packetData);
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < packetData.size(); i++)
  {
    crc.add(packetData.get(i));
  }
  uint16_t c = crc.getCRC();
  uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
  uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left
  LinkedList<byte> packet = LinkedList<byte>();
  packet.add(0x01); //SOH
  packet.add(packetNumber);
  packet.add(0xFF); //inverse packet number
  for (int i = 0; i<packetData.size(); i++)
  {
      packet.add(packetData.get(i));
  }
  packet.add(hi);
  packet.add(lo);
  //Console.println("Sending packet " + packetNumber.ToString() + " " + packet.ToArray().Length.ToString());
  //convert it back to a byte array
  byte p[packet.size()];
  for (int i = 0; i<packet.size(); i++)
  {
      p[i]=packet.get(i);
  }

  sendTelnet(p, sizeof(p));
}
void sendFinalPacket()
{
  LinkedList<byte> packetData = LinkedList<byte>();
  while (packetData.size() < 128)
  {
      packetData.add(0x00);
  }
  //uint16_t crc = ComputeCRC(packetData);
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < packetData.size(); i++)
  {
    crc.add(packetData.get(i));
  }
  uint16_t c = crc.getCRC();
  uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
  uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left
  LinkedList<byte> packet = LinkedList<byte>();
  packet.add(0x01); //SOH
  packet.add(0x00);
  packet.add(0xFF); //inverse packet number
  for (int i = 0; i<packetData.size(); i++)
  {
    packet.add(packetData.get(i));
  }
  packet.add(hi);
  packet.add(lo);
  //Console.println("Sending packet " + packetNumber.ToString() + " " + packet.ToArray().Length.ToString());
  //convert it back to a byte array
  byte p[packet.size()];
  for (int i = 0; i<packet.size(); i++)
  {
    p[i]=packet.get(i);
  }

  sendTelnet(p, sizeof(p));
}
void sendPacket(File dataFile)
{
  byte packetInverse = (byte)(0xFF - (int)packetNumber);
  LinkedList<byte> packetData = LinkedList<byte>();
  addLog("Sending Packet: " + String(packetNumber) + " - Block Size: " + blockSize);
  for (int i=0; i<blockSize; i++)
  {
    if (dataFile.available())
    {
      packetData.add(dataFile.read());
    }
  }
  if (packetData.size() == 0)
  {
    addLog("setting Last Packet");
    lastPacket = true;
  }
  else
  {
    while (packetData.size() < blockSize)
    {
      //addLog("filling");
      packetData.add(0x1A); //Fill packet
      lastPacket = true;
    }
    if (lastPacket)
      addLog("Last Packet Set");
     
    crc.reset();
    crc.setPolynome(0x1021);
    for (int i = 0; i < packetData.size(); i++)
    {
      crc.add(packetData.get(i));
    }
    uint16_t c = crc.getCRC();
    uint8_t hi = (byte)(c >> 8); // Shift 8 bytes -> tell get dropped
    uint8_t lo = (byte)(c & 0xFF); // Only last Byte is left

    LinkedList<byte> packet = LinkedList<byte>();
    packet.add(0x02); //STX
    packet.add(packetNumber);
    packet.add(packetInverse);
    for (int i = 0; i<packetData.size(); i++)
    {
      packet.add(packetData.get(i));
    }
    packet.add(hi);
    packet.add(lo);
    addLog("Packet Size: " + String(packet.size()));
    byte p[packet.size()];
    for (int i = 0; i<packet.size(); i++)
    {
      p[i]=packet.get(i);
    }
 
    sendTelnet(p, sizeof(p));
  }
}

/*ushort ComputeCRC(LinkedList<byte> bytes)
{
  crc.reset();
  crc.setPolynome(0x1021);
  for (int i = 0; i < bytes.size(); i++)
  {
    crc.add(bytes.get(i));
  }
  return crc.getCRC();
}*/
void sendTelnet(byte packet[], int sz)
{  
  String logmsg = "";
  for (int i=0; i<sz; i++)
  {
    //logmsg = "P#" + String(packetNumber) + " b#" + String(i);
   
    if (packet[i] == 0xFF && EscapeTelnet)
      SerialWriteLog(0xFF, "escaping FF");
     
    SerialWriteLog(packet[i], logmsg);
  }
  SerialFlush();
}
void receiveFileXMODEM()
{
  SD.remove("logfile.txt");
  SerialPrintLn("");
  SerialPrintLn("");
  fileName=prompt("Please enter the filename to receive: ");

  SD.remove(fileName);
  xferFile = SD.open("/"+fileName, FILE_WRITE);
  if (xferFile) {
    xferMsg = "RECEIVE XMODEM\nBytes Recevied:\n";
    waitTime = prompt("Start time in seconds: ", "30");  
    SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
    receiveLoopXMODEM();
   
    xferFile.flush();
    xferFile.close();
    clearInputBuffer();
    SerialPrintLn("Download Complete");
  } else
    SerialPrintLn("Transfer Cancelled");
}
void receiveFileYMODEM()
{
  SD.remove("logfile.txt");
  xferMsg = "RECEIVE YMODEM\nBytes Recevied:\n";
  SerialPrintLn("");
  SerialPrintLn("");
  waitTime = prompt("Start time in seconds: ", "30");  
  SerialPrintLn("\r\nStarting transfer in " + waitTime + " seconds...");
  receiveLoopYMODEM();
  xferFile.flush();
  xferFile.close();
  clearInputBuffer();
  SerialPrintLn("Download Complete");
}

bool sendStartByte(void *)
{
  //if (packetNumber>0)
  if (packetNumber>1)
    return false;
  else
  {
    SerialWriteLog(0x43, "Sending Start C from Timer"); //letter C
    readyToReceive = true;
    return true;
  }
}
void receiveLoopXMODEM()
{
  LinkedList<char> buffer = LinkedList<char>();  
  resetGlobals();
  packetNumber=1;

  timer.every(waitTime.toInt()*1000, sendStartByte);

  while (true)
  {    
    timer.tick();

    if (SerialAvailable() > 0) {
      char c = SerialRead();

      //addLog("RCV: " + String(c, HEX) + " " + String(c));

      if (!readyToReceive)
      {
        //we got something so maybe client is ready to send? clear the incoming buffer,
        //wait and send a C to indicate I'm ready to receive
        //timer.Enabled = false;
        timer.cancel();
        delay(100); //wait for anything else incoming
        clearInputBuffer();
        //delay(2000); //wait a bit
        SerialWriteLog(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          SerialWriteLog(0x06, "ACK after EOT"); //ACK              
          return;
        }
        else
        {
          updateXferMessage();
          buffer.add(c);

          //if we've hit the expected packet size
          bool processPacket = false;
          if (buffer.size() == packetSize && (buffer.get(0) == 0x02))
          {
            processPacket = true;
            blockSize = 1024;
            packetSize = 1029;
          }
          else if (buffer.size() == 133 && buffer.get(0) == 0x01)
          {
            //even though we're in 1K mode, I still have to process 128-byte packets
            addLog("buffer.get(0): " + String(buffer.get(0)));
            addLog("buffer.get(1): " + String(buffer.get(1)));
            processPacket = true;
            blockSize = 128;
            packetSize = 133;
          }
   
          if (processPacket)
          {
            addLog(String(buffer.size()) + " Process now");
            if (processPacket)
            {
              bool goodPacket = false;
              //verify the packet number matches what's expected
              if (buffer.get(1) == packetNumber)
              {
                //check the inverse for good measure
                if (buffer.get(2) == 255 - packetNumber)
                {
                  //get the CRC for the packet's data
                  crc.reset();
                  crc.setPolynome(0x1021);
                  for (int i = 3; i < packetSize-2; i++)
                  {
                    crc.add(buffer.get(i));
                  }
                  uint16_t c = crc.getCRC();
                  uint8_t hi = (char)(c >> 8); // Shift 8 bytes -> tell get dropped
                  uint8_t lo = (char)(c & 0xFF); // Only last Byte is left
     
                  //compare it to the received CRC - upper byte first
                  addLog("hi CRC: " + String(hi) + " " + buffer.get(packetSize - 2));
                  if (hi == buffer.get(packetSize - 2))
                  {
                    addLog("lo CRC: " + String(lo) + " " + buffer.get(packetSize - 1));
                    //and then compare the lower byte
                    if (lo == buffer.get(packetSize - 1))
                    {
                      goodPacket = true; //mark as a good packet so we send ACK below
                      //since it's good, add this packet to the file
                      for (int i = 3; i < packetSize - 2; i++)
                      {
                          xferFile.write(buffer.get(i));
                      }
                      xferFile.flush();
                      buffer.clear(); //clear the input buffer to get ready for next pack
                    }
                    else
                      addLog("Bad CRC (lower byte)");
                  }
                  else
                    addLog("Bad CRC (upper byte)");
                }
                else
                  addLog("Wrong inverse packet number");
              }
              else {
                addLog("Wrong packet number, expected ");
                addLog(String(packetNumber, HEX));
                addLog(" got ");
                addLog(String(buffer.get(1), HEX));
              }
              if (goodPacket)
              {
                addLog("\nGood packet, sending ACK");
                packetNumber++; //expect the next packet
                SerialWriteLog(0x06, "ACK after good packet"); //ACK
              }              
            }
          }
        }
      }
    }
  }
}
void receiveLoopYMODEM()
{
  LinkedList<char> buffer = LinkedList<char>();  
  bool lastByteIAC = false;
  resetGlobals();
  fileName="";
  String fileSize="";
  unsigned int fileSizeNum=0;
  unsigned int bytesWritten=0;

  timer.every(waitTime.toInt()*1000, sendStartByte);

  int XCount=0;

  while (true)
  {    
    timer.tick();

    if (SerialAvailable() > 0) {
      char c = SerialRead();

      //addLog("received: " + String(c, HEX) + " " + String(c) + " " + String(buffer.size()));
      //addLog("received: " + String(c, HEX) + " " + String(buffer.size()));
      addLog("received: " + String(c));
     
      if (c=='X' && XCount>=4)
        return;
      else if (c=='X')
        XCount++;
      else
        XCount=0;      

      if (!readyToReceive)
      {
        //we got something so maybe client is ready to send? clear the incoming buffer,
        //wait and send a C to indicate I'm ready to receive
        //timer.Enabled = false;
        timer.cancel();
        delay(100); //wait for anything else incoming
        clearInputBuffer();
        //delay(2000); //wait a bit
        SerialWriteLog(0x43, "Sending C to start transfer"); //'C'
        readyToReceive = true;
      }
      else
      {
        updateXferMessage();
        if (buffer.size() == 0 && c == 0x04) //EOT
        {
          //received EOT on its own - transmission is complete, send ACK and return the file
          SerialWriteLog(0x06, "ACK after EOT"); //ACK              
          return;
        }
        else
        {
          buffer.add(c);

          //if we've hit the expected packet size
          bool processPacket = false;
          if (buffer.size() == packetSize && (buffer.get(0) == 0x02))
          {
            processPacket = true;
            blockSize = 1024;
            packetSize = 1029;
          }
          else if (buffer.size() == 133 && buffer.get(0) == 0x01)
          {
            //even though we're in 1K mode, I still have to process 128-byte packets
           
            addLog("buffer.get(0): " + String(buffer.get(0)));
            addLog("buffer.get(1): " + String(buffer.get(1)));
            processPacket = true;
            blockSize = 128;
            packetSize = 133;
          }
   
          if (processPacket)
          {
            addLog(String(buffer.size()) + " Process now");
            if (processPacket)
            {
              bool goodPacket = false;
              //verify the packet number matches what's expected
              if (buffer.get(1) == packetNumber)
              {
                //check the inverse for good measure
                if (buffer.get(2) == 255 - packetNumber)
                {
                  //get the CRC for the packet's data
                  crc.reset();
                  crc.setPolynome(0x1021);
                  for (int i = 3; i < packetSize-2; i++)
                  {
                    crc.add(buffer.get(i));
                  }
                  uint16_t c = crc.getCRC();
                  uint8_t hi = (char)(c >> 8); // Shift 8 bytes -> tell get dropped
                  uint8_t lo = (char)(c & 0xFF); // Only last Byte is left
     
                  //compare it to the received CRC - upper byte first
                  addLog("hi CRC: " + String(hi) + " " + buffer.get(packetSize - 2));
                  if (hi == buffer.get(packetSize - 2))
                  {
                    addLog("lo CRC: " + String(lo) + " " + buffer.get(packetSize - 1));
                    //and then compare the lower byte
                    if (lo == buffer.get(packetSize - 1))
                    {
                      goodPacket = true; //mark as a good packet so we send ACK below
                      if (zeroPacket)
                      {
                        //parse the zero packet for the filename and size
                        int zeroCount=0;
                        for (int i = 3; i < packetSize - 2; i++)
                        {
                          if (buffer.get(i)==0x0 || (zeroCount>0 && buffer.get(i)==0x20))
                          {
                            zeroCount++;
                            if (zeroCount==2)
                              break;
                          } else {
                            if (zeroCount==0)
                              fileName += char(buffer.get(i));
                            else
                              fileSize += char(buffer.get(i));
                          }
                        }
                        SD.remove(fileName);
                        fileSizeNum = fileSize.toInt();
                        xferFile = SD.open("/"+fileName, FILE_WRITE);
                        addLog("receiving file " + fileName + " of size " + String(fileSizeNum));
                      }
                      else
                      {
                        //since it's good, add this packet to the file
                        for (int i = 3; i < packetSize - 2; i++)
                        {
                          if (bytesWritten < fileSizeNum)
                          {
                            xferFile.write(buffer.get(i));
                            bytesWritten++;
                          }
                        }
                        xferFile.flush();
                      }
                      buffer.clear(); //clear the input buffer to get ready for next pack
                    }
                    else
                      addLog("Bad CRC (lower byte)");
                  }
                  else
                    addLog("Bad CRC (upper byte)");
                }
                else
                  addLog("Wrong inverse packet number");
              }
              else
                addLog("Wrong packet number");

              if (goodPacket)
              {
                addLog("\nGood packet, sending ACK");
                packetNumber++; //expect the next packet
                SerialWriteLog(0x06, "ACK after good packet"); //ACK
                if (zeroPacket)
                {
                  SerialWriteLog(0x43, "send C after first ACK"); //'C'  //also send a "C" after the first ACK so actual transfer begins
                  zeroPacket = false;
                  //expect the data packets should come as 1K
                  blockSize = 1024;
                  packetSize = 1029;
                }
              }              
            }
          }
        }
      }
    }
  }
}

// Playback functions moved to src/modules/playback.cpp


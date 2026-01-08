#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <Arduino.h>
#include "SD.h"

// ============================================================================
// File transfer protocol functions (XModem, YModem, ZModem)
// ============================================================================

// Raw transfer
void sendFileRaw();
void receiveFileRaw();

// XModem
void sendFileXMODEM();
void receiveFileXMODEM();
void sendLoopXMODEM(File dataFile, String fileName, String fileSize);
void receiveLoopXMODEM();
void sendPacket(File dataFile);

// YModem
void sendFileYMODEM();
void receiveFileYMODEM();
void sendLoopYMODEM(File dataFile, String fileName, String fileSize);
void receiveLoopYMODEM();
void sendZeroPacket(String fileName, String fileSize);
void sendFinalPacket();

// ZModem main functions
void sendFileZMODEM();
void receiveFileZMODEM();

// ============================================================================
// ZModem helper functions
// ============================================================================

// CRC functions
uint16_t zmCRC16Update(uint16_t crc, uint8_t b);
uint16_t zmCRC16(uint8_t *data, size_t len);
uint32_t zmCRC32Update(uint32_t crc, uint8_t b);
uint32_t zmCRC32(uint8_t *data, size_t len);
uint32_t zmCalcCRC32(uint8_t *buf, int len);

// Debug logging
void zmOpenDebugLog();
void zmCloseDebugLog();
void zmDebugLog(const char *fmt, ...);
void zmDebugHex(const char *prefix, uint8_t *data, size_t len);

// Low-level I/O
bool zmNeedsEscape(uint8_t b);
void zmSendRaw(uint8_t b);
void zmSendEsc(uint8_t b);
void zmTx(uint8_t b);
void zmSendZDLE(uint8_t c);
int zmRecvByte(unsigned long tout);
int zmRecvZDLE(unsigned long tout);

// Header send functions
void zmSendHexHdr(uint8_t type, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3);
void zmSendBin16Hdr(uint8_t type, uint32_t pos);
void zmSendBin32Hdr(uint8_t type, uint32_t pos);
void zmSendBinHeader(uint8_t type, uint32_t pos);
void zmSendBinHdr(uint8_t type, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3);

// Header receive functions
int zmRecvHexHdr(uint8_t *hdr, unsigned long tout);
int zmRecvBin16Hdr(uint8_t *hdr, unsigned long tout);
int zmRecvBin32Hdr(uint8_t *hdr, unsigned long tout);
int zmRecvHeader(unsigned long tout);
uint32_t zmGetPos(uint8_t *h);

// Data subpacket functions
void zmSendDataSubpacket(uint8_t *data, size_t len, uint8_t frameEnd);
int zmRecvDataSubpacket(uint8_t *buf, size_t maxLen, uint8_t *frameEnd);

// Protocol helper functions
void zmInit(bool debugEnabled);
void zmSendZRINIT();
void zmSendZRQINIT();
void zmSendZRPOS(uint32_t pos);
void zmSendZACK(uint32_t pos);
void zmSendZNAK();
void zmSendZSKIP();
void zmSendZFIN();
void zmSendZABORT();
void zmSendZEOF(uint32_t pos);
void zmSendZFILE(const char *filename, uint32_t fileSize, uint32_t fileTime);
void zmSendZDATA(uint32_t pos);
void zmParseZRINIT();
bool zmParseZFILE(uint8_t *data, int len, char *filename, uint32_t *fileSize, uint32_t *fileTime);
void zmSendCancel();
bool zmCheckCancel();

// ============================================================================
// Common utilities
// ============================================================================
void sendTelnet(uint8_t* p, int len);
bool checkCancel();
void resetGlobals();
void clearInputBuffer();
void SerialWriteLog(char c, String log);
void addLog(String s);

#endif // FILE_TRANSFER_H

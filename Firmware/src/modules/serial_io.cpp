#include "serial_io.h"
#include "diagnostics.h"
#include <HardwareSerial.h>
#include <WiFiClient.h>

// Global variables (defined in main.cpp)
extern HardwareSerial PhysicalSerial;
extern WiFiClient consoleClient;
extern bool consoleConnected;

// Binary mode flag - when true, text output is suppressed on PhysicalSerial
// Used during PPP/SLIP modes where the RS232 port carries binary protocol data
bool binaryModeActive = false;

void setBinaryMode(bool active) {
  binaryModeActive = active;
}

// Helper to check if console is available for output
static inline bool consoleReady() {
  return consoleConnected && consoleClient.connected();
}

// Helper to check if we should output to PhysicalSerial
static inline bool physicalSerialReady() {
  return !binaryModeActive;
}

void SerialPrintLn(String s) {
  Serial.println(s);
  if (physicalSerialReady()) PhysicalSerial.println(s);
  if (consoleReady()) consoleClient.println(s);
}

void SerialPrintLn(char c, int format) {
  Serial.println(c, format);
  if (physicalSerialReady()) PhysicalSerial.println(c, format);
  if (consoleReady()) consoleClient.println(c, format);
}

void SerialPrintLn(char c) {
  Serial.println(c);
  if (physicalSerialReady()) PhysicalSerial.println(c);
  if (consoleReady()) consoleClient.println(c);
}

void SerialPrint(String s) {
  Serial.print(s);
  if (physicalSerialReady()) PhysicalSerial.print(s);
  if (consoleReady()) consoleClient.print(s);
}

void SerialPrint(char c) {
  Serial.print(c);
  if (physicalSerialReady()) PhysicalSerial.print(c);
  if (consoleReady()) consoleClient.print(c);
}

void SerialPrint(char c, int format) {
  Serial.print(c, format);
  if (physicalSerialReady()) PhysicalSerial.print(c, format);
  if (consoleReady()) consoleClient.print(c, format);
}

void SerialPrintLn() {
  Serial.println();
  if (physicalSerialReady()) PhysicalSerial.println();
  if (consoleReady()) consoleClient.println();
}

void SerialPrintLn(unsigned char n, int base) {
  Serial.println(n, base);
  if (physicalSerialReady()) PhysicalSerial.println(n, base);
  if (consoleReady()) consoleClient.println(n, base);
}

void SerialPrintLn(int n) {
  Serial.println(n);
  if (physicalSerialReady()) PhysicalSerial.println(n);
  if (consoleReady()) consoleClient.println(n);
}

void SerialPrintLn(int n, int base) {
  Serial.println(n, base);
  if (physicalSerialReady()) PhysicalSerial.println(n, base);
  if (consoleReady()) consoleClient.println(n, base);
}

void SerialPrintLn(unsigned int n) {
  Serial.println(n);
  if (physicalSerialReady()) PhysicalSerial.println(n);
  if (consoleReady()) consoleClient.println(n);
}

void SerialPrintLn(unsigned int n, int base) {
  Serial.println(n, base);
  if (physicalSerialReady()) PhysicalSerial.println(n, base);
  if (consoleReady()) consoleClient.println(n, base);
}

void SerialPrintLn(long n) {
  Serial.println(n);
  if (physicalSerialReady()) PhysicalSerial.println(n);
  if (consoleReady()) consoleClient.println(n);
}

void SerialPrintLn(long n, int base) {
  Serial.println(n, base);
  if (physicalSerialReady()) PhysicalSerial.println(n, base);
  if (consoleReady()) consoleClient.println(n, base);
}

void SerialPrintLn(unsigned long n) {
  Serial.println(n);
  if (physicalSerialReady()) PhysicalSerial.println(n);
  if (consoleReady()) consoleClient.println(n);
}

void SerialPrintLn(unsigned long n, int base) {
  Serial.println(n, base);
  if (physicalSerialReady()) PhysicalSerial.println(n, base);
  if (consoleReady()) consoleClient.println(n, base);
}

void SerialPrint(unsigned char n, int base) {
  Serial.print(n, base);
  if (physicalSerialReady()) PhysicalSerial.print(n, base);
  if (consoleReady()) consoleClient.print(n, base);
}

void SerialPrint(int n) {
  Serial.print(n);
  if (physicalSerialReady()) PhysicalSerial.print(n);
  if (consoleReady()) consoleClient.print(n);
}

void SerialPrint(int n, int base) {
  Serial.print(n, base);
  if (physicalSerialReady()) PhysicalSerial.print(n, base);
  if (consoleReady()) consoleClient.print(n, base);
}

void SerialPrint(unsigned int n) {
  Serial.print(n);
  if (physicalSerialReady()) PhysicalSerial.print(n);
  if (consoleReady()) consoleClient.print(n);
}

void SerialPrint(unsigned int n, int base) {
  Serial.print(n, base);
  if (physicalSerialReady()) PhysicalSerial.print(n, base);
  if (consoleReady()) consoleClient.print(n, base);
}

void SerialPrint(long n) {
  Serial.print(n);
  if (physicalSerialReady()) PhysicalSerial.print(n);
  if (consoleReady()) consoleClient.print(n);
}

void SerialPrint(long n, int base) {
  Serial.print(n, base);
  if (physicalSerialReady()) PhysicalSerial.print(n, base);
  if (consoleReady()) consoleClient.print(n, base);
}

void SerialPrint(unsigned long n) {
  Serial.print(n);
  if (physicalSerialReady()) PhysicalSerial.print(n);
  if (consoleReady()) consoleClient.print(n);
}

void SerialPrint(unsigned long n, int base) {
  Serial.print(n, base);
  if (physicalSerialReady()) PhysicalSerial.print(n, base);
  if (consoleReady()) consoleClient.print(n, base);
}

void SerialFlush() {
  Serial.flush();
  if (physicalSerialReady()) PhysicalSerial.flush();
  if (consoleReady()) consoleClient.flush();
}

int SerialAvailable() {
  int c = Serial.available();
  if (c == 0)
    c = PhysicalSerial.available();
  if (c == 0 && consoleReady())
    c = consoleClient.available();
  return c;
}

int SerialRead() {
  // Priority: USB Serial > Physical Serial > Console
  int c = Serial.available();
  if (c > 0) {
    int byte = Serial.read();
    hexDumpByte('R', (uint8_t)byte);
    return byte;
  }
  c = PhysicalSerial.available();
  if (c > 0) {
    int byte = PhysicalSerial.read();
    hexDumpByte('R', (uint8_t)byte);
    return byte;
  }
  if (consoleReady() && consoleClient.available()) {
    int byte = consoleClient.read();
    hexDumpByte('R', (uint8_t)byte);
    return byte;
  }
  return -1;
}

void SerialWrite(int c) {
  hexDumpByte('T', (uint8_t)c);
  Serial.write(c);
  if (physicalSerialReady()) PhysicalSerial.write(c);
  if (consoleReady()) consoleClient.write(c);
}

// Read from console only (for telnet IAC handling)
int ConsoleAvailable() {
  if (consoleReady())
    return consoleClient.available();
  return 0;
}

int ConsoleRead() {
  if (consoleReady() && consoleClient.available())
    return consoleClient.read();
  return -1;
}

void ConsolePrint(String s) {
  if (consoleReady()) {
    consoleClient.print(s);
  }
}

void ConsolePrintLn(String s) {
  if (consoleReady()) {
    consoleClient.println(s);
  }
}

// Get timestamp in milliseconds as formatted string
String getTimestamp() {
  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  unsigned long min = sec / 60;
  unsigned long hrs = min / 60;

  sec = sec % 60;
  min = min % 60;
  ms = ms % 1000;

  char timestamp[16];
  sprintf(timestamp, "[%02lu:%02lu:%02lu.%03lu] ", hrs, min, sec, ms);
  return String(timestamp);
}

// USB Debug print with timestamp (only to USB Serial, not PhysicalSerial)
void UsbDebugPrint(String s) {
  Serial.print(getTimestamp());
  Serial.print(s);
}

// USB Debug println with timestamp (only to USB Serial, not PhysicalSerial)
void UsbDebugPrintLn(String s) {
  Serial.print(getTimestamp());
  Serial.println(s);
}

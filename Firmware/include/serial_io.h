#ifndef SERIAL_IO_H
#define SERIAL_IO_H

#include <Arduino.h>

// Serial communication wrapper functions
// These functions write to Serial (USB), PhysicalSerial (UART2), and console (telnet) simultaneously
// When binary mode is active (PPP/SLIP), text output is suppressed on PhysicalSerial

// Binary mode control - when active, SerialPrint* only outputs to USB Serial
extern bool binaryModeActive;
void setBinaryMode(bool active);

void SerialPrintLn(String s);
void SerialPrintLn(char c, int format);
void SerialPrintLn(char c);
void SerialPrintLn();
void SerialPrintLn(unsigned char n, int base);
void SerialPrintLn(int n, int base);
void SerialPrintLn(unsigned int n, int base);
void SerialPrintLn(long n, int base);
void SerialPrintLn(unsigned long n, int base);
void SerialPrint(String s);
void SerialPrint(char c);
void SerialPrint(char c, int format);
void SerialPrint(unsigned char n, int base);
void SerialPrint(int n, int base);
void SerialPrint(unsigned int n, int base);
void SerialPrint(long n, int base);
void SerialPrint(unsigned long n, int base);
void SerialFlush();
int SerialAvailable();
int SerialRead();
void SerialWrite(int c);

// Console-specific functions (for telnet IAC handling)
int ConsoleAvailable();
int ConsoleRead();
void ConsolePrint(String s);
void ConsolePrintLn(String s);

// USB Debug functions with timestamps
// These only output to USB Serial (not PhysicalSerial) and include timestamps
void UsbDebugPrint(String s);
void UsbDebugPrintLn(String s);
String getTimestamp();

#endif // SERIAL_IO_H

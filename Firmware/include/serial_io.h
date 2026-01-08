#ifndef SERIAL_IO_H
#define SERIAL_IO_H

#include <Arduino.h>

// Serial communication wrapper functions
// These functions write to both Serial (USB) and PhysicalSerial (UART2) simultaneously

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

#endif // SERIAL_IO_H

#include "serial_io.h"
#include <HardwareSerial.h>

// Global variable (defined in main.cpp)
extern HardwareSerial PhysicalSerial;

void SerialPrintLn(String s) {
  Serial.println(s);
  PhysicalSerial.println(s);
}

void SerialPrintLn(char c, int format) {
  Serial.println(c, format);
  PhysicalSerial.println(c, format);
}

void SerialPrintLn(char c) {
  Serial.println(c);
  PhysicalSerial.println(c);
}

void SerialPrint(String s) {
  Serial.print(s);
  PhysicalSerial.print(s);
}

void SerialPrint(char c) {
  Serial.print(c);
  PhysicalSerial.print(c);
}

void SerialPrint(char c, int format) {
  Serial.print(c, format);
  PhysicalSerial.print(c, format);
}

void SerialPrintLn() {
  Serial.println();
  PhysicalSerial.println();
}

void SerialPrintLn(unsigned char n, int base) {
  Serial.println(n, base);
  PhysicalSerial.println(n, base);
}

void SerialPrintLn(int n, int base) {
  Serial.println(n, base);
  PhysicalSerial.println(n, base);
}

void SerialPrintLn(unsigned int n, int base) {
  Serial.println(n, base);
  PhysicalSerial.println(n, base);
}

void SerialPrintLn(long n, int base) {
  Serial.println(n, base);
  PhysicalSerial.println(n, base);
}

void SerialPrintLn(unsigned long n, int base) {
  Serial.println(n, base);
  PhysicalSerial.println(n, base);
}

void SerialPrint(unsigned char n, int base) {
  Serial.print(n, base);
  PhysicalSerial.print(n, base);
}

void SerialPrint(int n, int base) {
  Serial.print(n, base);
  PhysicalSerial.print(n, base);
}

void SerialPrint(unsigned int n, int base) {
  Serial.print(n, base);
  PhysicalSerial.print(n, base);
}

void SerialPrint(long n, int base) {
  Serial.print(n, base);
  PhysicalSerial.print(n, base);
}

void SerialPrint(unsigned long n, int base) {
  Serial.print(n, base);
  PhysicalSerial.print(n, base);
}

void SerialFlush() {
  Serial.flush();
  PhysicalSerial.flush();
}

int SerialAvailable() {
  int c = Serial.available();
  if (c==0)
    c = PhysicalSerial.available();
  return c;
}

int SerialRead() {
  int c = Serial.available();
  if (c>0)
    return Serial.read();
  else
    return PhysicalSerial.read();
}

void SerialWrite(int c) {
  Serial.write(c);
  PhysicalSerial.write(c);
}

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// EEPROM and settings management functions
void writeSettings();
void readSettings();
void defaultEEPROM();
String getEEPROM(int startAddress, int len);
void setEEPROM(String inString, int startAddress, int maxLen);

#endif // SETTINGS_H

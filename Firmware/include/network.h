#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <WiFi.h>

// WiFi and network management functions
void connectWiFi();
void disconnectWiFi();
void displayNetworkStatus();
String ipToString(IPAddress ip);
void showWifiIcon();
void setBaudRate(int inSpeed);
void setCarrier(byte carrier);
void showCallIcon();

#endif // NETWORK_H

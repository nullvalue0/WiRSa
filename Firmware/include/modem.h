#ifndef MODEM_H
#define MODEM_H

#include <Arduino.h>

// Modem and AT command functions
void sendResult(int resultCode);
void sendString(String msg);
void command();
void dialOut(String upCmd);
void hangUp();
void handleIncomingConnection();
void handleFlowControl();
void enterModemMode();
void mainLoop();
void modemLoop();
void settingsLoop();
void baudLoop();
void serialLoop();
void orientationLoop();
void defaultModeLoop();
void fileLoop();
void playbackLoop();
void listFilesLoop();
void protocolLoop();
int checkButton();
void displayHelp();
void displayCurrentSettings();
void displayStoredSettings();
void waitForSpace();
void waitForEnter();
void storeSpeedDial(byte num, String location);
void handleWebHangUp();
void handleRoot();
String connectTimeString();
bool refreshDisplay(void*);
String prompt(String msg, String dflt);
void clearInputBuffer();
String getLine();

#endif // MODEM_H

#ifndef DISPLAY_MENU_H
#define DISPLAY_MENU_H

#include <Arduino.h>

// Display and menu system functions
void showMessage(String message);
void updateXferMessage();
void showMenu(String menuName, String options[], int sz, int dispMode, int defaultSel);
void menuOption(int idx, String item);
bool showHeader(String menu, int dispMode);
void mainMenu(bool arrow);
void settingsMenu(bool arrow);
void baudMenu(bool arrow);
void serialMenu(bool arrow);
void orientationMenu(bool arrow);
void defaultModeMenu(bool arrow);
void fileMenu(bool arrow);
void playbackMenu(bool arrow);
void listFilesMenu(bool arrow);
void protocolMenu(bool arrow);
void modemMenuMsg();
void modemConnected();
void modemMenu();
void readSwitches();
void readBackSwitch();
void waitSwitches();
void waitBackSwitch();
String padLeft(String s, int len);
String padRight(String s, int len);

#endif // DISPLAY_MENU_H

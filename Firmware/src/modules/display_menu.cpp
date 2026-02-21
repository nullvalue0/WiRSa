// Display and Menu System Module
// Handles all display rendering and menu navigation

#include "Arduino.h"
#include "display_menu.h"
#include "globals.h"
#include "serial_io.h"
#include "network.h"
#include <Adafruit_SSD1306.h>
#include <SD.h>
#include <WiFi.h>
#include <arduino-timer.h>

// External global variables
extern Adafruit_SSD1306 display;
extern String baudDisp[];
extern String bitsDisp[];
extern String mainMenuDisp[];
extern String fileMenuDisp[];
extern String playbackMenuDisp[];
extern String settingsMenuDisp[];
extern String orientationMenuDisp[];
extern String defaultModeDisp[];
extern String protocolMenuDisp[];
extern String files[];
extern String speedDials[];
extern String xferMsg;
extern int menuMode;
extern int menuIdx;
extern int menuCnt;
extern int lastMenu;
extern byte serialSpeed;
extern byte serialConfig;
extern byte dispOrientation;
extern byte defaultMode;
extern bool msgFlag;
extern unsigned long xferBytes;
extern unsigned int xferBlock;
extern bool speedDialShown;
extern Timer<16> modem_timer;
extern long bytesSent;
extern long bytesRecv;
extern int tcpServerPort;
extern int dispCharCnt;
extern int dispLineCnt;

// WiFi and display graphics
extern const unsigned char wifi_symbol[];
extern const unsigned char phon_symbol[];
extern bool callConnected;

// Menu mode constants and display modes are #defined in globals.h
// (MODE_MAIN, MODE_MODEM, etc. and MENU_BOTH, MENU_NUM, MENU_DISP)

String padLeft(String s, int len) {
  while (s.length()<len)
  {
    s = " " + s;
  }
  return s;
}

String padRight(String s, int len) {
  while (s.length()<len)
  {
    s = s + " ";
  }
  return s;
}

void mainMenu(bool arrow) {
  menuMode=MODE_MAIN;
  showMenu("MAIN", mainMenuDisp, 7, (arrow?MENU_DISP:MENU_BOTH), 0);
}

//returns true/false depending on if default menu item should be set
bool showHeader(String menu, int dispMode) {
  bool setDef = false;

  if (dispMode!=MENU_DISP) {
    SerialPrintLn();
    SerialPrintLn();
    SerialPrintLn("-=-=- " + menu + " MENU -=-=-");
  }

  //21 chars wide x 8 lines);
  if (lastMenu != menuMode || msgFlag)
  { //clear the whole display, reset the selction
    msgFlag=false;
    display.clearDisplay();
    menuIdx=0;
    setDef=true;
    lastMenu=menuMode;
    display.drawLine(0,0,0,63,SSD1306_WHITE);
    display.drawLine(127,0,127,63,SSD1306_WHITE);
    display.drawLine(0,63,127,63,SSD1306_WHITE);
    display.drawLine(0,16,127,16,SSD1306_WHITE);
  } else {
    //just clear the selection area
    display.fillRect(6, 17, 8, 46, SSD1306_BLACK);
  }

  //display.fillRect(0, 0, 127, 9, SSD1306_WHITE);
  display.fillRect(0, 0, 127, 16, SSD1306_WHITE); //fill in the yellow header area

  display.drawLine(1, 1, 126, 1, SSD1306_BLACK);  //add an outline to fill this
  display.drawLine(1, 14, 126, 14, SSD1306_BLACK); //somewhat annoyingly large area
  display.drawLine(1, 1, 1, 14, SSD1306_BLACK);
  display.drawLine(126, 1, 126, 14, SSD1306_BLACK);


  //display.drawLine(0,0,0,63,SSD1306_WHITE);
    //display.setFont(&FreeMono9pt7b);
  display.setCursor(4, 4);     // Start at top-left corner
  display.setTextColor(SSD1306_BLACK); // Draw white text
  String line = "WiRSa " + menu;
  display.print(line);
  String bps = baudDisp[serialSpeed];

  display.print(padLeft(bps,20-line.length()));
  display.setTextColor(SSD1306_WHITE); // Draw white text
  //display.setCursor(0, 13);     // Start at top-left corner
  display.setCursor(0, 17);     // Start at top-left corner
  //display.setFont();
  display.display();

  return setDef;
}

void showMessage(String message) {
  //display.fillRect(1, 10, 125, 53, SSD1306_BLACK);
  //display.drawRect(15, 12, 97, 40, SSD1306_WHITE);
  //display.fillRect(17, 14, 93, 36, SSD1306_WHITE);
  display.fillRect(1, 17, 125, 44, SSD1306_BLACK);
  display.drawRect(15, 18, 97, 43, SSD1306_WHITE);
  display.fillRect(17, 20, 93, 39, SSD1306_WHITE);

  int x = 19;
  //int y = 16;
  int y = 21;
  int c= 0;
  display.setCursor(x,y);
  display.setTextColor(SSD1306_BLACK);

  for (int i = 0; i < message.length(); i++) {
    if (message[i] == '\n') {
      x = 19; y += 8; c = 0;
      display.setCursor(x, y);
    } else if (c == 15) {
      x = 19; y += 8; c = 0;
      display.setCursor(x, y);
      display.write(message[i]);
    } else {
      display.write(message[i]);
      c++;
    }
  }
  display.setTextColor(SSD1306_WHITE);
  display.display();
  msgFlag=true;
}

void updateXferMessage() {
  xferBytes++;
  xferBlock++;
  if (xferBytes==0 || xferBlock==1024) {
    xferBlock=0;
    showMessage(xferMsg + String(xferBytes));
  }
}

void showMenu(String menuName, String options[], int sz, int dispMode, int defaultSel) {
  if (showHeader(menuName, dispMode))
    menuIdx = defaultSel;

  menuCnt = sz;

  int menuStart=0;
  int menuEnd=0;

  int maxItems=5; //can only show 5 menu items per page

  if (menuCnt<=maxItems)
    menuEnd=menuCnt;
  else {
    //clear out menu area
    display.fillRect(1, 17, 126, 46, SSD1306_BLACK);

    menuStart=menuIdx;
    if (menuIdx<maxItems)
      menuStart=0;
    else
      menuStart=menuIdx-(maxItems-1);

    menuEnd=menuStart+maxItems;
    if (menuEnd>menuCnt)
      menuEnd=menuCnt;
  }

  display.setCursor(1, 19);

  for (int i=menuStart;i<menuEnd;i++) {
    menuOption(i, options[i]);
  }

  if (dispMode!=MENU_DISP) {
    int c = 0;
    for (int i=0;i<menuCnt;i++) {
      if (dispMode==MENU_NUM)
      {
        if (c==2) {
          SerialPrintLn(" [" + String((char)(65+i)) + "] " + options[i]);
          c=0;
        }
        else {
          SerialPrint(" [" + String((char)(65+i)) + "] " + padRight(options[i], 15));
          c++;
        }
      }
      else //MENU_BOTH
        SerialPrintLn(" [" + options[i].substring(0,1) + "]" + options[i].substring(1));
    }
  }

  if (menuStart>0) {
    display.setCursor(120, 19);
    display.write(0x1E);
  }

  if (menuEnd<menuCnt) {
    display.setCursor(120, 54);
    display.write(0x1F);
  }

  display.display();

  if (dispMode!=MENU_DISP) {
    SerialPrintLn();
    SerialPrint("> ");
  }
}

void menuOption(int idx, String item) {
  display.print(" ");
  if (menuIdx==idx)
    display.write(16);
  else
    display.print(" ");
  display.print(" ");
  display.println(item.substring(0,17));
}

void playbackMenu(bool arrow) {
  menuMode = MODE_PLAYBACK;
  showMenu("PLAYBACK", playbackMenuDisp, 6, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void settingsMenu(bool arrow) {
  menuMode = MODE_SETTINGS;
  showMenu("CONFIG", settingsMenuDisp, 10, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void orientationMenu(bool arrow) {
  menuMode = MODE_ORIENTATION;
  showMenu("DISPLAY", orientationMenuDisp, 2, (arrow?MENU_DISP:MENU_BOTH), dispOrientation);
}

void defaultModeMenu(bool arrow) {
  menuMode = MODE_DEFAULTMODE;
  showMenu("DEFAULT", defaultModeDisp, 2, (arrow?MENU_DISP:MENU_NUM), defaultMode);
}

void protocolMenu(bool arrow) {
  menuMode = MODE_PROTOCOL;
  showMenu("PROTOCOL", protocolMenuDisp, 6, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void baudMenu(bool arrow) {
  menuMode = MODE_SETBAUD;
  showMenu("BAUD RATE", baudDisp, 9, (arrow?MENU_DISP:MENU_NUM), serialSpeed);
}

void serialMenu(bool arrow) {
  menuMode = MODE_SERIALCONFIG;
  showMenu("SERIAL", bitsDisp, 24, (arrow?MENU_DISP:MENU_NUM), serialConfig);
}

void fileMenu(bool arrow) {
  menuMode = MODE_FILE;
  showMenu("FILE XFER", fileMenuDisp, 4, (arrow?MENU_DISP:MENU_BOTH), 0);
}

void listFilesMenu(bool arrow) {
  menuMode = MODE_LISTFILE;
  int c=0;
  File root = SD.open("/");
  while(true) {
    File entry =  root.openNextFile();
    if (! entry) {
     // no more files
      break;
    }
    if (!entry.isDirectory()) {
      if (c<100)
      {
        String fn = entry.name();
        files[c]=fn;
        c++;
      }
      //SerialPrintLn(entry.size(), DEC);
    }
    entry.close();
  }
  root.close();

  showMenu("FILE LIST", files, c, (arrow?MENU_DISP:MENU_NUM), 0);
}

void modemMenu() {
  modem_timer.cancel();
  menuMode = MODE_MODEM;
  speedDialShown=true;
  showMenu("DIAL LIST", speedDials, 10, MENU_DISP, 0);
}

void modemMenuMsg() {
  menuMode = MODE_MODEM;
  display.clearDisplay();
  display.setCursor(1, 1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Modem mode\nUp/Down show menu");
  display.display();
  dispCharCnt=21*2;
  dispLineCnt=0;
}

void modemConnected() {
  showMenu("MODEM", {}, 0, MENU_DISP, 0);
  showWifiIcon();
  showCallIcon();
  display.setTextWrap(false);

  if (signalMonitorEnabled) {
    // Signal monitor overlay - show pin states instead of IP/bytes
    display.setCursor(3, 19);
    display.print("DCD:");
    display.print(callConnected ? "HI" : "LO");
    display.setCursor(64, 19);
    display.print("RTS:");
    display.print(digitalRead(RTS_PIN) ? "HI" : "LO");

    display.setCursor(3, 29);
    display.print("CTS:");
    display.print(readCTS() ? "HI" : "LO");
    display.setCursor(64, 29);
    display.print("DTR:");
    display.print(readDTR() ? "HI" : "LO");

    display.setCursor(3, 39);
    display.print("DSR:");
    display.print(callConnected ? "HI" : "LO");
    display.setCursor(64, 39);
    display.print("RI :");
    display.print("LO");

    display.drawLine(0, 50, 127, 50, SSD1306_WHITE);
    display.setCursor(3, 53);
    display.print("SIG MON");
  } else {
    display.setCursor(3, 19);
    display.print("WIFI: ");
    display.print(WiFi.SSID());

    display.setCursor(3, 29);
    //display.print("IP: ");
    display.print(ipToString(WiFi.localIP()));

    if (tcpServerPort > 0) {
      display.print(":");
      display.print(tcpServerPort);
    }

    display.drawLine(0,40,127,40,SSD1306_WHITE);

    display.setCursor(3, 43);
    display.print("SENT: ");
    display.print(String(bytesSent));
    display.print(" B");

    display.setCursor(3, 53);
    display.print("RECV: ");
    display.print(String(bytesRecv));
    display.print(" B");
  }

  display.display();
  display.setTextWrap(true);
}

void readSwitches()
{ // These digital pins are pulled HIGH, pushed=LOW
  // Restored from original WiRSa.ino for proper button debouncing
  BTNUP = !digitalRead(SW1_PIN);
  BTNBK = !digitalRead(SW2_PIN);
  BTNEN = !digitalRead(SW3_PIN);
  BTNDN = !digitalRead(SW4_PIN);
}

void readBackSwitch()
{ // Only read BACK button when in active connection (modem mode online)
  // Prevents interference with connection. All other buttons are disabled.
  BTNDN = LOW;
  BTNBK = !digitalRead(SW2_PIN);
  BTNEN = LOW;
  BTNUP = LOW;
}

void waitSwitches()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(BTNDN||BTNBK||BTNEN||BTNUP) {
      delay(10);
      readSwitches();
    }
  }
}

void waitBackSwitch()
{
  //wait for all switches to be released
  for (int i=0;i<10;i++) {
    while(BTNDN||BTNBK||BTNEN||BTNUP) {
      delay(10);
      readBackSwitch();
    }
  }
}

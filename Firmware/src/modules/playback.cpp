// Playback Module
// Handles file playback and terminal mode functions

#include "Arduino.h"
#include "playback.h"
#include "globals.h"
#include "serial_io.h"
#include "display_menu.h"
#include "file_transfer.h"
#include <SD.h>
#include <WiFi.h>

// External global variables
extern int menuIdx;
extern int menuCnt;
extern String fileName;
extern String terminalMode;
extern long bytesSent;
extern long bytesRecv;
extern bool sentChanged;
extern bool recvChanged;
extern bool callConnected;

// External functions from modem.h (not yet extracted)
extern String getLine();

void playbackLoop()
{
  int menuSel=-1;
  readSwitches();
  if (BTNUP) {  //UP
      menuIdx--;
      if (menuIdx<0)
        menuIdx=menuCnt-1;
      playbackMenu(true);
  }
  else if (BTNDN) { //DOWN
    menuIdx++;
    if (menuIdx>(menuCnt-1))
      menuIdx=0;
    playbackMenu(true);
  }
  else if (BTNEN) { //ENTER
    menuSel = menuIdx;
  }
  else if (BTNBK) { //BACK
    mainMenu(false);
  }
  waitSwitches();

  if (SerialAvailable() || menuSel>-1)
  {
    char chr = SerialRead();
    SerialPrintLn(chr);

    if (chr == 'L' || chr == 'l') {
      listFiles();
    } else if (chr == 'T' || chr == 't') {
      changeTerminalMode();
    } else if (chr == 'E' || chr == 'e') {
      evalKey();
    } else if (chr == 'D' || chr == 'd') {
      SerialPrint("Enter Filename: ");
      String fileName = getLine();
      if (fileName!="") {
        if (displayFile(fileName, false, 0))
          waitKey(27,96);
        SerialPrintLn("");
        playbackMenu(false);
      }
    } else if (chr == 'P' || chr == 'p') {
      SerialPrint("Enter Filename: ");
      fileName = getLine();
      if (fileName!="") {
        SerialPrintLn("");
        SerialPrint("Begin at Position: ");
        String pos = getLine();
        if (displayFile(fileName, true, pos.toInt()))
          waitKey(27,96);
        SerialPrintLn("");
        playbackMenu(false);
      }
    } else if (chr=='M'||chr=='m'||menuSel==0) {
      mainMenu(false);
    } else {
      playbackMenu(false);
    }
  }
}


void listFiles()
{
   SerialPrintLn("");
   SerialPrintLn("listing files on SD card..");
   SerialPrintLn("");
   File root = SD.open("/");
   SerialPrintLn("FILENAME            SIZE");
   SerialPrintLn("--------            ----");
   while(true) {
     File entry =  root.openNextFile();
     if (! entry) {
       // no more files
       break;
     }
    if (!entry.isDirectory()) {
        String fn = entry.name();
        SerialPrint(fn);
        for(int i=0; i<20-fn.length(); i++)
        {
           SerialPrint(" ");
        }
        SerialPrintLn(entry.size(), DEC);
        //SerialPrintLn(String(entry.size());
    }
    entry.close();
  }
  root.close();

  SerialPrintLn("");
  uint8_t cardType = SD.cardType();
  Serial.print("SD Card Type: ");
  if (cardType == CARD_NONE) {
   Serial.println("No SD card attached");
  } else if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  SerialPrintLn("");

  SerialPrint("> ");
}

void displayChar(char chr, int dir) {
  if (WiFi.status() == WL_CONNECTED && callConnected==true) {
    if (dir==XFER_SEND) {
      bytesSent++;
      sentChanged=true;
    }
    if (dir==XFER_RECV) {
      bytesRecv++;
      recvChanged=true;
    }
  }
}

void changeTerminalMode()
{
  if (terminalMode=="VT100")
    terminalMode="Viewpoint";
  else if (terminalMode=="Viewpoint")
    terminalMode="VT100";

  SerialPrintLn("Terminal Mode set to: " + terminalMode);
}

void clearScreen()
{
  if (terminalMode=="VT100")
  {
    SerialPrint((char)27);
    SerialPrint("[2J");  //clear screen
    SerialPrint((char)27);
    SerialPrint("[1;1H"); //cursor first row/col
  }
  else if (terminalMode=="Viewpoint")
  {
    SerialPrint((char)26); //clear screen
    SerialPrint((char)27);
    SerialPrint("="); //cursor first row/col
    SerialPrint((char)1);
    SerialPrint((char)1);
  }
}

bool displayFile(String filename, bool playback_mode, int begin_position)
{
  File myFile = SD.open("/"+filename);
  clearScreen();
  int pos=0;
  if (myFile) {
    // read from the file until there's nothing else in it:
    while (myFile.available()) {
      if (playback_mode && pos>begin_position)
      {
        char c = getKey();
        if (c==27||c==96)
        {
          myFile.close();
          return false;
        }
        else if (c==9)
        {
          myFile.close();
          return displayFile(filename, playback_mode, begin_position);
        }
      }
      char chr = myFile.read();
      if (chr=='^') //VT escape code
        SerialPrint((char)27);
      else if (chr=='`') //Playback escape code
      {
        chr = myFile.read(); //get the next char
        if (chr=='P') //continue playback
          playback_mode=false;
        else if (chr=='W') //wait for any key (pause playback)
          playback_mode=true;
        else if (chr=='D') //delay 1 second
          delay(1000);
        else if (chr=='E') //wait for enter key
        {
          waitKey(10,13);
          playback_mode=false;
        }
      }
      else
        SerialWrite(chr);
      pos++;
    }
    // close the file:
    myFile.close();
    return true;
  } else {
    // if the file didn't open, print an error:
    SerialPrintLn("error opening '" + filename + "'");
    return false;
  }
}

void evalKey()
{
  SerialPrint("Press Key (repeat same key 3 times to exit): ");
  SerialPrintLn("\n");
  SerialPrintLn("DEC\tHEX\tCHR");
  char lastKey;
  int lastCnt=0;
  while(1) {
    char c = getKey();
    SerialPrint(c, DEC);
    SerialPrint("\t");
    SerialPrint(c, HEX);
    SerialPrint("\t");
    SerialPrintLn(c);
    if (c==lastKey) {
      lastCnt++;
      if (lastCnt==2)
        break;
    } else {
      lastCnt=0;
      lastKey=c;
    }
  }
  playbackMenu(false);
}

char getKey()
{
  while (true)
  {
    if (SerialAvailable() > 0)
      return SerialRead();
  }
}

void waitKey(int key1, int key2)
{
  while (true)
  {
    if (SerialAvailable() > 0)
    {
      char c = SerialRead();
      if (c == key1 || c == key2)
        break;
    }
  }
}

String getFileSize(String fileName)
{
   File root = SD.open("/");
   while(true) {
     File entry =  root.openNextFile();
     if (!entry) {
       // no more files
       break;
     }
     if (!entry.isDirectory()) {
         String fn = entry.name();
         if (fn==fileName)
         {
           String sz = String(entry.size());
           entry.close();
           root.close();
           return sz;
         }
     }
     entry.close();
   }
  root.close();
  return "0";
}

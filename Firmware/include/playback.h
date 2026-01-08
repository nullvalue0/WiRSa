#ifndef PLAYBACK_H
#define PLAYBACK_H

#include <Arduino.h>

// File playback and terminal mode functions
bool displayFile(String fileName, bool playback, int startPos);
void changeTerminalMode();
void evalKey();
char getKey();
void waitKey(int k1, int k2);
void displayChar(char c, int direction);
void listFiles();
String getFileSize(String fileName);

#endif // PLAYBACK_H

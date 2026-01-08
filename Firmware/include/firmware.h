#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <Arduino.h>

// Firmware update (OTA) functions
void firmwareCheck();
void firmwareUpdate();
void firmwareUpdateStarted();
void firmwareUpdateFinished();
void firmwareUpdateProgress(int cur, int total);
void firmwareUpdateError(int err);

#endif // FIRMWARE_H

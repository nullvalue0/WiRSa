// Firmware Update Module
// Handles OTA (Over-The-Air) firmware updates

#include "Arduino.h"
#include "firmware.h"
#include "globals.h"
#include "serial_io.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// External global variables
extern String build;
extern HardwareSerial PhysicalSerial;

void firmwareCheck() {
  //http://update.retrodisks.com/wirsa-v3.php
  //This works by checking the latest release tag on the hosted github page
  //I have to call the github API page at https://api.github.com/repos/nullvalue0/WiRSa/releases/latest
  //However the problem is this only works over SSL and returns a large result as JSON. Hitting SSL
  //pages from the ESP8266 is a lot of work, so to get around it a built a very simple PHP script that
  //I host on a plain HTTP site. It hits the github API endpoint over HTTPS, parses the JSON and just
  //returns the latest version string over HTTP as plain text - this way the ESP8266 can easily check
  //the latest version.

  SerialPrintLn("\nChecking firmware version..."); yield();
  WiFiClient client;

  HTTPClient http;
  if (http.begin(client, "http://update.retrodisks.com/wirsa-v3.php")) {
      int httpCode = http.GET();
      if (httpCode == 200) {
        String version = http.getString();
        SerialPrintLn("Latest Version: " + version + ", Device Version: " + build);
        if (build!=version)
          SerialPrintLn("WiRSa firmware update available, download the latest release at https://github.com/nullvalue0/WiRSa or use commmand ATFU to apply updates now.");
        else
          SerialPrintLn("Your WiRSa is running the latest firmware version.");
      }
      else
        SerialPrintLn("Firmware version check failed.");
  }

}

void firmwareUpdate() {
   WiFiClient client;

   httpUpdate.setLedPin(LED_PIN, LOW);
   httpUpdate.onStart(firmwareUpdateStarted);
   httpUpdate.onEnd(firmwareUpdateFinished);
   httpUpdate.onProgress(firmwareUpdateProgress);
   httpUpdate.onError(firmwareUpdateError);

   //t_httpUpdate_return ret = httpUpdate.update(client, "http://update.retrodisks.com/wirsa-bin-v3.php");
   t_httpUpdate_return ret = httpUpdate.update(client, "http://update.retrodisks.com/wirsa_v3.bin");
   switch (ret) {
     case HTTP_UPDATE_FAILED:
       Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
       break;

     case HTTP_UPDATE_NO_UPDATES:
       SerialPrintLn("HTTP_UPDATE_NO_UPDATES");
       break;

     case HTTP_UPDATE_OK:
       SerialPrintLn("HTTP_UPDATE_OK");
       break;
   }
 }

void firmwareUpdateStarted() {
  SerialPrintLn("HTTP update process started");
}

void firmwareUpdateFinished() {
  SerialPrintLn("HTTP update process finished...\r\n\r\nPLEASE WAIT, APPLYING UPDATE - DEVICE WILL REBOOT ON IT'S OWN WHEN COMPLETE IN ABOUT 10 SECONDS\r\n\r\n");
}

void firmwareUpdateProgress(int cur, int total) {
  Serial.printf("HTTP update process at %d of %d bytes...\r\n", cur, total);
  PhysicalSerial.printf("HTTP update process at %d of %d bytes...\r\n", cur, total);
}

void firmwareUpdateError(int err) {
  Serial.printf("HTTP update fatal error code %\r\n", err);
  PhysicalSerial.printf("HTTP update fatal error code %\r\n", err);
}

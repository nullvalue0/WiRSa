#ifndef WIRSA_WEB_UI_H
#define WIRSA_WEB_UI_H

#include <Arduino.h>

// Web server setup and route registration
void setupWebServer();

// Route handlers (internal - not meant to be called directly)
void handleWebRoot();
void handleWebHangUp();

// API handlers (internal - not meant to be called directly)
void handleApiStatus();
void handleApiSettingsGet();
void handleApiSettingsPost();
void handleApiReboot();
void handleApiFilesList();
void handleApiFilesGet();
void handleApiFilesDelete();
void handleApiUpload();
void handleApiUploadProcess();

// External PROGMEM HTML strings (defined in web_ui_html.cpp)
extern const char HTML_PAGE[] PROGMEM;

#endif // WIRSA_WEB_UI_H

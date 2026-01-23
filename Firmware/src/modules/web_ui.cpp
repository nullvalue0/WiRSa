#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "SD.h"
#include "web_ui.h"
#include "settings.h"

// External references to global objects and variables
extern WebServer webServer;
extern WiFiClient tcpClient;

// From main.cpp - settings variables
extern String build;
extern String ssid, password, busyMsg;
extern byte serialSpeed, serialConfig;
extern bool echo, autoAnswer, telnet, verboseResults, petTranslate;
extern int tcpServerPort;
extern byte flowControl, pinPolarity, dispOrientation, defaultMode;
extern String speedDials[10];
extern bool callConnected;
extern unsigned long connectTime;

// Serial configuration arrays
extern const long bauds[];
extern const int bits[];

// Helper function from modem.cpp
extern String connectTimeString();
extern String ipToString(IPAddress ip);

// File handle for uploads
static File uploadFile;

// Setup all web server routes
void setupWebServer() {
  // Main page (SPA)
  webServer.on("/", HTTP_GET, handleWebRoot);

  // Legacy endpoint for compatibility
  webServer.on("/ath", HTTP_GET, handleWebHangUp);

  // Status API
  webServer.on("/api/status", HTTP_GET, handleApiStatus);

  // Settings API
  webServer.on("/api/settings", HTTP_GET, handleApiSettingsGet);
  webServer.on("/api/settings", HTTP_POST, handleApiSettingsPost);

  // Reboot API
  webServer.on("/api/reboot", HTTP_POST, handleApiReboot);

  // Files API
  webServer.on("/api/files", HTTP_GET, handleApiFilesList);
  webServer.on("/api/files", HTTP_DELETE, handleApiFilesDelete);

  // File download (separate endpoint to handle filename parameter)
  webServer.on("/api/download", HTTP_GET, handleApiFilesGet);

  // File upload (multipart form handler)
  webServer.on("/api/upload", HTTP_POST, handleApiUpload, handleApiUploadProcess);
}

// Serve main SPA page
void handleWebRoot() {
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");
  webServer.sendContent_P(HTML_PAGE);
}

// Legacy hangup endpoint
void handleWebHangUp() {
  extern void hangUp();
  String t = "NO CARRIER (" + connectTimeString() + ")";
  hangUp();
  webServer.send(200, "text/plain", t);
}

// GET /api/status - Return device status as JSON
void handleApiStatus() {
  JsonDocument doc;

  // WiFi status
  String wifiStatus;
  switch (WiFi.status()) {
    case WL_CONNECTED: wifiStatus = "CONNECTED"; break;
    case WL_IDLE_STATUS: wifiStatus = "OFFLINE"; break;
    case WL_CONNECT_FAILED: wifiStatus = "CONNECT FAILED"; break;
    case WL_NO_SSID_AVAIL: wifiStatus = "SSID UNAVAILABLE"; break;
    case WL_CONNECTION_LOST: wifiStatus = "CONNECTION LOST"; break;
    case WL_DISCONNECTED: wifiStatus = "DISCONNECTED"; break;
    default: wifiStatus = "UNKNOWN"; break;
  }
  doc["wifiStatus"] = wifiStatus;
  doc["ssid"] = WiFi.SSID();

  // MAC address
  byte mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  doc["mac"] = macStr;

  // IP info
  doc["ip"] = ipToString(WiFi.localIP());
  doc["gateway"] = ipToString(WiFi.gatewayIP());
  doc["subnet"] = ipToString(WiFi.subnetMask());
  doc["rssi"] = WiFi.RSSI();

  // Server info
  doc["serverPort"] = tcpServerPort;

  // Connection status
  doc["callConnected"] = callConnected;
  if (callConnected) {
    doc["remoteIp"] = ipToString(tcpClient.remoteIP());
    doc["callLength"] = connectTimeString();
  }

  // System info
  doc["version"] = build;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

// GET /api/settings - Return all settings as JSON
void handleApiSettingsGet() {
  JsonDocument doc;

  // WiFi settings
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = ssid;
  // Don't send actual password, just indicate if set
  wifi["passwordSet"] = password.length() > 0;

  // Serial settings
  JsonObject serial = doc["serial"].to<JsonObject>();
  serial["baudIndex"] = serialSpeed;
  serial["baudRate"] = bauds[serialSpeed];
  serial["configIndex"] = serialConfig;

  // Available baud rates
  JsonArray baudOptions = doc["baudOptions"].to<JsonArray>();
  for (int i = 0; i < 9; i++) {
    baudOptions.add(bauds[i]);
  }

  // Modem settings
  JsonObject modem = doc["modem"].to<JsonObject>();
  modem["echo"] = echo;
  modem["verbose"] = verboseResults;
  modem["autoAnswer"] = autoAnswer;
  modem["telnet"] = telnet;
  modem["petscii"] = petTranslate;

  // Flow control
  JsonObject flow = doc["flow"].to<JsonObject>();
  flow["control"] = flowControl;
  flow["pinPolarity"] = pinPolarity;

  // Network settings
  JsonObject network = doc["network"].to<JsonObject>();
  network["serverPort"] = tcpServerPort;
  network["busyMsg"] = busyMsg;

  // Speed dials
  JsonArray dials = doc["speedDials"].to<JsonArray>();
  for (int i = 0; i < 10; i++) {
    dials.add(speedDials[i]);
  }

  // Display settings
  JsonObject display = doc["display"].to<JsonObject>();
  display["orientation"] = dispOrientation;
  display["defaultMode"] = defaultMode;

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

// POST /api/settings - Update settings from JSON
void handleApiSettingsPost() {
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"error\":\"No body\"}");
    return;
  }

  String body = webServer.arg("plain");
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  bool needsRestart = false;

  // Update WiFi settings
  if (doc.containsKey("wifi")) {
    JsonObject wifi = doc["wifi"];
    if (wifi.containsKey("ssid")) {
      String newSsid = wifi["ssid"].as<String>();
      if (newSsid != ssid) {
        ssid = newSsid;
        needsRestart = true;
      }
    }
    if (wifi.containsKey("password")) {
      String newPass = wifi["password"].as<String>();
      if (newPass.length() > 0) {
        password = newPass;
        needsRestart = true;
      }
    }
  }

  // Update serial settings
  if (doc.containsKey("serial")) {
    JsonObject serial = doc["serial"];
    if (serial.containsKey("baudIndex")) {
      byte newBaud = serial["baudIndex"];
      if (newBaud < 9) {
        serialSpeed = newBaud;
      }
    }
    if (serial.containsKey("configIndex")) {
      byte newConfig = serial["configIndex"];
      if (newConfig < 24) {
        serialConfig = newConfig;
      }
    }
  }

  // Update modem settings
  if (doc.containsKey("modem")) {
    JsonObject modem = doc["modem"];
    if (modem.containsKey("echo")) echo = modem["echo"];
    if (modem.containsKey("verbose")) verboseResults = modem["verbose"];
    if (modem.containsKey("autoAnswer")) autoAnswer = modem["autoAnswer"];
    if (modem.containsKey("telnet")) telnet = modem["telnet"];
    if (modem.containsKey("petscii")) petTranslate = modem["petscii"];
  }

  // Update flow control
  if (doc.containsKey("flow")) {
    JsonObject flow = doc["flow"];
    if (flow.containsKey("control")) {
      byte newFlow = flow["control"];
      if (newFlow <= 2) flowControl = newFlow;
    }
    if (flow.containsKey("pinPolarity")) {
      byte newPol = flow["pinPolarity"];
      if (newPol <= 1) pinPolarity = newPol;
    }
  }

  // Update network settings
  if (doc.containsKey("network")) {
    JsonObject network = doc["network"];
    if (network.containsKey("serverPort")) {
      int newPort = network["serverPort"];
      if (newPort > 0 && newPort < 65536) {
        tcpServerPort = newPort;
      }
    }
    if (network.containsKey("busyMsg")) {
      busyMsg = network["busyMsg"].as<String>();
    }
  }

  // Update speed dials
  if (doc.containsKey("speedDials")) {
    JsonArray dials = doc["speedDials"];
    for (int i = 0; i < 10 && i < dials.size(); i++) {
      speedDials[i] = dials[i].as<String>();
    }
  }

  // Update display settings
  if (doc.containsKey("display")) {
    JsonObject display = doc["display"];
    if (display.containsKey("orientation")) {
      byte newOri = display["orientation"];
      if (newOri <= 1) dispOrientation = newOri;
    }
    if (display.containsKey("defaultMode")) {
      byte newMode = display["defaultMode"];
      if (newMode <= 1) defaultMode = newMode;
    }
  }

  // Save to EEPROM
  writeSettings();

  // Send response
  JsonDocument response;
  response["success"] = true;
  response["needsRestart"] = needsRestart;
  if (needsRestart) {
    response["message"] = "WiFi settings changed. Reboot required.";
  }

  String responseStr;
  serializeJson(response, responseStr);
  webServer.send(200, "application/json", responseStr);
}

// POST /api/reboot - Trigger device reboot
void handleApiReboot() {
  webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
  delay(500);
  ESP.restart();
}

// GET /api/files - List files on SD card
void handleApiFilesList() {
  JsonDocument doc;
  JsonArray files = doc["files"].to<JsonArray>();

  // Try to initialize SD if not already
  if (!SD.begin()) {
    doc["error"] = "SD card not available";
    String response;
    serializeJson(doc, response);
    webServer.send(500, "application/json", response);
    return;
  }

  File root = SD.open("/");
  if (!root) {
    doc["error"] = "Cannot open root directory";
    String response;
    serializeJson(doc, response);
    webServer.send(500, "application/json", response);
    return;
  }

  File entry;
  while ((entry = root.openNextFile())) {
    if (!entry.isDirectory()) {
      JsonObject file = files.add<JsonObject>();
      file["name"] = String(entry.name());
      file["size"] = entry.size();
    }
    entry.close();
    yield();
  }
  root.close();

  // SD card info
  doc["totalBytes"] = SD.totalBytes();
  doc["usedBytes"] = SD.usedBytes();

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

// GET /api/download?name=filename - Download file from SD
void handleApiFilesGet() {
  if (!webServer.hasArg("name")) {
    webServer.send(400, "application/json", "{\"error\":\"Missing filename\"}");
    return;
  }

  String filename = webServer.arg("name");
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  if (!SD.begin()) {
    webServer.send(500, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }

  if (!SD.exists(filename)) {
    webServer.send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file) {
    webServer.send(500, "application/json", "{\"error\":\"Cannot open file\"}");
    return;
  }

  // Send file with proper headers for download
  String contentDisposition = "attachment; filename=\"" + filename.substring(1) + "\"";
  webServer.sendHeader("Content-Disposition", contentDisposition);
  webServer.streamFile(file, "application/octet-stream");
  file.close();
}

// DELETE /api/files?name=filename - Delete file from SD
void handleApiFilesDelete() {
  if (!webServer.hasArg("name")) {
    webServer.send(400, "application/json", "{\"error\":\"Missing filename\"}");
    return;
  }

  String filename = webServer.arg("name");
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  if (!SD.begin()) {
    webServer.send(500, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }

  if (!SD.exists(filename)) {
    webServer.send(404, "application/json", "{\"error\":\"File not found\"}");
    return;
  }

  if (SD.remove(filename)) {
    webServer.send(200, "application/json", "{\"success\":true}");
  } else {
    webServer.send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
  }
}

// POST /api/upload - Response handler for file upload
void handleApiUpload() {
  webServer.send(200, "application/json", "{\"success\":true}");
}

// File upload processor (called during upload chunks)
void handleApiUploadProcess() {
  HTTPUpload& upload = webServer.upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Initialize SD card
    if (!SD.begin()) {
      return;
    }

    String filename = "/" + upload.filename;
    // Remove existing file if it exists
    if (SD.exists(filename)) {
      SD.remove(filename);
    }
    uploadFile = SD.open(filename, FILE_WRITE);
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  }
}

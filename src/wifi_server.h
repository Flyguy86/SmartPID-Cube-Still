#pragma once

void initWiFi();             // Configure ESP8266, start HTTP server
void wifiPoll();             // Call every loop — handles incoming HTTP requests
bool isWiFiReady();          // True if ESP8266 initialized successfully
void wifiCheckConnection();  // Periodic STA/AP failover check
bool isWiFiSTA();            // True if in STA (client) mode
bool isWiFiSTAFailed();      // True if last STA join attempt failed
const char* getWiFiIP();     // Current IP address string

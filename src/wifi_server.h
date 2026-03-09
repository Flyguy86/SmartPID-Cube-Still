#pragma once

void initWiFi();     // Configure ESP8266, start HTTP server
void wifiPoll();     // Call every loop — handles incoming HTTP requests
bool isWiFiReady();  // True if ESP8266 initialized successfully

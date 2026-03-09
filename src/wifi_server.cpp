// ═══════════════════════════════════════════════════════════════════════════════
//  ESP8266 AT Command WiFi Server
//  Handles: WiFi init, TCP server on port 80, HTTP request/response
//  Communication: Serial1 at 57600 baud
// ═══════════════════════════════════════════════════════════════════════════════

#include "wifi_server.h"
#include "config.h"
#include "storage.h"
#include "sensors.h"
#include "outputs.h"
#include "pages.h"
#include "scheduler.h"

static bool wifiReady = false;

// ─── Low-level AT helpers ───────────────────────────────────────────────────

// Wait for a specific character from ESP
static bool espWaitChar(char expected, int timeoutMs) {
    unsigned long start = millis();
    while (millis() - start < (unsigned long)timeoutMs) {
        if (Serial1.available()) {
            if (Serial1.read() == expected) return true;
        }
    }
    return false;
}

// Wait for a line containing 'expected' string. Returns true on match.
static bool espWaitLine(const char* expected, int timeoutMs) {
    unsigned long start = millis();
    char buf[128];
    int pos = 0;
    while (millis() - start < (unsigned long)timeoutMs) {
        if (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                buf[pos] = '\0';
                if (strstr(buf, expected)) return true;
                pos = 0;
            } else if (c != '\r' && pos < 126) {
                buf[pos++] = c;
            }
        }
    }
    return false;
}

// Send AT command, wait for OK/ERROR
// Yields to critical tasks (sensors/PID) while waiting for ESP response
static bool espCmd(const char* cmd, int timeout = 3000) {
    while (Serial1.available()) Serial1.read();
    Serial1.println(cmd);
    unsigned long start = millis();
    char buf[128];
    int pos = 0;
    while (millis() - start < (unsigned long)timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                buf[pos] = '\0';
                if (strstr(buf, "OK"))    return true;
                if (strstr(buf, "ERROR")) return false;
                if (strstr(buf, "FAIL"))  return false;
                pos = 0;
            } else if (c != '\r' && pos < 126) {
                buf[pos++] = c;
            }
        } else {
            // No serial data ready — yield to critical tasks
            yieldCritical();
        }
    }
    return false;
}

// Send AT command and capture full response
// Yields to critical tasks while waiting
static bool espCmdResp(const char* cmd, char* resp, int maxResp, int timeout = 3000) {
    while (Serial1.available()) Serial1.read();
    Serial1.println(cmd);
    unsigned long start = millis();
    int rp = 0;
    char line[128];
    int lp = 0;
    resp[0] = '\0';
    while (millis() - start < (unsigned long)timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            if (rp < maxResp - 1) resp[rp++] = c;
            resp[rp] = '\0';
            if (c == '\n') {
                line[lp] = '\0';
                if (strstr(line, "OK"))    return true;
                if (strstr(line, "ERROR")) return false;
                lp = 0;
            } else if (c != '\r' && lp < 126) {
                line[lp++] = c;
            }
        } else {
            yieldCritical();
        }
    }
    return false;
}

// ─── IPD State (declared early so handlers can reset it) ────────────────────

enum IPDState : uint8_t {
    S_IDLE = 0,
    S_GOT_PLUS,     // seen '+'
    S_GOT_I,        // seen '+I'
    S_GOT_P,        // seen '+IP'
    S_GOT_D,        // seen '+IPD'
    S_IPD_META,     // reading "connId,len" until ':'
    S_IPD_DATA      // reading 'len' bytes of request data
};

static IPDState ipdState = S_IDLE;
static char ipdMeta[20];
static int  ipdMetaPos = 0;
static char ipdBuf[1024];
static int  ipdBufPos  = 0;
static int  ipdConnId  = -1;
static int  ipdTotalLen = 0;

// ─── HTTP Response Sending ──────────────────────────────────────────────────

// Send a chunk of data via AT+CIPSEND
// Uses a robust wait that skips +IPD data arriving mid-send
static bool espSendChunk(int connId, const char* data, int len) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d", connId, len);

    // Flush stale data
    delay(2);
    while (Serial1.available()) Serial1.read();

    Serial1.println(cmd);

    // Wait for '>' prompt, skipping any +IPD / noise
    {
        unsigned long t = millis();
        bool gotPrompt = false;
        while (millis() - t < 3000) {
            if (Serial1.available()) {
                char c = Serial1.read();
                if (c == '>') { gotPrompt = true; break; }
            } else {
                yieldCritical();
            }
        }
        if (!gotPrompt) {
            SerialUSB.println(F("[wifi] CIPSEND no >"));
            return false;
        }
    }

    // Send raw data
    Serial1.write((const uint8_t*)data, len);

    // Wait for SEND OK, skip everything else (including +IPD)
    {
        unsigned long t = millis();
        char buf[20];
        int bp = 0;
        while (millis() - t < 3000) {
            if (Serial1.available()) {
                char c = Serial1.read();
                if (c == '\n') {
                    buf[bp] = '\0';
                    if (strstr(buf, "SEND OK")) return true;
                    if (strstr(buf, "ERROR") || strstr(buf, "CLOSED")) return false;
                    bp = 0;
                } else if (c != '\r' && bp < 18) {
                    buf[bp++] = c;
                }
            } else {
                yieldCritical();
            }
        }
    }
    SerialUSB.println(F("[wifi] CIPSEND no SEND OK"));
    return false;
}

// Close a connection
static void espClose(int connId) {
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", connId);
    espCmd(cmd, 2000);
}

// Send HTTP response header + body (with Content-Length for reliable delivery)
// cacheSecs > 0 adds Cache-Control header so browser doesn't re-fetch static pages
static void sendHTTPResponse(int connId, const char* contentType, const char* body, int bodyLen, int cacheSecs = 0) {
    // Build header
    char header[256];
    int hlen;
    if (cacheSecs > 0) {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "Cache-Control: max-age=%d\r\n"
            "Connection: close\r\n"
            "\r\n",
            contentType, bodyLen, cacheSecs);
    } else {
        hlen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            contentType, bodyLen);
    }

    // Send header
    if (!espSendChunk(connId, header, hlen)) { espClose(connId); return; }

    // Send body in larger chunks (2048 bytes — ESP8266 AT firmware supports up to 2048)
    const int CHUNK = 2048;
    int sent = 0;
    while (sent < bodyLen) {
        int chunk = bodyLen - sent;
        if (chunk > CHUNK) chunk = CHUNK;
        if (!espSendChunk(connId, body + sent, chunk)) break;
        sent += chunk;
    }

    espClose(connId);

    // Reset IPD parser after large transfers (incoming +IPD may have been lost)
    ipdState = S_IDLE;
    delay(10);
    while (Serial1.available()) Serial1.read();
}

// Send a JSON response — combines header+body in ONE CIPSEND to halve AT overhead
static void sendJSON(int connId, const char* json, int jsonLen) {
    char buf[600];
    int hlen = snprintf(buf, sizeof(buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        jsonLen);

    // If header+body fits in buffer, send as single chunk (1 AT round trip)
    if (hlen + jsonLen < (int)sizeof(buf)) {
        memcpy(buf + hlen, json, jsonLen);
        espSendChunk(connId, buf, hlen + jsonLen);
    } else {
        // Fallback: separate sends
        espSendChunk(connId, buf, hlen);
        espSendChunk(connId, json, jsonLen);
    }
    espClose(connId);
    ipdState = S_IDLE;
}

// Send a binary response — combines header+body in ONE CIPSEND
static void sendBinary(int connId, const uint8_t* data, int dataLen) {
    char buf[200];
    int hlen = snprintf(buf, sizeof(buf),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        dataLen);

    // Header + binary body always fits — status is only 17 bytes
    memcpy(buf + hlen, data, dataLen);
    espSendChunk(connId, buf, hlen + dataLen);
    espClose(connId);
    ipdState = S_IDLE;
}

// Send a 200 OK with simple message
static void sendOK(int connId) {
    const char* j = "{\"ok\":true}";
    sendJSON(connId, j, strlen(j));
}

// Send 404
static void send404(int connId) {
    const char* body = "Not Found";
    char header[128];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n\r\n");
    espSendChunk(connId, header, hlen);
    espSendChunk(connId, body, strlen(body));
    espClose(connId);
}

// ─── Query String Parser ───────────────────────────────────────────────────

static bool getParam(const char* query, const char* key, char* val, int maxVal) {
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char* found = strstr(query, search);
    if (!found) return false;
    found += strlen(search);
    const char* end = strchr(found, '&');
    int len = end ? (int)(end - found) : (int)strlen(found);
    if (len >= maxVal) len = maxVal - 1;
    strncpy(val, found, len);
    val[len] = '\0';
    return true;
}

// Simple URL decode (handles %XX and +)
static void urlDecode(char* dst, const char* src, int maxLen) {
    int di = 0;
    for (int si = 0; src[si] && di < maxLen - 1; si++) {
        if (src[si] == '%' && src[si+1] && src[si+2]) {
            char hex[3] = { src[si+1], src[si+2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}



// ─── API Handlers ───────────────────────────────────────────────────────────

// Binary status packet layout (17 bytes, little-endian):
//   [0-1]  int16  temp0       (°F × 10)
//   [2-3]  int16  temp1       (°F × 10)
//   [4]    uint8  flags       bit0-1: connected[0,1]  bit2-6: outputs[0..4]
//   [5]    uint8  ssrPWM      (0-255)
//   [6]    uint8  runState    (0=IDLE 1=HEAT 2=HOLD 3=DONE)
//   [7-8]  int16  targetTemp  (°F × 10)
//   [9-10] int16  currentTemp (°F × 10)
//   [11-12] uint16 holdRemSec
//   [13]   uint8  maxPWM
//   [14]   uint8  currentStep
//   [15]   uint8  totalSteps
//   [16]   int8   autoTuneSensor (-1 = inactive)
static void handleStatus(int connId) {
    Settings& s = getSettings();
    int cs = getCurrentStep();
    ProfileStep& step = s.profile.steps[cs < s.profile.numSteps ? cs : 0];

    uint8_t bin[17];
    int16_t t0 = (int16_t)(getSensorTemp(0) * 10.0f);
    int16_t t1 = (int16_t)(getSensorTemp(1) * 10.0f);
    memcpy(bin + 0, &t0, 2);
    memcpy(bin + 2, &t1, 2);

    uint8_t flags = 0;
    if (isSensorConnected(0)) flags |= 0x01;
    if (isSensorConnected(1)) flags |= 0x02;
    if (getOutput(0)) flags |= 0x04;
    if (getOutput(1)) flags |= 0x08;
    if (getOutput(2)) flags |= 0x10;
    if (getOutput(3)) flags |= 0x20;
    if (getOutput(4)) flags |= 0x40;
    bin[4] = flags;

    bin[5] = (uint8_t)getSSRPWM();
    bin[6] = (uint8_t)getRunState();

    int16_t tgt = (int16_t)(step.targetTemp * 10.0f);
    int16_t cur = (int16_t)(getSensorTemp(step.sensorIndex) * 10.0f);
    memcpy(bin + 7, &tgt, 2);
    memcpy(bin + 9, &cur, 2);

    uint16_t rem = (uint16_t)getHoldRemaining();
    memcpy(bin + 11, &rem, 2);

    bin[13] = (uint8_t)step.maxPWM;
    bin[14] = (uint8_t)cs;
    bin[15] = (uint8_t)s.profile.numSteps;
    bin[16] = (uint8_t)((int8_t)getAutoTuneSensor());

    sendBinary(connId, bin, 17);
}

static void handleSettingsGet(int connId) {
    Settings& s = getSettings();
    char json[600];
    int n = snprintf(json, sizeof(json),
        "{\"ssid\":\"%s\","
        "\"sc\":[{\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.2f,\"out\":%d},"
        "{\"kp\":%.2f,\"ki\":%.3f,\"kd\":%.2f,\"out\":%d}],"
        "\"prof\":{\"n\":%d,\"steps\":[",
        s.wifi.ssid,
        s.sensorCfg[0].pid.Kp, s.sensorCfg[0].pid.Ki, s.sensorCfg[0].pid.Kd,
        s.sensorCfg[0].outputIndex,
        s.sensorCfg[1].pid.Kp, s.sensorCfg[1].pid.Ki, s.sensorCfg[1].pid.Kd,
        s.sensorCfg[1].outputIndex,
        s.profile.numSteps
    );
    for (int i = 0; i < s.profile.numSteps && i < MAX_PROFILE_STEPS; i++) {
        if (i > 0 && n < (int)sizeof(json) - 60) json[n++] = ',';
        n += snprintf(json + n, sizeof(json) - n,
            "{\"target\":%.1f,\"hold\":%d,\"sensor\":%d,\"maxpwm\":%d,\"out\":%d}",
            s.profile.steps[i].targetTemp,
            s.profile.steps[i].holdMinutes,
            s.profile.steps[i].sensorIndex,
            s.profile.steps[i].maxPWM,
            s.profile.steps[i].outputIndex);
    }
    n += snprintf(json + n, sizeof(json) - n, "]}}");
    sendJSON(connId, json, n);
}

static void handleOutputToggle(int connId, const char* query) {
    char val[8];
    int id = 0, state = 0;
    if (getParam(query, "id", val, sizeof(val))) id = atoi(val);
    if (getParam(query, "s", val, sizeof(val)))  state = atoi(val);
    setOutput(id, state != 0);
    SerialUSB.print(F("[web] output ")); SerialUSB.print(id);
    SerialUSB.print(F(" = ")); SerialUSB.println(state);
    sendOK(connId);
}

static void handlePWMSet(int connId, const char* query) {
    char val[8];
    if (getParam(query, "v", val, sizeof(val))) {
        int v = atoi(val);
        setSSRPWM(v);
        SerialUSB.print(F("[web] PWM = ")); SerialUSB.println(v);
    }
    sendOK(connId);
}

static void handleWifiSave(int connId, const char* query) {
    Settings& s = getSettings();
    char raw[65];

    if (getParam(query, "ssid", raw, sizeof(raw))) {
        urlDecode(s.wifi.ssid, raw, sizeof(s.wifi.ssid));
    }
    if (getParam(query, "pass", raw, sizeof(raw))) {
        urlDecode(s.wifi.password, raw, sizeof(s.wifi.password));
    }
    s.wifi.configured = (strlen(s.wifi.ssid) > 0);
    saveSettings();

    SerialUSB.print(F("[web] WiFi saved: ")); SerialUSB.println(s.wifi.ssid);
    sendOK(connId);

    // Try to join the network
    delay(500);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP_CUR=\"%s\",\"%s\"", s.wifi.ssid, s.wifi.password);
    if (espCmd(cmd, 15000)) {
        SerialUSB.println(F("[wifi] Connected to AP!"));
        // Read new IP
        char resp[256];
        if (espCmdResp("AT+CIFSR", resp, sizeof(resp), 3000)) {
            SerialUSB.print(F("[wifi] ")); SerialUSB.print(resp);
        }
    } else {
        SerialUSB.println(F("[wifi] Failed to join AP"));
    }
}

static void handlePIDSave(int connId, const char* query) {
    Settings& s = getSettings();
    char val[16];
    int n = 0;
    if (getParam(query, "n", val, sizeof(val))) n = atoi(val);
    if (n < 0 || n > 1) n = 0;

    if (getParam(query, "kp", val, sizeof(val))) s.sensorCfg[n].pid.Kp = atof(val);
    if (getParam(query, "ki", val, sizeof(val))) s.sensorCfg[n].pid.Ki = atof(val);
    if (getParam(query, "kd", val, sizeof(val))) s.sensorCfg[n].pid.Kd = atof(val);
    if (getParam(query, "out", val, sizeof(val))) {
        int o = atoi(val);
        if (o >= 0 && o < NUM_OUTPUTS) s.sensorCfg[n].outputIndex = o;
    }
    saveSettings();

    SerialUSB.print(F("[web] Sensor[")); SerialUSB.print(n);
    SerialUSB.print(F("] Kp=")); SerialUSB.print(s.sensorCfg[n].pid.Kp);
    SerialUSB.print(F(" Ki=")); SerialUSB.print(s.sensorCfg[n].pid.Ki);
    SerialUSB.print(F(" Kd=")); SerialUSB.print(s.sensorCfg[n].pid.Kd);
    SerialUSB.print(F(" out=")); SerialUSB.println(s.sensorCfg[n].outputIndex);
    sendOK(connId);
}

static void handleProfileSave(int connId, const char* query) {
    Settings& s = getSettings();
    char val[16];
    int idx = 0;

    if (getParam(query, "step", val, sizeof(val))) idx = atoi(val);
    if (idx < 0 || idx >= s.profile.numSteps) idx = 0;

    ProfileStep& step = s.profile.steps[idx];
    if (getParam(query, "target", val, sizeof(val))) step.targetTemp = atof(val);
    if (getParam(query, "hold", val, sizeof(val)))   step.holdMinutes = atoi(val);
    if (getParam(query, "sensor", val, sizeof(val))) step.sensorIndex = atoi(val);
    if (getParam(query, "maxpwm", val, sizeof(val))) step.maxPWM = atoi(val);
    if (getParam(query, "out", val, sizeof(val))) {
        int o = atoi(val);
        if (o >= 0 && o < NUM_OUTPUTS) step.outputIndex = o;
    }
    saveSettings();

    SerialUSB.print(F("[web] Step ")); SerialUSB.print(idx + 1);
    SerialUSB.print(F(": target=")); SerialUSB.print(step.targetTemp);
    SerialUSB.print(F("°F hold=")); SerialUSB.print(step.holdMinutes);
    SerialUSB.print(F("min sensor=")); SerialUSB.print(step.sensorIndex);
    SerialUSB.print(F(" out=")); SerialUSB.print(step.outputIndex);
    SerialUSB.print(F(" maxPWM=")); SerialUSB.println(step.maxPWM);
    sendOK(connId);
}

static void handleProfileAdd(int connId) {
    Settings& s = getSettings();
    if (s.profile.numSteps >= MAX_PROFILE_STEPS) {
        sendOK(connId);
        return;
    }
    int n = s.profile.numSteps;
    // Copy from last step as template
    s.profile.steps[n] = s.profile.steps[n > 0 ? n - 1 : 0];
    s.profile.numSteps++;
    saveSettings();
    SerialUSB.print(F("[web] Added step ")); SerialUSB.println(s.profile.numSteps);
    sendOK(connId);
}

static void handleProfileDel(int connId, const char* query) {
    Settings& s = getSettings();
    if (s.profile.numSteps <= 1) {
        sendOK(connId);
        return;
    }
    char val[8];
    int idx = s.profile.numSteps - 1;
    if (getParam(query, "step", val, sizeof(val))) idx = atoi(val);
    if (idx < 0 || idx >= s.profile.numSteps) {
        sendOK(connId);
        return;
    }
    // Shift remaining steps down
    for (int i = idx; i < s.profile.numSteps - 1; i++) {
        s.profile.steps[i] = s.profile.steps[i + 1];
    }
    s.profile.numSteps--;
    saveSettings();
    SerialUSB.print(F("[web] Removed step, now ")); SerialUSB.println(s.profile.numSteps);
    sendOK(connId);
}

static void handleStart(int connId) {
    startProfile();
    sendOK(connId);
}

static void handleStop(int connId) {
    stopProfile();
    sendOK(connId);
}

// ─── WiFi Scan Handler ─────────────────────────────────────────────────────

static void handleScan(int connId) {
    SerialUSB.println(F("[wifi] Scanning networks..."));

    // Run AT+CWLAP (takes several seconds)
    while (Serial1.available()) Serial1.read();
    Serial1.println("AT+CWLAP");

    char ssids[400];
    int sp = 0;
    ssids[sp++] = '[';
    int count = 0;

    unsigned long start = millis();
    char line[128];
    int lp = 0;
    int skipBytes = 0;  // bytes to drain from +IPD payload

    while (millis() - start < 8000) {
        if (Serial1.available()) {
            char c = Serial1.read();

            // Drain stray +IPD payload bytes
            if (skipBytes > 0) {
                skipBytes--;
                continue;
            }

            if (c == '\n') {
                line[lp] = '\0';

                // Detect +IPD arriving during scan — skip entire payload
                char* ipd = strstr(line, "+IPD,");
                if (ipd) {
                    char* cm = strchr(ipd + 5, ',');
                    if (cm) {
                        int ipdLen = atoi(cm + 1);
                        char* col = strchr(cm + 1, ':');
                        if (col) {
                            int used = lp - (int)(col + 1 - line);
                            if (used < 0) used = 0;
                            skipBytes = ipdLen - used;
                            if (skipBytes < 0) skipBytes = 0;
                        } else {
                            skipBytes = ipdLen;
                        }
                    }
                    lp = 0;
                    continue;
                }

                if (strstr(line, "+CWLAP:")) {
                    // +CWLAP:(ecn,"ssid",rssi,...)
                    char* q1 = strchr(line, '"');
                    if (q1) {
                        char* q2 = strchr(q1 + 1, '"');
                        if (q2 && q2 > q1 + 1) {
                            *q2 = '\0';
                            if (count > 0 && sp < 390) ssids[sp++] = ',';
                            if (sp < 385) {
                                ssids[sp++] = '"';
                                for (char* p = q1 + 1; *p && sp < 393; p++) {
                                    if (*p == '"' || *p == '\\') ssids[sp++] = '\\';
                                    ssids[sp++] = *p;
                                }
                                ssids[sp++] = '"';
                                count++;
                            }
                        }
                    }
                }
                if (strstr(line, "OK") || strstr(line, "ERROR")) break;
                lp = 0;
            } else if (c != '\r' && lp < 126) {
                line[lp++] = c;
            }
        }
    }
    ssids[sp++] = ']';
    ssids[sp] = '\0';

    // Brief flush before sending response
    delay(50);
    while (Serial1.available()) Serial1.read();

    char json[430];
    int n = snprintf(json, sizeof(json), "{\"nets\":%s}", ssids);
    sendJSON(connId, json, n);

    // Recovery: flush serial, reset IPD parser, re-verify server
    delay(100);
    while (Serial1.available()) Serial1.read();
    ipdState = S_IDLE;

    // Re-establish TCP server in case scan disrupted it
    espCmd("AT+CIPMUX=1", 1000);
    espCmd("AT+CIPSERVER=1,80", 2000);

    SerialUSB.print(F("[wifi] Found "));
    SerialUSB.print(count);
    SerialUSB.println(F(" networks"));
}

// ─── Auto-Tune Handler ─────────────────────────────────────────────────────

static void handleAutoTune(int connId, const char* query) {
    char val[8];
    int n = 0;
    if (getParam(query, "n", val, sizeof(val))) n = atoi(val);
    if (n < 0 || n > 1) n = 0;

    if (isAutoTuning()) {
        stopAutoTune();
        sendOK(connId);
    } else {
        startAutoTune(n);
        sendOK(connId);
    }
}

// ─── HTTP Request Router ───────────────────────────────────────────────────

static void handleHTTPRequest(int connId, const char* data, int len) {
    // Parse first line: "GET /path?query HTTP/1.1"
    char method[8] = {0};
    char path[64]  = {0};
    char query[256] = {0};

    const char* sp1 = strchr(data, ' ');
    if (!sp1) { espClose(connId); return; }
    int mlen = sp1 - data;
    if (mlen > 7) mlen = 7;
    strncpy(method, data, mlen);

    sp1++;
    const char* sp2 = strchr(sp1, ' ');
    if (!sp2) { espClose(connId); return; }

    const char* qmark = (const char*)memchr(sp1, '?', sp2 - sp1);
    if (qmark) {
        int plen = qmark - sp1;
        if (plen > 63) plen = 63;
        strncpy(path, sp1, plen);
        qmark++;
        int qlen = sp2 - qmark;
        if (qlen > 255) qlen = 255;
        strncpy(query, qmark, qlen);
    } else {
        int plen = sp2 - sp1;
        if (plen > 63) plen = 63;
        strncpy(path, sp1, plen);
    }

    SerialUSB.print(F("[http] ")); SerialUSB.print(method);
    SerialUSB.print(' '); SerialUSB.println(path);

    // Route
    if (strcmp(path, "/") == 0) {
        sendHTTPResponse(connId, "text/html", PAGE_MAIN, strlen(PAGE_MAIN), 300);
    }
    else if (strcmp(path, "/settings") == 0) {
        sendHTTPResponse(connId, "text/html", PAGE_SETTINGS, strlen(PAGE_SETTINGS), 300);
    }
    else if (strcmp(path, "/api/status") == 0) {
        handleStatus(connId);
    }
    else if (strcmp(path, "/api/settings") == 0) {
        handleSettingsGet(connId);
    }
    else if (strcmp(path, "/api/output") == 0) {
        handleOutputToggle(connId, query);
    }
    else if (strcmp(path, "/api/pwm") == 0) {
        handlePWMSet(connId, query);
    }
    else if (strcmp(path, "/api/wifi") == 0) {
        handleWifiSave(connId, query);
    }
    else if (strcmp(path, "/api/pid") == 0) {
        handlePIDSave(connId, query);
    }
    else if (strcmp(path, "/api/profile") == 0) {
        handleProfileSave(connId, query);
    }
    else if (strcmp(path, "/api/profile/add") == 0) {
        handleProfileAdd(connId);
    }
    else if (strcmp(path, "/api/profile/del") == 0) {
        handleProfileDel(connId, query);
    }
    else if (strcmp(path, "/api/start") == 0) {
        handleStart(connId);
    }
    else if (strcmp(path, "/api/stop") == 0) {
        handleStop(connId);
    }
    else if (strcmp(path, "/api/scan") == 0) {
        handleScan(connId);
    }
    else if (strcmp(path, "/api/autotune") == 0) {
        handleAutoTune(connId, query);
    }
    else {
        send404(connId);
    }
}

// ─── +IPD Stream Parser (state machine) ────────────────────────────────────
//
// Detects "+IPD,<connId>,<len>:" in the Serial1 byte stream,
// then reads exactly <len> bytes of HTTP request data.

static void processESPByte(char c) {
    switch (ipdState) {
        case S_IDLE:
            if (c == '+') ipdState = S_GOT_PLUS;
            break;
        case S_GOT_PLUS:
            ipdState = (c == 'I') ? S_GOT_I : S_IDLE;
            break;
        case S_GOT_I:
            ipdState = (c == 'P') ? S_GOT_P : S_IDLE;
            break;
        case S_GOT_P:
            ipdState = (c == 'D') ? S_GOT_D : S_IDLE;
            break;
        case S_GOT_D:
            if (c == ',') {
                ipdState   = S_IPD_META;
                ipdMetaPos = 0;
            } else {
                ipdState = S_IDLE;
            }
            break;
        case S_IPD_META:
            if (c == ':') {
                ipdMeta[ipdMetaPos] = '\0';
                // Parse "connId,len"
                char* comma = strchr(ipdMeta, ',');
                if (comma) {
                    *comma = '\0';
                    ipdConnId  = atoi(ipdMeta);
                    ipdTotalLen = atoi(comma + 1);
                } else {
                    ipdState = S_IDLE;
                    break;
                }
                if (ipdTotalLen > (int)sizeof(ipdBuf) - 1)
                    ipdTotalLen = sizeof(ipdBuf) - 1;
                ipdBufPos = 0;
                ipdState  = S_IPD_DATA;
            } else if (ipdMetaPos < (int)sizeof(ipdMeta) - 1) {
                ipdMeta[ipdMetaPos++] = c;
            }
            break;
        case S_IPD_DATA:
            ipdBuf[ipdBufPos++] = c;
            if (ipdBufPos >= ipdTotalLen) {
                ipdBuf[ipdBufPos] = '\0';
                handleHTTPRequest(ipdConnId, ipdBuf, ipdBufPos);
                ipdState = S_IDLE;
            }
            break;
    }
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool isWiFiReady() {
    return wifiReady;
}

// Time-budgeted WiFi poll — process available serial data but limit
// to WIFI_POLL_MAX_BYTES per invocation to avoid starving other tasks
#define WIFI_POLL_MAX_BYTES 128

void wifiPoll() {
    if (!wifiReady) return;
    int processed = 0;
    while (Serial1.available() && processed < WIFI_POLL_MAX_BYTES) {
        processESPByte(Serial1.read());
        processed++;
    }
}

void initWiFi() {
    SerialUSB.println(F("[wifi] Initializing ESP8266..."));

    // Try 115200 first (our target baud), then fall back to 57600 and upgrade
    Serial1.begin(115200);
    delay(300);
    while (Serial1.available()) Serial1.read();

    if (!espCmd("AT", 2000)) {
        // ESP may still be at 57600 — try that
        Serial1.end();
        Serial1.begin(57600);
        delay(300);
        while (Serial1.available()) Serial1.read();

        if (!espCmd("AT", 2000)) {
            SerialUSB.println(F("[wifi] ESP8266 not responding! WiFi disabled."));
            Serial1.end();
            return;
        }

        // ESP answered at 57600 — switch it to 115200
        SerialUSB.println(F("[wifi] ESP at 57600, upgrading to 115200..."));
        espCmd("ATE0", 1000);
        Serial1.println("AT+UART_CUR=115200,8,1,0,0");
        delay(200);
        Serial1.end();
        Serial1.begin(115200);
        delay(300);
        while (Serial1.available()) Serial1.read();

        if (!espCmd("AT", 2000)) {
            SerialUSB.println(F("[wifi] Baud upgrade failed! Reverting to 57600."));
            Serial1.end();
            Serial1.begin(57600);
            delay(300);
            while (Serial1.available()) Serial1.read();
            if (!espCmd("AT", 2000)) {
                SerialUSB.println(F("[wifi] ESP8266 lost! WiFi disabled."));
                Serial1.end();
                return;
            }
        }
    }
    SerialUSB.println(F("[wifi] ESP8266 OK"));

    // Disable echo
    espCmd("ATE0", 1000);

    // Check for saved WiFi credentials
    Settings& s = getSettings();

    if (s.wifi.configured && strlen(s.wifi.ssid) > 0) {
        // Station + AP mode (join home network, keep AP as fallback)
        SerialUSB.println(F("[wifi] Credentials found, STA+AP mode"));
        espCmd("AT+CWMODE_CUR=3", 2000);

        // Configure our AP (open, no password)
        espCmd("AT+CWSAP_CUR=\"SmartPID-Still\",\"\",5,0,4,0", 3000);

        // Join the saved network
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+CWJAP_CUR=\"%s\",\"%s\"",
                 s.wifi.ssid, s.wifi.password);
        SerialUSB.print(F("[wifi] Joining: ")); SerialUSB.println(s.wifi.ssid);
        if (espCmd(cmd, 15000)) {
            SerialUSB.println(F("[wifi] Connected to home network!"));
        } else {
            SerialUSB.println(F("[wifi] Could not join — AP-only fallback"));
        }
    } else {
        // AP-only mode
        SerialUSB.println(F("[wifi] No credentials, AP-only mode"));
        espCmd("AT+CWMODE_CUR=2", 2000);
        espCmd("AT+CWSAP_CUR=\"SmartPID-Still\",\"\",5,0,4,0", 3000);
    }

    // Print IP addresses
    char resp[256];
    if (espCmdResp("AT+CIFSR", resp, sizeof(resp), 3000)) {
        // Extract and print IPs
        char* line = strtok(resp, "\n");
        while (line) {
            if (strstr(line, "CIFSR")) {
                SerialUSB.print(F("[wifi] ")); SerialUSB.println(line);
            }
            line = strtok(NULL, "\n");
        }
    }

    // Enable multiple connections (required for server mode)
    espCmd("AT+CIPMUX=1", 2000);

    // Start TCP server on port 80
    if (espCmd("AT+CIPSERVER=1,80", 3000)) {
        SerialUSB.println(F("[wifi] HTTP server started on port 80"));
        wifiReady = true;
    } else {
        SerialUSB.println(F("[wifi] Failed to start server!"));
    }
}

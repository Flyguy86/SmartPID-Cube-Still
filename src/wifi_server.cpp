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
#include "runlog.h"
#include "pinscan.h"

static bool wifiReady = false;
static bool staMode = false;   // true = STA (client), false = AP (hotspot)
static bool staFailed = false; // true if last STA join attempt failed
static char currentIP[20] = "0.0.0.0";  // Cached IP address

// Forward declarations for WiFi mode helpers
static bool wifiStartSTA(const char* ssid, const char* password);
static void wifiStartAP();
static void wifiPrintIPs();
static void wifiStartServer();

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
    static char buf[600];
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

// ─── Float→String Helper (newlib-nano has no %f) ──────────────────────────
// Writes float as string with 'decimals' decimal places into dst. Returns chars written.
static int fmtFloat(char* dst, int maxLen, float v, int decimals) {
    int neg = (v < 0);
    if (neg) v = -v;
    // Scale: 1 decimal → ×10, 2 → ×100, 3 → ×1000
    long scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;
    long scaled = (long)(v * scale + 0.5f);
    long whole = scaled / scale;
    long frac = scaled % scale;
    if (decimals == 0)
        return snprintf(dst, maxLen, "%s%ld", neg ? "-" : "", whole);
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%s%%ld.%%0%dld", decimals);
    return snprintf(dst, maxLen, fmt, neg ? "-" : "", whole, frac);
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
    RunProfile& prof = s.profiles[s.activeProfile];
    int cs = getCurrentStep();
    ProfileStep& step = prof.steps[cs < prof.numSteps ? cs : 0];

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

    // Use first assignment for status display
    int16_t tgt = step.numAssignments > 0 ?
        (int16_t)(step.assignments[0].targetTemp * 10.0f) : 0;
    int sensorIdx = step.numAssignments > 0 ? step.assignments[0].sensorIndex : 0;
    int16_t cur = (int16_t)(getSensorTemp(sensorIdx) * 10.0f);
    memcpy(bin + 7, &tgt, 2);
    memcpy(bin + 9, &cur, 2);

    uint16_t rem = (uint16_t)getHoldRemaining();
    memcpy(bin + 11, &rem, 2);

    bin[13] = step.numAssignments > 0 ? step.assignments[0].maxPWM : 0;
    bin[14] = (uint8_t)cs;
    bin[15] = (uint8_t)prof.numSteps;
    bin[16] = (uint8_t)((int8_t)getAutoTuneSensor());

    sendBinary(connId, bin, 17);
}

static void handleSettingsGet(int connId) {
    Settings& s = getSettings();
    char json[300];
    int n = snprintf(json, sizeof(json), "{\"ssid\":\"%s\",\"sc\":[", s.wifi.ssid);
    for (int i = 0; i < 2; i++) {
        if (i > 0) json[n++] = ',';
        n += snprintf(json + n, sizeof(json) - n, "{\"kp\":");
        n += fmtFloat(json + n, sizeof(json) - n, s.sensorCfg[i].pid.Kp, 2);
        n += snprintf(json + n, sizeof(json) - n, ",\"ki\":");
        n += fmtFloat(json + n, sizeof(json) - n, s.sensorCfg[i].pid.Ki, 3);
        n += snprintf(json + n, sizeof(json) - n, ",\"kd\":");
        n += fmtFloat(json + n, sizeof(json) - n, s.sensorCfg[i].pid.Kd, 2);
        n += snprintf(json + n, sizeof(json) - n, ",\"out\":%d}", s.sensorCfg[i].outputIndex);
    }
    n += snprintf(json + n, sizeof(json) - n, "]}");
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

// ─── Pin Scanner HTTP API ───────────────────────────────────────────────────
// /api/pintest?p=N   → set Arduino pin N HIGH (previous off). p=-1 for all off.
// /api/pintest       → report currently active pin

static void handlePinTest(int connId, const char* query) {
    char val[8];
    if (getParam(query, "p", val, sizeof(val))) {
        int p = atoi(val);
        if (p < 0) {
            scanAllOff();
        } else {
            scanSetPin(p);
        }
        // Respond with pin state
        char resp[96];
        int active = scanGetActivePin();
        if (active >= 0) {
            snprintf(resp, sizeof(resp),
                "{\"ok\":true,\"pin\":%d,\"port\":\"%s\",\"state\":\"HIGH\"}",
                active, pinPortName(active));
        } else {
            strcpy(resp, "{\"ok\":true,\"pin\":-1,\"state\":\"OFF\"}");
        }
        sendJSON(connId, resp, strlen(resp));
    } else {
        // No param — report status
        char resp[96];
        int active = scanGetActivePin();
        if (active >= 0) {
            snprintf(resp, sizeof(resp),
                "{\"pin\":%d,\"port\":\"%s\",\"state\":\"HIGH\"}",
                active, pinPortName(active));
        } else {
            strcpy(resp, "{\"pin\":-1,\"state\":\"OFF\"}");
        }
        sendJSON(connId, resp, strlen(resp));
    }
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

    if (!s.wifi.configured) return;

    // Try to switch to STA mode
    delay(500);
    if (wifiStartSTA(s.wifi.ssid, s.wifi.password)) {
        staMode = true;
    } else {
        staMode = false;
        wifiStartAP();
    }
    wifiStartServer();
    wifiPrintIPs();
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

// ─── Profile API Handlers ───────────────────────────────────────────────────

// GET /api/profiles → summary of all profiles
static void handleProfilesGet(int connId) {
    Settings& s = getSettings();
    char json[256];
    int n = snprintf(json, sizeof(json), "{\"ap\":%d,\"p\":[", s.activeProfile);
    for (int i = 0; i < MAX_PROFILES; i++) {
        if (i > 0) json[n++] = ',';
        n += snprintf(json + n, sizeof(json) - n,
            "{\"name\":\"%s\",\"n\":%d}",
            s.profiles[i].name, s.profiles[i].numSteps);
    }
    n += snprintf(json + n, sizeof(json) - n, "]}");
    sendJSON(connId, json, n);
}

// GET /api/profile/get?p=0 → full detail of one profile
static void handleProfileGetOne(int connId, const char* query) {
    Settings& s = getSettings();
    char val[8];
    int p = 0;
    if (getParam(query, "p", val, sizeof(val))) p = atoi(val);
    if (p < 0 || p >= MAX_PROFILES) p = 0;

    RunProfile& prof = s.profiles[p];

    // Build complete JSON in buffer (max ~1100 bytes for 10 steps × 2 assignments)
    static char json[1300];
    int n = snprintf(json, sizeof(json),
        "{\"name\":\"%s\",\"n\":%d,\"steps\":[", prof.name, prof.numSteps);

    for (int si = 0; si < prof.numSteps; si++) {
        ProfileStep& st = prof.steps[si];
        if (si > 0) json[n++] = ',';
        n += snprintf(json + n, sizeof(json) - n,
            "{\"hold\":%d,\"cool\":%d,\"na\":%d,\"a\":[",
            st.holdMinutes, st.coolMode ? 1 : 0, st.numAssignments);
        for (int ai = 0; ai < st.numAssignments; ai++) {
            SensorAssignment& a = st.assignments[ai];
            if (ai > 0) json[n++] = ',';
            n += snprintf(json + n, sizeof(json) - n,
                "{\"s\":%d,\"o\":%d,\"m\":%d,\"t\":",
                a.sensorIndex, a.outputIndex, a.maxPWM);
            n += fmtFloat(json + n, sizeof(json) - n, a.targetTemp, 1);
            json[n++] = '}';
        }
        n += snprintf(json + n, sizeof(json) - n, "]}");
    }
    n += snprintf(json + n, sizeof(json) - n, "]}");

    sendJSON(connId, json, n);
}

// GET /api/profile/select?p=0 → set active profile
static void handleProfileSelect(int connId, const char* query) {
    Settings& s = getSettings();
    char val[8];
    int p = 0;
    if (getParam(query, "p", val, sizeof(val))) p = atoi(val);
    if (p >= 0 && p < MAX_PROFILES) {
        s.activeProfile = p;
        saveSettings();
        SerialUSB.print(F("[web] Active profile = ")); SerialUSB.println(p);
    }
    sendOK(connId);
}

// GET /api/profile/name?p=0&name=... → set profile name
static void handleProfileName(int connId, const char* query) {
    Settings& s = getSettings();
    char val[8], raw[20];
    int p = 0;
    if (getParam(query, "p", val, sizeof(val))) p = atoi(val);
    if (p < 0 || p >= MAX_PROFILES) p = 0;
    if (getParam(query, "name", raw, sizeof(raw))) {
        urlDecode(s.profiles[p].name, raw, sizeof(s.profiles[p].name));
    }
    saveSettings();
    // Confirm with stored name
    char json[64];
    int n = snprintf(json, sizeof(json), "{\"ok\":true,\"name\":\"%s\"}", s.profiles[p].name);
    sendJSON(connId, json, n);
}

// GET /api/profile/resize?p=0&n=3 → set number of steps
static void handleProfileResize(int connId, const char* query) {
    Settings& s = getSettings();
    char val[8];
    int p = 0, n = 0;
    if (getParam(query, "p", val, sizeof(val))) p = atoi(val);
    if (getParam(query, "n", val, sizeof(val))) n = atoi(val);
    if (p < 0 || p >= MAX_PROFILES) p = 0;
    if (n < 0) n = 0;
    if (n > MAX_PROFILE_STEPS) n = MAX_PROFILE_STEPS;
    // Initialize new steps if growing
    for (int i = s.profiles[p].numSteps; i < n; i++) {
        memset(&s.profiles[p].steps[i], 0, sizeof(ProfileStep));
        s.profiles[p].steps[i].holdMinutes = 60;
    }
    s.profiles[p].numSteps = n;
    saveSettings();
    // Confirm with stored count
    char json[48];
    int jn = snprintf(json, sizeof(json), "{\"ok\":true,\"n\":%d}", s.profiles[p].numSteps);
    sendJSON(connId, json, jn);
}

// GET /api/profile/step?p=0&s=0&hold=60&cool=0&na=1&s0=0&o0=0&m0=255&t0=175.0
static void handleProfileStepSave(int connId, const char* query) {
    Settings& s = getSettings();
    char val[16];
    int p = 0, si = 0;
    if (getParam(query, "p", val, sizeof(val))) p = atoi(val);
    if (getParam(query, "s", val, sizeof(val))) si = atoi(val);
    if (p < 0 || p >= MAX_PROFILES) p = 0;
    if (si < 0 || si >= s.profiles[p].numSteps) { sendOK(connId); return; }

    ProfileStep& step = s.profiles[p].steps[si];
    if (getParam(query, "hold", val, sizeof(val))) step.holdMinutes = atoi(val);
    if (getParam(query, "cool", val, sizeof(val))) step.coolMode = (atoi(val) != 0);
    if (getParam(query, "na", val, sizeof(val))) {
        int na = atoi(val);
        if (na < 0) na = 0;
        if (na > MAX_ASSIGNMENTS_PER_STEP) na = MAX_ASSIGNMENTS_PER_STEP;
        step.numAssignments = na;
    }

    // Parse assignments: s0, o0, m0, t0, s1, o1, m1, t1
    for (int ai = 0; ai < step.numAssignments; ai++) {
        char key[8];
        snprintf(key, sizeof(key), "s%d", ai);
        if (getParam(query, key, val, sizeof(val))) step.assignments[ai].sensorIndex = atoi(val);
        snprintf(key, sizeof(key), "o%d", ai);
        if (getParam(query, key, val, sizeof(val))) step.assignments[ai].outputIndex = atoi(val);
        snprintf(key, sizeof(key), "m%d", ai);
        if (getParam(query, key, val, sizeof(val))) step.assignments[ai].maxPWM = atoi(val);
        snprintf(key, sizeof(key), "t%d", ai);
        if (getParam(query, key, val, sizeof(val))) step.assignments[ai].targetTemp = atof(val);
    }

    saveSettings();
    SerialUSB.print(F("[web] Profile ")); SerialUSB.print(p);
    SerialUSB.print(F(" step ")); SerialUSB.print(si);
    SerialUSB.print(F(": na=")); SerialUSB.print(step.numAssignments);
    SerialUSB.print(step.coolMode ? F(" COOL") : F(" HEAT"));
    SerialUSB.print(F(" hold=")); SerialUSB.println(step.holdMinutes);
    // Confirm with stored values
    char json[80];
    int jn = snprintf(json, sizeof(json),
        "{\"ok\":true,\"s\":%d,\"na\":%d,\"hold\":%d}",
        si, step.numAssignments, step.holdMinutes);
    sendJSON(connId, json, jn);
}

static void handleStart(int connId) {
    startProfile();
    sendOK(connId);
}

static void handleStop(int connId) {
    stopProfile();
    sendOK(connId);
}

// ─── Run Log Handlers ──────────────────────────────────────────────────────

static const char* logEventName(LogEventType t) {
    switch (t) {
        case LOG_RUN_START:   return "RUN_START";
        case LOG_RUN_STOP:    return "RUN_STOP";
        case LOG_RUN_DONE:    return "RUN_DONE";
        case LOG_STEP_START:  return "STEP_START";
        case LOG_TARGET_HIT:  return "TARGET_HIT";
        case LOG_HOLD_DONE:   return "HOLD_DONE";
        case LOG_TEMP_CHANGE: return "TEMP";
        case LOG_ESTOP:       return "ESTOP";
        default:              return "?";
    }
}

// GET /api/log?which=active|last  → JSON summary + entries
static void handleLogGet(int connId, const char* query) {
    char which[8] = "active";
    getParam(query, "which", which, sizeof(which));

    const LogEntry* entries;
    uint16_t count = 0;
    RunLogHeader hdr;

    bool isActive = (strcmp(which, "last") != 0);
    if (isActive) {
        entries = getActiveEntries(count);
        hdr = getActiveRunHeader();
    } else {
        if (!hasLastRun()) {
            const char* j = "{\"ok\":false,\"msg\":\"No saved run\"}";
            sendJSON(connId, j, strlen(j));
            return;
        }
        entries = getLastRunEntries(count);
        hdr = getLastRunHeader();
    }

    // Build JSON: {"ok":true,"logging":T/F,"dur":N,"steps":N,"count":N,"entries":[...]}
    // We'll build it in chunks since it can be large
    char buf[512];
    int pos = snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"logging\":%s,\"dur\":%lu,\"steps\":%d,\"count\":%d,\"entries\":[",
        isLogging() ? "true" : "false",
        (unsigned long)hdr.durationSec,
        hdr.numSteps,
        count);

    // Calculate total content length first
    // Each entry: {"t":N,"e":"NAME","x":N,"v":NNN.N},  ~40 chars max
    // We'll estimate and use chunked sending instead
    // Actually, let's just stream the whole response

    // First, build all entries into a big buffer
    // With 500 entries × ~40 chars = ~20KB — too big for RAM
    // Instead, send header without Content-Length and close after

    // Send HTTP header (no content-length, connection: close)
    char httpHdr[128];
    int hLen = snprintf(httpHdr, sizeof(httpHdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n\r\n");
    espSendChunk(connId, httpHdr, hLen);

    // Send JSON opening
    espSendChunk(connId, buf, pos);

    // Send entries in batches — pack multiple entries into a buffer
    char batch[512];
    int bp = 0;
    for (uint16_t i = 0; i < count; i++) {
        bp += snprintf(batch + bp, sizeof(batch) - bp,
            "%s{\"t\":%u,\"e\":%d,\"x\":%d,\"v\":",
            i > 0 ? "," : "",
            entries[i].secOffset,
            (int)entries[i].type,
            entries[i].extra);
        bp += fmtFloat(batch + bp, sizeof(batch) - bp, entries[i].value, 1);
        batch[bp++] = '}';
        // Flush when buffer is getting full, or last entry
        if (bp > 400 || i == count - 1) {
            if (!espSendChunk(connId, batch, bp)) goto logDone;
            bp = 0;
            yieldCritical();
        }
    }

    // Close JSON
    espSendChunk(connId, "]}", 2);
logDone:
    espClose(connId);
    ipdState = S_IDLE;
}

// GET /api/log/csv?which=active|last  → CSV download
static void handleLogCSV(int connId, const char* query) {
    char which[8] = "active";
    getParam(query, "which", which, sizeof(which));

    const LogEntry* entries;
    uint16_t count = 0;

    bool isActive = (strcmp(which, "last") != 0);
    if (isActive) {
        entries = getActiveEntries(count);
    } else {
        if (!hasLastRun()) {
            send404(connId);
            return;
        }
        entries = getLastRunEntries(count);
    }

    // Send HTTP header for CSV download
    char httpHdr[196];
    int hLen = snprintf(httpHdr, sizeof(httpHdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/csv\r\n"
        "Content-Disposition: attachment; filename=\"runlog.csv\"\r\n"
        "Connection: close\r\n\r\n");
    espSendChunk(connId, httpHdr, hLen);

    // CSV header
    const char* csvHdr = "Time(s),Event,Sensor/Step,Value\r\n";
    espSendChunk(connId, csvHdr, strlen(csvHdr));

    // CSV rows — batch into buffer
    char batch[512];
    int bp = 0;
    for (uint16_t i = 0; i < count; i++) {
        int len = snprintf(batch + bp, sizeof(batch) - bp,
            "%u,%s,%d,",
            entries[i].secOffset,
            logEventName(entries[i].type),
            entries[i].extra);
        bp += len;
        bp += fmtFloat(batch + bp, sizeof(batch) - bp, entries[i].value, 1);
        len = snprintf(batch + bp, sizeof(batch) - bp, "\r\n");
        bp += len;
        if (bp > 400 || i == count - 1) {
            if (!espSendChunk(connId, batch, bp)) break;
            bp = 0;
            yieldCritical();
        }
    }

    espClose(connId);
    ipdState = S_IDLE;
}

// GET /api/log/recent → last 15 entries of active run (compact JSON for dashboard)
static void handleLogRecent(int connId) {
    uint16_t count = 0;
    const LogEntry* entries = getActiveEntries(count);

    // Get last 15 entries
    int start = (count > 15) ? count - 15 : 0;
    int n = count - start;

    char buf[512];
    int pos = snprintf(buf, sizeof(buf), "{\"ok\":true,\"entries\":[");

    for (int i = start; i < (int)count; i++) {
        if (pos > (int)sizeof(buf) - 60) break;  // Safety
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"t\":%u,\"e\":%d,\"x\":%d,\"v\":",
            i > start ? "," : "",
            entries[i].secOffset,
            (int)entries[i].type,
            entries[i].extra);
        pos += fmtFloat(buf + pos, sizeof(buf) - pos, entries[i].value, 1);
        buf[pos++] = '}';
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

    sendJSON(connId, buf, pos);
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
    else if (strcmp(path, "/profiles") == 0) {
        sendHTTPResponse(connId, "text/html", PAGE_PROFILES, strlen(PAGE_PROFILES), 300);
    }
    else if (strcmp(path, "/log") == 0) {
        sendHTTPResponse(connId, "text/html", PAGE_LOG, strlen(PAGE_LOG), 300);
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
    else if (strcmp(path, "/api/profiles") == 0) {
        handleProfilesGet(connId);
    }
    else if (strcmp(path, "/api/profile/get") == 0) {
        handleProfileGetOne(connId, query);
    }
    else if (strcmp(path, "/api/profile/select") == 0) {
        handleProfileSelect(connId, query);
    }
    else if (strcmp(path, "/api/profile/name") == 0) {
        handleProfileName(connId, query);
    }
    else if (strcmp(path, "/api/profile/resize") == 0) {
        handleProfileResize(connId, query);
    }
    else if (strcmp(path, "/api/profile/step") == 0) {
        handleProfileStepSave(connId, query);
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
    else if (strcmp(path, "/api/log") == 0) {
        handleLogGet(connId, query);
    }
    else if (strcmp(path, "/api/log/csv") == 0) {
        handleLogCSV(connId, query);
    }
    else if (strcmp(path, "/api/log/recent") == 0) {
        handleLogRecent(connId);
    }
    else if (strcmp(path, "/api/pintest") == 0) {
        handlePinTest(connId, query);
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

// ─── WiFi Mode Helpers ──────────────────────────────────────────────────────

// Start as WiFi client — returns true if connected
static bool wifiStartSTA(const char* ssid, const char* password) {
    SerialUSB.print(F("[wifi] STA mode — joining: ")); SerialUSB.println(ssid);
    espCmd("AT+CWMODE_DEF=1", 2000);   // Pure station mode (saved to ESP flash)
    delay(200);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP_DEF=\"%s\",\"%s\"", ssid, password);
    if (espCmd(cmd, 15000)) {
        SerialUSB.println(F("[wifi] STA connected!"));
        staFailed = false;
        return true;
    }
    SerialUSB.println(F("[wifi] STA join failed"));
    staFailed = true;
    return false;
}

// Start as WiFi hotspot
static void wifiStartAP() {
    SerialUSB.println(F("[wifi] AP mode — creating hotspot"));
    espCmd("AT+CWMODE_DEF=2", 2000);   // Pure AP mode (saved to ESP flash)
    delay(200);
    espCmd("AT+CWSAP_DEF=\"SmartPID-Still\",\"\",5,0,4,0", 3000);
    strncpy(currentIP, "192.168.4.1", sizeof(currentIP));
}

// Print current IP addresses to serial and cache the active IP
static void wifiPrintIPs() {
    char resp[256];
    if (espCmdResp("AT+CIFSR", resp, sizeof(resp), 3000)) {
        char* line = strtok(resp, "\n");
        while (line) {
            if (strstr(line, "CIFSR")) {
                SerialUSB.print(F("[wifi] ")); SerialUSB.println(line);
                // Extract IP from +CIFSR:STAIP,"x.x.x.x" or +CIFSR:APIP,"x.x.x.x"
                const char* tag = staMode ? "STAIP" : "APIP";
                if (strstr(line, tag)) {
                    char* q1 = strchr(line, '"');
                    if (q1) {
                        q1++;
                        char* q2 = strchr(q1, '"');
                        if (q2) {
                            int len = q2 - q1;
                            if (len > 0 && len < (int)sizeof(currentIP)) {
                                memcpy(currentIP, q1, len);
                                currentIP[len] = '\0';
                            }
                        }
                    }
                }
            }
            line = strtok(NULL, "\n");
        }
    }
}

// Start (or restart) the TCP server
static void wifiStartServer() {
    espCmd("AT+CIPSERVER=0", 2000);    // Stop any existing server
    delay(100);
    espCmd("AT+CIPMUX=1", 2000);       // Multi-connection mode
    if (espCmd("AT+CIPSERVER=1,80", 3000)) {
        SerialUSB.println(F("[wifi] HTTP server started on port 80"));
        wifiReady = true;
    } else {
        SerialUSB.println(F("[wifi] Failed to start server!"));
    }
}

// Check STA connectivity — called periodically
void wifiCheckConnection() {
    if (!wifiReady) return;
    Settings& s = getSettings();

    if (staMode) {
        // In STA mode — verify we're still connected
        char resp[128];
        if (espCmdResp("AT+CIPSTATUS", resp, sizeof(resp), 2000)) {
            // STATUS:5 means "not connected to AP"
            if (strstr(resp, "STATUS:5")) {
                SerialUSB.println(F("[wifi] STA connection lost — switching to AP"));
                staMode = false;
                wifiStartAP();
                wifiStartServer();
                wifiPrintIPs();
            }
        }
    } else {
        // In AP mode — try to reconnect if we have credentials
        // (either in Settings or stored on ESP flash via _DEF)
        if (s.wifi.configured && strlen(s.wifi.ssid) > 0) {
            SerialUSB.println(F("[wifi] Retrying STA connection..."));
            if (wifiStartSTA(s.wifi.ssid, s.wifi.password)) {
                staMode = true;
                wifiStartServer();
                wifiPrintIPs();
            }
        } else {
            // Settings cleared by reflash — check if ESP has stored credentials
            // by trying AT+CWJAP_DEF (switch to STA and use saved creds)
            SerialUSB.println(F("[wifi] Trying ESP stored credentials..."));
            espCmd("AT+CWMODE_DEF=1", 2000);
            delay(3000); // give ESP time to auto-connect
            char cwjap[128];
            if (espCmdResp("AT+CWJAP?", cwjap, sizeof(cwjap), 3000)) {
                char* p = strstr(cwjap, "+CWJAP:\"");
                if (p) {
                    p += 8;
                    char* q = strchr(p, '"');
                    if (q && q > p) {
                        int len = q - p;
                        if (len < (int)sizeof(s.wifi.ssid)) {
                            memcpy(s.wifi.ssid, p, len);
                            s.wifi.ssid[len] = '\0';
                            s.wifi.configured = true;
                            SerialUSB.print(F("[wifi] Reconnected via ESP creds: "));
                            SerialUSB.println(s.wifi.ssid);
                            staMode = true;
                            staFailed = false;
                            wifiStartServer();
                            wifiPrintIPs();
                            return;
                        }
                    }
                }
            }
            // ESP stored creds didn't work — go back to AP
            wifiStartAP();
            wifiStartServer();
            wifiPrintIPs();
        }
    }
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool isWiFiReady() {
    return wifiReady;
}

bool isWiFiSTA() {
    return staMode;
}

bool isWiFiSTAFailed() {
    return staFailed;
}

const char* getWiFiIP() {
    return currentIP;
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

    // Try to connect as STA (client), fall back to AP (hotspot)
    Settings& s = getSettings();
    staMode = false;

    // Check if ESP already connected (credentials saved in ESP flash via _DEF)
    char cwjap[128];
    if (espCmdResp("AT+CWJAP?", cwjap, sizeof(cwjap), 3000)) {
        // Response like: +CWJAP:"MyNetwork","aa:bb:cc:dd:ee:ff",6,-50\r\nOK
        char* p = strstr(cwjap, "+CWJAP:\"");
        if (p) {
            p += 8; // skip '+CWJAP:"'
            char* q = strchr(p, '"');
            if (q && q > p) {
                int len = q - p;
                if (len < (int)sizeof(s.wifi.ssid)) {
                    memcpy(s.wifi.ssid, p, len);
                    s.wifi.ssid[len] = '\0';
                    s.wifi.configured = true;
                    SerialUSB.print(F("[wifi] ESP already connected to: "));
                    SerialUSB.println(s.wifi.ssid);
                    staMode = true;
                    staFailed = false;
                }
            }
        }
    }

    // If ESP wasn't already connected, try with Settings credentials
    if (!staMode && s.wifi.configured && strlen(s.wifi.ssid) > 0) {
        staMode = wifiStartSTA(s.wifi.ssid, s.wifi.password);
    }
    if (!staMode) {
        wifiStartAP();
    }

    wifiPrintIPs();
    wifiStartServer();
}

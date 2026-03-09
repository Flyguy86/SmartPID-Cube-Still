#include "webui.h"
#include "config.h"
#include "controller.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static AsyncWebServer server(80);
static AsyncEventSource events("/events");

// ─── HTML UI (embedded) ─────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SmartPID CUBE — Still Controller</title>
<style>
  :root { --bg: #1a1a2e; --card: #16213e; --accent: #0f3460; --hi: #e94560; --txt: #eee; --ok: #4ecca3; }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--txt); padding: 16px; }
  h1 { text-align: center; margin-bottom: 16px; color: var(--hi); font-size: 1.5em; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; max-width: 960px; margin: 0 auto; }
  @media (max-width: 600px) { .grid { grid-template-columns: 1fr; } }
  .card { background: var(--card); border-radius: 12px; padding: 20px; box-shadow: 0 4px 20px rgba(0,0,0,.3); }
  .card h2 { color: var(--hi); font-size: 1.1em; margin-bottom: 12px; border-bottom: 1px solid var(--accent); padding-bottom: 8px; }
  .temp-display { font-size: 2.5em; font-weight: bold; text-align: center; padding: 10px; }
  .temp-display.ok { color: var(--ok); }
  .temp-display.warn { color: var(--hi); }
  .temp-display.off { color: #555; }
  label { display: block; margin: 8px 0 4px; font-size: 0.85em; color: #aaa; }
  input[type=number], select { width: 100%; padding: 8px; border: 1px solid var(--accent); border-radius: 6px;
    background: var(--bg); color: var(--txt); font-size: 1em; }
  select { cursor: pointer; }
  .row { display: flex; gap: 8px; margin-top: 6px; }
  .row > * { flex: 1; }
  button { padding: 10px 16px; border: none; border-radius: 6px; font-size: 0.9em; cursor: pointer;
    font-weight: bold; transition: opacity 0.2s; }
  button:hover { opacity: 0.85; }
  .btn-enable { background: var(--ok); color: #000; }
  .btn-disable { background: var(--hi); color: #fff; }
  .btn-autotune { background: #e2a03f; color: #000; }
  .btn-save { background: var(--accent); color: var(--txt); }
  .btn-cancel { background: #555; color: var(--txt); }
  .status { font-size: 0.8em; color: #aaa; margin-top: 8px; text-align: center; }
  .pid-info { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4px; margin-top: 8px;
    font-size: 0.8em; text-align: center; color: #aaa; }
  .pid-info span { background: var(--bg); border-radius: 4px; padding: 4px; }
  .footer { text-align: center; margin-top: 20px; font-size: 0.75em; color: #555; }
  .badge { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 0.75em; font-weight: bold; }
  .badge.on { background: var(--ok); color: #000; }
  .badge.off { background: #555; color: #999; }
  .badge.tuning { background: #e2a03f; color: #000; animation: pulse 1s infinite; }
  .badge.onoff { background: #5dade2; color: #000; }
  @keyframes pulse { 0%,100% { opacity: 1; } 50% { opacity: 0.5; } }
  .output-bar { height: 6px; background: var(--bg); border-radius: 3px; margin-top: 8px; overflow: hidden; }
  .output-bar .fill { height: 100%; background: var(--ok); transition: width 0.5s; border-radius: 3px; }
  details { margin-top: 10px; }
  summary { cursor: pointer; color: var(--hi); font-size: 0.85em; font-weight: bold; padding: 4px 0; }
  summary:hover { opacity: 0.8; }
  .cfg-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-top: 6px; }
  .cfg-grid label { margin: 0; font-size: 0.75em; }
  .cfg-grid input, .cfg-grid select { font-size: 0.85em; padding: 6px; }
  .mode-onoff-fields { display: none; margin-top: 6px; }
  .mode-onoff-fields.visible { display: block; }
  .mode-pid-fields { margin-top: 6px; }
  .mode-pid-fields.hidden { display: none; }
  .toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: var(--ok); color: #000;
    padding: 10px 24px; border-radius: 8px; font-weight: bold; font-size: 0.9em; opacity: 0; transition: opacity 0.3s;
    pointer-events: none; z-index: 1000; }
  .toast.show { opacity: 1; }
  .sd-badge { display: inline-block; padding: 2px 6px; border-radius: 4px; font-size: 0.7em; margin-left: 4px; }
  .sd-badge.present { background: var(--ok); color: #000; }
  .sd-badge.absent { background: #555; color: #999; }
</style>
</head>
<body>
<h1>&#127942; SmartPID CUBE — Still Controller</h1>
<div class="grid">

<!-- Channel 0: Boiler -->
<div class="card" id="ch0">
  <h2>Boiler (Ch 0)
    <span class="badge off" id="ch0_badge">OFF</span>
  </h2>
  <div class="temp-display off" id="ch0_temp">--.-&deg;C</div>
  <div class="output-bar"><div class="fill" id="ch0_bar" style="width:0%"></div></div>
  <div class="pid-info">
    <span>Kp: <b id="ch0_kp_disp">--</b></span>
    <span>Ki: <b id="ch0_ki_disp">--</b></span>
    <span>Kd: <b id="ch0_kd_disp">--</b></span>
  </div>
  <div class="status">Output: <b id="ch0_out">0</b>% | SP: <b id="ch0_sp_disp">--</b>&deg;C | <span id="ch0_outname">--</span></div>

  <label>Control Mode</label>
  <select id="ch0_mode" onchange="modeChanged(0)">
    <option value="0">PID (PWM)</option>
    <option value="1">ON/OFF (Hysteresis)</option>
  </select>

  <label>Output Assignment</label>
  <select id="ch0_outsel">
    <option value="0">Relay 1 (10A)</option>
    <option value="1">Relay 2 (10A)</option>
    <option value="2" selected>SSR</option>
    <option value="3">DC Out 1 (12V)</option>
    <option value="4">DC Out 2 (12V)</option>
    <option value="255">None</option>
  </select>

  <label>Setpoint (&deg;C)</label>
  <input type="number" id="ch0_sp" step="0.5" value="78.0" min="0" max="200">

  <div class="mode-onoff-fields" id="ch0_onoff_fields">
    <label>Hysteresis Band (&plusmn;&deg;C)</label>
    <input type="number" id="ch0_hyst" step="0.1" min="0.1" max="20" value="1.0">
  </div>

  <div class="mode-pid-fields" id="ch0_pid_fields">
    <label>PID Parameters</label>
    <div class="row">
      <input type="number" id="ch0_kp" step="0.1" placeholder="Kp">
      <input type="number" id="ch0_ki" step="0.1" placeholder="Ki">
      <input type="number" id="ch0_kd" step="0.1" placeholder="Kd">
    </div>
  </div>

  <div class="row" style="margin-top:12px">
    <button class="btn-enable" onclick="setEnabled(0,true)">Enable</button>
    <button class="btn-disable" onclick="setEnabled(0,false)">Disable</button>
  </div>
  <div class="row">
    <button class="btn-autotune" onclick="startAutotune(0)">Auto-Tune</button>
    <button class="btn-cancel" onclick="cancelAutotune(0)">Cancel Tune</button>
  </div>
  <div class="row">
    <button class="btn-save" onclick="saveParams(0)">Apply &amp; Save</button>
  </div>
  <details>
    <summary>&#9881; Advanced / Autotune Config</summary>
    <label>Autotune Output Step (0–255)</label>
    <input type="number" id="ch0_at_step" step="1" min="1" max="255" value="127">
    <div class="cfg-grid">
      <div><label>Noise Band (&deg;C)</label>
      <input type="number" id="ch0_at_nb" step="0.1" min="0.1" max="10" value="0.5"></div>
      <div><label>Lookback (sec)</label>
      <input type="number" id="ch0_at_lb" step="1" min="5" max="300" value="30"></div>
      <div><label>Max Cycles</label>
      <input type="number" id="ch0_at_mc" step="1" min="3" max="50" value="10"></div>
      <div><label>SSR Window (ms)</label>
      <input type="number" id="ch0_ssrw" step="500" min="1000" max="30000" value="5000"></div>
    </div>
  </details>
</div>

<!-- Channel 1: Column -->
<div class="card" id="ch1">
  <h2>Column (Ch 1)
    <span class="badge off" id="ch1_badge">OFF</span>
  </h2>
  <div class="temp-display off" id="ch1_temp">--.-&deg;C</div>
  <div class="output-bar"><div class="fill" id="ch1_bar" style="width:0%"></div></div>
  <div class="pid-info">
    <span>Kp: <b id="ch1_kp_disp">--</b></span>
    <span>Ki: <b id="ch1_ki_disp">--</b></span>
    <span>Kd: <b id="ch1_kd_disp">--</b></span>
  </div>
  <div class="status">Output: <b id="ch1_out">0</b>% | SP: <b id="ch1_sp_disp">--</b>&deg;C | <span id="ch1_outname">--</span></div>

  <label>Control Mode</label>
  <select id="ch1_mode" onchange="modeChanged(1)">
    <option value="0">PID (PWM)</option>
    <option value="1">ON/OFF (Hysteresis)</option>
  </select>

  <label>Output Assignment</label>
  <select id="ch1_outsel">
    <option value="0">Relay 1 (10A)</option>
    <option value="1">Relay 2 (10A)</option>
    <option value="2">SSR</option>
    <option value="3">DC Out 1 (12V)</option>
    <option value="4">DC Out 2 (12V)</option>
    <option value="255" selected>None</option>
  </select>

  <label>Setpoint (&deg;C)</label>
  <input type="number" id="ch1_sp" step="0.5" value="20.0" min="0" max="200">

  <div class="mode-onoff-fields" id="ch1_onoff_fields">
    <label>Hysteresis Band (&plusmn;&deg;C)</label>
    <input type="number" id="ch1_hyst" step="0.1" min="0.1" max="20" value="1.0">
  </div>

  <div class="mode-pid-fields" id="ch1_pid_fields">
    <label>PID Parameters</label>
    <div class="row">
      <input type="number" id="ch1_kp" step="0.1" placeholder="Kp">
      <input type="number" id="ch1_ki" step="0.1" placeholder="Ki">
      <input type="number" id="ch1_kd" step="0.1" placeholder="Kd">
    </div>
  </div>

  <div class="row" style="margin-top:12px">
    <button class="btn-enable" onclick="setEnabled(1,true)">Enable</button>
    <button class="btn-disable" onclick="setEnabled(1,false)">Disable</button>
  </div>
  <div class="row">
    <button class="btn-autotune" onclick="startAutotune(1)">Auto-Tune</button>
    <button class="btn-cancel" onclick="cancelAutotune(1)">Cancel Tune</button>
  </div>
  <div class="row">
    <button class="btn-save" onclick="saveParams(1)">Apply &amp; Save</button>
  </div>
  <details>
    <summary>&#9881; Advanced / Autotune Config</summary>
    <label>Autotune Output Step (0–255)</label>
    <input type="number" id="ch1_at_step" step="1" min="1" max="255" value="127">
    <div class="cfg-grid">
      <div><label>Noise Band (&deg;C)</label>
      <input type="number" id="ch1_at_nb" step="0.1" min="0.1" max="10" value="0.5"></div>
      <div><label>Lookback (sec)</label>
      <input type="number" id="ch1_at_lb" step="1" min="5" max="300" value="30"></div>
      <div><label>Max Cycles</label>
      <input type="number" id="ch1_at_mc" step="1" min="3" max="50" value="10"></div>
      <div><label>SSR Window (ms)</label>
      <input type="number" id="ch1_ssrw" step="500" min="1000" max="30000" value="5000"></div>
    </div>
  </details>
</div>

</div>

<div class="toast" id="toast"></div>

<div class="footer" id="footer">Uptime: 0s | WiFi: -- | SD: <span class="sd-badge absent" id="sd_badge">N/A</span></div>

<script>
const OUT_NAMES = ['Relay 1','Relay 2','SSR','DC Out 1','DC Out 2'];

function modeChanged(ch) {
  const pre = 'ch' + ch;
  const mode = parseInt(document.getElementById(pre + '_mode').value);
  document.getElementById(pre + '_onoff_fields').className = 'mode-onoff-fields' + (mode === 1 ? ' visible' : '');
  document.getElementById(pre + '_pid_fields').className = 'mode-pid-fields' + (mode === 0 ? '' : ' hidden');
}

const evtSource = new EventSource('/events');
evtSource.addEventListener('state', function(e) {
  const d = JSON.parse(e.data);
  for (let i = 0; i < 2; i++) {
    const ch = d.ch[i];
    const pre = 'ch' + i;
    const tempEl = document.getElementById(pre + '_temp');
    const t = ch.temp;

    if (t === null || isNaN(t)) {
      tempEl.textContent = 'ERR';
      tempEl.className = 'temp-display off';
    } else {
      tempEl.textContent = t.toFixed(1) + '\u00B0C';
      tempEl.className = 'temp-display ' + (ch.enabled ? (Math.abs(t - ch.sp) < 2 ? 'ok' : 'warn') : 'off');
    }

    const pct = (ch.output / 255 * 100).toFixed(0);
    document.getElementById(pre + '_out').textContent = pct;
    document.getElementById(pre + '_bar').style.width = pct + '%';
    document.getElementById(pre + '_sp_disp').textContent = ch.sp.toFixed(1);
    document.getElementById(pre + '_kp_disp').textContent = ch.kp.toFixed(2);
    document.getElementById(pre + '_ki_disp').textContent = ch.ki.toFixed(2);
    document.getElementById(pre + '_kd_disp').textContent = ch.kd.toFixed(2);

    // Output assignment display
    const outIdx = ch.outid;
    document.getElementById(pre + '_outname').textContent = (outIdx < 5) ? OUT_NAMES[outIdx] : 'None';

    // Badge: mode-aware
    const badge = document.getElementById(pre + '_badge');
    if (ch.autotuning) { badge.textContent = 'TUNING'; badge.className = 'badge tuning'; }
    else if (ch.enabled && ch.mode === 1) { badge.textContent = 'ON/OFF'; badge.className = 'badge onoff'; }
    else if (ch.enabled) { badge.textContent = 'PID'; badge.className = 'badge on'; }
    else { badge.textContent = 'OFF'; badge.className = 'badge off'; }

    // Update input fields only if user hasn't focused them
    if (document.activeElement?.id !== pre + '_sp') document.getElementById(pre + '_sp').value = ch.sp;
    if (document.activeElement?.id !== pre + '_kp') document.getElementById(pre + '_kp').value = ch.kp;
    if (document.activeElement?.id !== pre + '_ki') document.getElementById(pre + '_ki').value = ch.ki;
    if (document.activeElement?.id !== pre + '_kd') document.getElementById(pre + '_kd').value = ch.kd;
    if (document.activeElement?.id !== pre + '_hyst') document.getElementById(pre + '_hyst').value = ch.hyst;
    if (document.activeElement?.id !== pre + '_mode') {
      document.getElementById(pre + '_mode').value = ch.mode;
      modeChanged(i);
    }
    if (document.activeElement?.id !== pre + '_outsel') document.getElementById(pre + '_outsel').value = ch.outid;
    if (document.activeElement?.id !== pre + '_at_step') document.getElementById(pre + '_at_step').value = ch.at_step;
    if (document.activeElement?.id !== pre + '_at_nb') document.getElementById(pre + '_at_nb').value = ch.at_nb;
    if (document.activeElement?.id !== pre + '_at_lb') document.getElementById(pre + '_at_lb').value = ch.at_lb;
    if (document.activeElement?.id !== pre + '_at_mc') document.getElementById(pre + '_at_mc').value = ch.at_mc;
    if (document.activeElement?.id !== pre + '_ssrw') document.getElementById(pre + '_ssrw').value = ch.ssrw;
  }
  const sdBadge = document.getElementById('sd_badge');
  if (d.sd) { sdBadge.textContent = d.sdlog ? 'Logging' : 'Ready'; sdBadge.className = 'sd-badge present'; }
  else { sdBadge.textContent = 'No Card'; sdBadge.className = 'sd-badge absent'; }
  document.getElementById('footer').innerHTML =
    'Uptime: ' + d.uptime + 's | WiFi: ' + (d.wifi ? 'Connected' : 'AP Mode') +
    ' | SD: <span class="sd-badge ' + (d.sd ? 'present' : 'absent') + '">' +
    (d.sd ? (d.sdlog ? 'Logging' : 'Ready') : 'No Card') + '</span>';
});

function api(url, body) {
  fetch(url, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
    .then(r => r.json()).then(d => {
      if (d.ok) { showToast('Saved!'); } else { alert(d.error || 'Error'); }
    });
}
function showToast(msg) {
  const t = document.getElementById('toast'); t.textContent = msg;
  t.classList.add('show'); setTimeout(() => t.classList.remove('show'), 1500);
}
function setEnabled(ch, en)    { api('/api/enable',    { channel: ch, enabled: en }); }
function startAutotune(ch)     { api('/api/autotune',  { channel: ch, action: 'start' }); }
function cancelAutotune(ch)    { api('/api/autotune',  { channel: ch, action: 'cancel' }); }
function saveParams(ch) {
  const pre = 'ch' + ch;
  api('/api/config', {
    channel: ch,
    setpoint: parseFloat(document.getElementById(pre + '_sp').value),
    kp: parseFloat(document.getElementById(pre + '_kp').value),
    ki: parseFloat(document.getElementById(pre + '_ki').value),
    kd: parseFloat(document.getElementById(pre + '_kd').value),
    mode: parseInt(document.getElementById(pre + '_mode').value),
    hyst: parseFloat(document.getElementById(pre + '_hyst').value),
    outid: parseInt(document.getElementById(pre + '_outsel').value),
    at_step: parseFloat(document.getElementById(pre + '_at_step').value),
    at_nb:   parseFloat(document.getElementById(pre + '_at_nb').value),
    at_lb:   parseInt(document.getElementById(pre + '_at_lb').value),
    at_mc:   parseInt(document.getElementById(pre + '_at_mc').value),
    ssrw:    parseInt(document.getElementById(pre + '_ssrw').value)
  });
}
</script>
</body>
</html>
)rawliteral";

// ─── SSE State Broadcasting ────────────────────────────────────────────────
static unsigned long lastEventTime = 0;

static String buildStateJson() {
    JsonDocument doc;
    JsonArray chArr = doc["ch"].to<JsonArray>();

    for (int i = 0; i < 2; i++) {
        JsonObject ch = chArr.add<JsonObject>();
        if (isnan(gState.ch[i].temperature)) {
            ch["temp"] = nullptr;
        } else {
            ch["temp"] = gState.ch[i].temperature;
        }
        ch["sp"] = gState.ch[i].setpoint;
        ch["output"] = gState.ch[i].pidOutput;
        ch["kp"] = gState.ch[i].params.Kp;
        ch["ki"] = gState.ch[i].params.Ki;
        ch["kd"] = gState.ch[i].params.Kd;
        ch["enabled"] = gState.ch[i].enabled;
        ch["autotuning"] = gState.ch[i].autotuning;
        ch["mode"]    = (int)gState.ch[i].mode;
        ch["hyst"]    = gState.ch[i].hysteresis;
        ch["outid"]   = (int)gState.ch[i].assignedOutput;
        ch["at_step"] = gState.ch[i].atCfg.outputStep;
        ch["at_nb"]   = gState.ch[i].atCfg.noiseBand;
        ch["at_lb"]   = gState.ch[i].atCfg.lookbackSec;
        ch["at_mc"]   = gState.ch[i].atCfg.maxCycles;
        ch["ssrw"]    = gState.ch[i].ssrWindowMs;
    }

    doc["uptime"] = gState.uptime;
    doc["wifi"] = gState.wifiConnected;
    doc["sd"] = gState.sdCardPresent;
    doc["sdlog"] = gState.sdLogging;

    String json;
    serializeJson(doc, json);
    return json;
}

void webui_init() {
    // --- Serve HTML ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    // --- API: Get State ---
    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", buildStateJson());
    });

    // --- Helper: collect POST body then parse JSON ---
    // We use onBody + onRequest pattern since esphome fork lacks AsyncCallbackJsonWebHandler

    // --- API: Set Config (setpoint + PID params) ---
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        // Body already collected, parse here
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, (const char*)data, len);
        if (err) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
            return;
        }
        int ch = doc["channel"] | -1;
        if (ch < 0 || ch > 1) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
            return;
        }
        if (doc["setpoint"].is<double>()) {
            controller_setSetpoint(ch, doc["setpoint"].as<double>());
        }
        if (doc["kp"].is<double>() && doc["ki"].is<double>() && doc["kd"].is<double>()) {
            controller_setPIDParams(ch, doc["kp"].as<double>(), doc["ki"].as<double>(), doc["kd"].as<double>());
        }
        if (doc["mode"].is<int>()) {
            controller_setMode(ch, (ControlMode)(doc["mode"].as<int>()));
        }
        if (doc["hyst"].is<double>()) {
            controller_setHysteresis(ch, doc["hyst"].as<double>());
        }
        if (doc["outid"].is<int>()) {
            controller_setOutput(ch, (OutputID)(doc["outid"].as<int>()));
        }
        if (doc["at_step"].is<double>()) {
            controller_setAutotuneConfig(ch,
                doc["at_step"].as<double>(),
                doc["at_nb"] | gState.ch[ch].atCfg.noiseBand,
                doc["at_lb"] | gState.ch[ch].atCfg.lookbackSec,
                doc["at_mc"] | gState.ch[ch].atCfg.maxCycles);
        }
        if (doc["ssrw"].is<int>()) {
            controller_setSSRWindow(ch, doc["ssrw"].as<unsigned long>());
        }
        controller_saveConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // --- API: Enable/Disable ---
    server.on("/api/enable", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, (const char*)data, len);
        if (err) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
            return;
        }
        int ch = doc["channel"] | -1;
        if (ch < 0 || ch > 1) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
            return;
        }
        controller_setEnabled(ch, doc["enabled"] | false);
        controller_saveConfig();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // --- API: Autotune ---
    server.on("/api/autotune", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, (const char*)data, len);
        if (err) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
            return;
        }
        int ch = doc["channel"] | -1;
        if (ch < 0 || ch > 1) {
            request->send(400, "application/json", "{\"ok\":false,\"error\":\"bad channel\"}");
            return;
        }
        String action = doc["action"] | "start";
        if (action == "start") {
            controller_startAutotune(ch);
        } else {
            controller_cancelAutotune(ch);
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // --- SSE Events ---
    events.onConnect([](AsyncEventSourceClient *client) {
        client->send(buildStateJson().c_str(), "state", millis());
    });
    server.addHandler(&events);

    server.begin();
    Serial.println("[WEB] Server started on port 80");
}

void webui_update() {
    unsigned long now = millis();
    // Send SSE update every 1 second
    if (now - lastEventTime >= 1000) {
        lastEventTime = now;
        events.send(buildStateJson().c_str(), "state", now);
    }
}

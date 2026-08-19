#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <math.h>
#include "WavinController.h"

#if __has_include("config.h")
#include "config.h"
#else
#include "config.h.example"
#endif

ESP8266WebServer server(80);
WavinController wavin(0, false, 1000);

#ifndef SETPOINT_STEP_TENTHS
#define SETPOINT_STEP_TENTHS 5
#endif

#ifndef SETPOINT_MIN_TENTHS
#define SETPOINT_MIN_TENTHS 50
#endif

#ifndef SETPOINT_MAX_TENTHS
#define SETPOINT_MAX_TENTHS 350
#endif

#ifndef SETPOINT_DEBOUNCE_MS
#define SETPOINT_DEBOUNCE_MS 800
#endif

struct ChannelData {
  uint8_t ch;
  String name;
  float temp;
  float target;
  float standby;
  int battery;
  bool heating;
  uint8_t mode;
  bool active;
};

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "\\r");
  value.replace("\t", "\\t");
  return value;
}

String getDefaultChannelName(uint8_t ch) {
  return "Kanal " + String(ch + 1);
}

String getChannelName(uint8_t ch) {
  if (ch < WavinController::NUMBER_OF_CHANNELS && CHANNEL_NAMES[ch] != nullptr && CHANNEL_NAMES[ch][0] != '\0') {
    return String(CHANNEL_NAMES[ch]);
  }

  return getDefaultChannelName(ch);
}

String getModeLabel(uint8_t mode) {
  if (mode == WavinController::PACKED_DATA_CONFIGURATION_MODE_STANDBY) {
    return "Standby";
  }

  return "Manuel";
}

String getHeatLabel(bool heating) {
  return heating ? "🔥 Varme" : "Sluk";
}

String getHeatBadgeClass(bool heating) {
  return heating ? "badge-heat" : "badge-off";
}

int clampSetpointTenths(int value) {
  if (value < SETPOINT_MIN_TENTHS) {
    return SETPOINT_MIN_TENTHS;
  }

  if (value > SETPOINT_MAX_TENTHS) {
    return SETPOINT_MAX_TENTHS;
  }

  return value;
}

bool readChannelData(uint8_t ch, ChannelData &data) {
  uint16_t primaryReg[1];
  data.active = false;

  if (!wavin.readRegisters(WavinController::CATEGORY_CHANNELS, ch, WavinController::CHANNELS_PRIMARY_ELEMENT, 1, primaryReg)) {
    return false;
  }

  uint8_t element = primaryReg[0] & WavinController::CHANNELS_PRIMARY_ELEMENT_ELEMENT_MASK;
  if (element == 0) {
    return true;
  }

  uint16_t elementReg[11];
  uint16_t targetReg[1];
  uint16_t standbyReg[1];
  uint16_t timerReg[1];
  uint16_t configReg[1];

  if (!wavin.readRegisters(WavinController::CATEGORY_ELEMENTS, element - 1, 0, 11, elementReg) ||
      !wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_MANUAL_TEMPERATURE, 1, targetReg) ||
      !wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_STANDBY_TEMPERATURE, 1, standbyReg) ||
      !wavin.readRegisters(WavinController::CATEGORY_CHANNELS, ch, WavinController::CHANNELS_TIMER_EVENT, 1, timerReg) ||
      !wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_CONFIGURATION, 1, configReg)) {
    return false;
  }

  data.ch = ch;
  data.name = getChannelName(ch);
  data.temp = elementReg[WavinController::ELEMENTS_AIR_TEMPERATURE] / 10.0;
  data.target = targetReg[0] / 10.0;
  data.standby = standbyReg[0] / 10.0;
  data.battery = elementReg[WavinController::ELEMENTS_BATTERY_STATUS] * 10;
  data.heating = (timerReg[0] & WavinController::CHANNELS_TIMER_EVENT_OUTP_ON_MASK) != 0;
  data.mode = configReg[0] & WavinController::PACKED_DATA_CONFIGURATION_MODE_MASK;
  data.active = true;
  return true;
}

void appendChannelCard(String &html, const ChannelData &data) {
  String channelId = String(data.ch);

  html += "<div class='card'><div class='row'><div><b id='name-" + channelId + "'>" + htmlEscape(data.name) + "</b><br><span id='temp-" + channelId + "'>" + String(data.temp, 1) + "°C</span></div>";
  html += "<div class='badges'><span class='badge " + getHeatBadgeClass(data.heating) + "' id='heat-" + channelId + "'>" + getHeatLabel(data.heating) + "</span>";
  html += "<span class='badge badge-mode' id='mode-" + channelId + "' onclick='toggleMode(" + channelId + ")' style='cursor:pointer'>" + getModeLabel(data.mode) + "</span></div>";
  html += "<div class='battery'>Batteri: <span id='battery-" + channelId + "'>" + String(data.battery) + "%</span></div></div>";
  html += "<div class='controls'><button class='btn' onclick='adjust(" + channelId + ",-" + String(SETPOINT_STEP_TENTHS) + ")'>-</button>";
  html += "<span class='target' id='target-" + channelId + "' data-target-tenths='" + String((int)lroundf(data.target * 10.0f)) + "'>" + String(data.target, 1) + "°C</span>";
  html += "<button class='btn' onclick='adjust(" + channelId + "," + String(SETPOINT_STEP_TENTHS) + ")'>+</button>";
  html += "<div class='standby'>Standby: <span id='standby-" + channelId + "'>" + String(data.standby, 1) + "°C</span></div></div></div></div>";
}

void handleData() {
  String json;
  json.reserve(2048);
  json = "[";
  bool first = true;

  for (uint8_t ch = 0; ch < WavinController::NUMBER_OF_CHANNELS; ch++) {
    ChannelData data;
    if (!readChannelData(ch, data) || !data.active) {
      continue;
    }

    if (!first) {
      json += ",";
    }

    json += "{\"ch\":" + String(data.ch);
    json += ",\"name\":\"" + jsonEscape(data.name) + "\"";
    json += ",\"temp\":" + String(data.temp, 1);
    json += ",\"target\":" + String(data.target, 1);
    json += ",\"standby\":" + String(data.standby, 1);
    json += ",\"battery\":" + String(data.battery);
    json += ",\"heating\":" + String(data.heating ? "true" : "false");
    json += ",\"mode\":" + String(data.mode);
    json += "}";
    first = false;
  }

  json += "]";
  server.send(200, "application/json", json);
}

void handleRoot() {
  uint16_t hwReg[1] = {0}, swReg[1] = {0}, nameReg[1] = {0};
  bool hwOk   = wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x02, 1, hwReg);
  bool swOk   = wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x03, 1, swReg);
  bool nameOk = wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x04, 1, nameReg);

  String html;
  html.reserve(12000);
  html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
  html += "body { font-family: -apple-system, sans-serif; background: #f0f2f5; padding: 15px; }";
  html += ".card { background: white; border-radius: 12px; padding: 15px; margin-bottom: 12px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".row { display: flex; justify-content: space-between; align-items: center; gap: 12px; }";
  html += ".controls { text-align: right; }";
  html += ".btn { background: #007bff; color: white; border: none; padding: 10px 18px; border-radius: 8px; font-size: 18px; cursor: pointer; }";
  html += ".target { display: inline-block; margin: 0 15px; font-weight: bold; }";
  html += ".badges { display: flex; gap: 8px; margin-top: 8px; flex-wrap: wrap; }";
  html += ".badge { display: inline-block; padding: 4px 8px; border-radius: 999px; font-size: 0.8em; font-weight: 600; }";
  html += ".badge-heat { background: #f8d7da; color: #b02a37; }";
  html += ".badge-off { background: #e9ecef; color: #6c757d; }";
  html += ".badge-mode { background: #dbeafe; color: #1d4ed8; }";
  html += ".battery, .standby { font-size: 0.8em; color: #666; margin-top: 5px; }";
  html += ".footer { background: #dee2e6; padding: 15px; border-radius: 12px; font-size: 0.85em; color: #444; }";
  html += "</style><script>";
  html += "var SETPOINT_MIN_TENTHS = " + String(SETPOINT_MIN_TENTHS) + ";";
  html += "var SETPOINT_MAX_TENTHS = " + String(SETPOINT_MAX_TENTHS) + ";";
  html += "var SETPOINT_DEBOUNCE_MS = " + String(SETPOINT_DEBOUNCE_MS) + ";";
  html += "var pendingSetpoints = {};";
  html += "var pendingSetpointTimers = {};";
  html += "var pendingModes = {};";
  html += "function formatTemp(value) { return Number(value).toFixed(1) + '°C'; }";
  html += "function parseTempTenths(value) { var number = parseFloat(String(value).replace('°C', '')); return isNaN(number) ? 0 : Math.round(number * 10); }";
  html += "function clampTargetTenths(value) { if (value < SETPOINT_MIN_TENTHS) return SETPOINT_MIN_TENTHS; if (value > SETPOINT_MAX_TENTHS) return SETPOINT_MAX_TENTHS; return value; }";
  html += "function setTargetValue(ch, tenths) { var el = document.getElementById('target-' + ch); if (!el) return; el.textContent = formatTemp(tenths / 10); el.setAttribute('data-target-tenths', String(tenths)); }";
  html += "function getTargetTenths(ch) { var el = document.getElementById('target-' + ch); if (!el) return 0; var value = parseInt(el.getAttribute('data-target-tenths'), 10); if (isNaN(value)) { value = 0; } return value; }";
  html += "function updateBadge(id, text, className) { var el = document.getElementById(id); if (!el) return; el.textContent = text; el.className = 'badge ' + className; }";
  html += "function refreshData() { fetch('/data').then(function(response) { return response.json(); }).then(function(items) { items.forEach(function(item) { var temp = document.getElementById('temp-' + item.ch); if (temp) temp.textContent = formatTemp(item.temp); var name = document.getElementById('name-' + item.ch); if (name) name.textContent = item.name; var targetTenths = parseTempTenths(item.target); if (Object.prototype.hasOwnProperty.call(pendingSetpoints, item.ch)) { if (targetTenths === pendingSetpoints[item.ch] && !pendingSetpointTimers[item.ch]) { delete pendingSetpoints[item.ch]; } else { targetTenths = pendingSetpoints[item.ch]; } } else { setTargetValue(item.ch, targetTenths); } var standby = document.getElementById('standby-' + item.ch); if (standby) standby.textContent = formatTemp(item.standby); var battery = document.getElementById('battery-' + item.ch); if (battery) battery.textContent = item.battery + '%'; updateBadge('heat-' + item.ch, item.heating ? '🔥 Varme' : 'Sluk', item.heating ? 'badge-heat' : 'badge-off'); var modeVal = item.mode; if (Object.prototype.hasOwnProperty.call(pendingModes, item.ch)) { if (modeVal === pendingModes[item.ch]) { delete pendingModes[item.ch]; } else { modeVal = pendingModes[item.ch]; } } updateBadge('mode-' + item.ch, modeVal === 0 ? 'Manuel' : 'Standby', 'badge-mode'); }); }).catch(function() {}); }";
  html += "function pushSetpoint(ch, tenths) { fetch('/set?ch=' + ch + '&val=' + (tenths / 10).toFixed(1)).then(function(response) { if (!response.ok) { throw new Error('setpoint'); } setTimeout(refreshData, 300); }).catch(function() { setTargetValue(ch, getTargetTenths(ch)); }); }";
  html += "function queueSetpointPush(ch) { if (pendingSetpointTimers[ch]) clearTimeout(pendingSetpointTimers[ch]); pendingSetpointTimers[ch] = setTimeout(function() { delete pendingSetpointTimers[ch]; pushSetpoint(ch, pendingSetpoints[ch]); }, SETPOINT_DEBOUNCE_MS); }";
  html += "function adjust(ch, deltaTenths) { var targetTenths = clampTargetTenths(getTargetTenths(ch) + deltaTenths); pendingSetpoints[ch] = targetTenths; setTargetValue(ch, targetTenths); queueSetpointPush(ch); }";
  html += "function toggleMode(ch) { var modeEl = document.getElementById('mode-' + ch); var current = Object.prototype.hasOwnProperty.call(pendingModes, ch) ? pendingModes[ch] : (modeEl && modeEl.textContent.indexOf('Standby') !== -1 ? 1 : 0); var next = current === 0 ? 1 : 0; pendingModes[ch] = next; updateBadge('mode-' + ch, next === 0 ? 'Manuel' : 'Standby', 'badge-mode'); fetch('/setmode?ch=' + ch + '&mode=' + next).then(function(response) { if (!response.ok) throw new Error('mode'); setTimeout(refreshData, 300); }).catch(function() { updateBadge('mode-' + ch, current === 0 ? 'Manuel' : 'Standby', 'badge-mode'); delete pendingModes[ch]; }); }";
  html += "setInterval(refreshData, 10000);";
  html += "</script></head><body><h1>Wavin Styring</h1>";

  for (uint8_t ch = 0; ch < WavinController::NUMBER_OF_CHANNELS; ch++) {
    ChannelData data;
    if (readChannelData(ch, data) && data.active) {
      appendChannelCard(html, data);
    }
  }

  String sysModel = nameOk ? ("AC-" + String(nameReg[0])) : "N/A";
  String sysHW    = hwOk   ? ("MC110" + String(hwReg[0])) : "N/A";
  String sysSW    = swOk   ? ("MC610" + String(swReg[0] >> 4, HEX) + String(swReg[0] & 0x0F)) : "N/A";
  html += "<div class='footer'><b>System Info:</b><br>Model: " + sysModel + " | HW: " + sysHW + " | SW: " + sysSW + "</div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetMode() {
  if (!server.hasArg("ch") || !server.hasArg("mode")) {
    server.send(400, "text/plain", "Manglende parameter");
    return;
  }

  int channel = server.arg("ch").toInt();
  if (channel < 0 || channel >= WavinController::NUMBER_OF_CHANNELS) {
    server.send(400, "text/plain", "Ugyldig kanal");
    return;
  }

  int mode = server.arg("mode").toInt();
  if (mode != WavinController::PACKED_DATA_CONFIGURATION_MODE_MANUAL &&
      mode != WavinController::PACKED_DATA_CONFIGURATION_MODE_STANDBY) {
    server.send(400, "text/plain", "Ugyldig tilstand");
    return;
  }

  if (wavin.writeMaskedRegister(WavinController::CATEGORY_PACKED_DATA, channel, WavinController::PACKED_DATA_CONFIGURATION, (uint16_t)mode, WavinController::PACKED_DATA_CONFIGURATION_MODE_MASK)) {
    server.send(200, "text/plain", "OK");
    return;
  }

  server.send(500, "text/plain", "Fejl");
}

void handleSetTemp() {
  if (!server.hasArg("ch") || !server.hasArg("val")) {
    server.send(400, "text/plain", "Manglende parameter");
    return;
  }

  int channel = server.arg("ch").toInt();
  if (channel < 0 || channel >= WavinController::NUMBER_OF_CHANNELS) {
    server.send(400, "text/plain", "Ugyldig kanal");
    return;
  }

  int tempValue = clampSetpointTenths((int)lroundf(server.arg("val").toFloat() * 10.0f));
  if (wavin.writeRegister(WavinController::CATEGORY_PACKED_DATA, channel, WavinController::PACKED_DATA_MANUAL_TEMPERATURE, tempValue)) {
    server.send(200, "text/plain", "OK");
    return;
  }

  server.send(500, "text/plain", "Fejl");
}

void setup() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set", handleSetTemp);
  server.on("/setmode", handleSetMode);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}

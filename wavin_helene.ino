#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "WavinController.h"

#if __has_include("config.h")
#include "config.h"
#else
#include "config.h.example"
#endif

ESP8266WebServer server(80);
WavinController wavin(0, false, 1000);

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

  html += "<div class='card'><div class='row'><div><b id='name-" + channelId + "'>" + htmlEscape(data.name) + "</b><br><span id='temp-" + channelId + "'>" + String(data.temp, 1) + "°C</span>";
  html += "<div class='badges'><span class='badge " + getHeatBadgeClass(data.heating) + "' id='heat-" + channelId + "'>" + getHeatLabel(data.heating) + "</span>";
  html += "<span class='badge badge-mode' id='mode-" + channelId + "'>" + getModeLabel(data.mode) + "</span></div>";
  html += "<div class='battery'>Batteri: <span id='battery-" + channelId + "'>" + String(data.battery) + "%</span></div></div>";
  html += "<div class='controls'><button class='btn' onclick='adjust(" + channelId + ",-0.5)'>-</button>";
  html += "<span class='target' id='target-" + channelId + "'>" + String(data.target, 1) + "°C</span>";
  html += "<button class='btn' onclick='adjust(" + channelId + ",0.5)'>+</button>";
  html += "<div class='standby'>Standby: <span id='standby-" + channelId + "'>" + String(data.standby, 1) + "°C</span></div></div></div></div>";
}

void handleData() {
  String json = "[";
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
  uint16_t hwReg[1], swReg[1], nameReg[1];
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x02, 1, hwReg);
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x03, 1, swReg);
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x04, 1, nameReg);

  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
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
  html += "function formatTemp(value) { return Number(value).toFixed(1) + '°C'; }";
  html += "function getTargetValue(ch) { var el = document.getElementById('target-' + ch); return el ? parseFloat(el.textContent) : 0; }";
  html += "function updateBadge(id, text, className) { var el = document.getElementById(id); if (!el) return; el.textContent = text; el.className = 'badge ' + className; }";
  html += "function refreshData() { fetch('/data').then(function(response) { return response.json(); }).then(function(items) { items.forEach(function(item) {";
  html += "var temp = document.getElementById('temp-' + item.ch); if (temp) temp.textContent = formatTemp(item.temp);";
  html += "var name = document.getElementById('name-' + item.ch); if (name) name.textContent = item.name;";
  html += "var target = document.getElementById('target-' + item.ch); if (target) target.textContent = formatTemp(item.target);";
  html += "var standby = document.getElementById('standby-' + item.ch); if (standby) standby.textContent = formatTemp(item.standby);";
  html += "var battery = document.getElementById('battery-' + item.ch); if (battery) battery.textContent = item.battery + '%';";
  html += "updateBadge('heat-' + item.ch, item.heating ? '🔥 Varme' : 'Sluk', item.heating ? 'badge-heat' : 'badge-off');";
  html += "var mode = document.getElementById('mode-' + item.ch); if (mode) mode.textContent = item.mode === 1 ? 'Standby' : 'Manuel';";
  html += "}); }).catch(function() {}); }";
  html += "function adjust(ch, delta) { var val = (getTargetValue(ch) + delta).toFixed(1); fetch('/set?ch=' + ch + '&val=' + val).then(function() { setTimeout(refreshData, 300); }); }";
  html += "setInterval(refreshData, 10000);";
  html += "</script></head><body><h1>Wavin Styring</h1>";

  for (uint8_t ch = 0; ch < WavinController::NUMBER_OF_CHANNELS; ch++) {
    ChannelData data;
    if (readChannelData(ch, data) && data.active) {
      appendChannelCard(html, data);
    }
  }

  html += "<div class='footer'><b>System Info:</b><br>Model: AC-" + String(nameReg[0]) + " | HW: MC110" + String(hwReg[0]) + " | SW: MC610" + String(swReg[0] >> 4, HEX) + String(swReg[0] & 0x0F) + "</div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
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

  int tempValue = (int)(server.arg("val").toFloat() * 10);
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
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}

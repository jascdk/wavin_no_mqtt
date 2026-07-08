#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <math.h>

#include "WavinController.h"

#ifndef TX_ENABLE_PIN
#define TX_ENABLE_PIN 13
#endif

const char *WIFI_SSID = "ssid";
const char *WIFI_PASSWORD = "pass";
const char *OTA_HOSTNAME = "wavin-controller";

WavinController wavin(TX_ENABLE_PIN, false, 3000);
ESP8266WebServer server(80);

uint16_t nameReg[6];
uint16_t hwReg[6];
uint16_t swReg[6];

struct ChannelData {
  uint8_t channel;
  bool active;
  bool outputOn;
  uint8_t primaryElement;
  float airTemp;
  float floorTemp;
  float manualTemp;
  int mode;
  bool intLock;
};

int clampSetpointTenths(int value) {
  const int minVal = 50;
  const int maxVal = 350;
  if (value < minVal) return minVal;
  if (value > maxVal) return maxVal;
  return value;
}

bool readChannelData(uint8_t ch, ChannelData &data) {
  data.channel = ch;

  uint16_t statusReg[1] = {0};
  if (!wavin.readRegisters(WavinController::CATEGORY_CHANNELS, ch, WavinController::CHANNELS_TIMER_EVENT, 1, statusReg)) {
    return false;
  }
  data.outputOn = (statusReg[0] & WavinController::CHANNELS_TIMER_EVENT_OUTP_ON_MASK) != 0;

  uint16_t primaryReg[1] = {0};
  if (!wavin.readRegisters(WavinController::CATEGORY_CHANNELS, ch, WavinController::CHANNELS_PRIMARY_ELEMENT, 1, primaryReg)) {
    return false;
  }
  data.primaryElement = primaryReg[0] & WavinController::CHANNELS_PRIMARY_ELEMENT_ELEMENT_MASK;
  bool allTpLost = (primaryReg[0] & WavinController::CHANNELS_PRIMARY_ELEMENT_ALL_TP_LOST_MASK) != 0;

  uint16_t airReg[1] = {0};
  uint16_t floorReg[1] = {0};
  bool hasAir = wavin.readRegisters(WavinController::CATEGORY_ELEMENTS, data.primaryElement, WavinController::ELEMENTS_AIR_TEMPERATURE, 1, airReg);
  bool hasFloor = wavin.readRegisters(WavinController::CATEGORY_ELEMENTS, data.primaryElement, WavinController::ELEMENTS_AIR_TEMPERATURE + 1, 1, floorReg);

  data.airTemp = hasAir ? ((int16_t)airReg[0] / 10.0f) : NAN;
  data.floorTemp = hasFloor ? ((int16_t)floorReg[0] / 10.0f) : NAN;

  uint16_t manualReg[1] = {0};
  if (!wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_MANUAL_TEMPERATURE, 1, manualReg)) {
    return false;
  }
  data.manualTemp = ((int16_t)manualReg[0]) / 10.0f;

  uint16_t cfgReg[1] = {0};
  if (!wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_CONFIGURATION, 1, cfgReg)) {
    return false;
  }
  data.mode = cfgReg[0] & WavinController::PACKED_DATA_CONFIGURATION_MODE_MASK;
  data.intLock = (cfgReg[0] & WavinController::PACKED_DATA_CONFIGURATION_INT_LOCK_MASK) != 0;

  data.active = !allTpLost && (hasAir || hasFloor);
  return true;
}

void appendChannelCard(String &html, const ChannelData &data) {
  String cardClass = data.outputOn ? "channel-card on" : "channel-card";
  html += "<div class='" + cardClass + "'>";
  html += "<h3>Kanal " + String(data.channel + 1) + " " + (data.intLock ? String("🔒") : String("🔓")) + "</h3>";
  html += "<p><b>Luft:</b> " + (isnan(data.airTemp) ? String("N/A") : String(data.airTemp, 1) + " \u00B0C") + "</p>";
  html += "<p><b>Gulv:</b> " + (isnan(data.floorTemp) ? String("N/A") : String(data.floorTemp, 1) + " \u00B0C") + "</p>";
  html += "<p><b>Setpoint:</b> " + String(data.manualTemp, 1) + " \u00B0C</p>";

  html += "<form onsubmit='setMode(event," + String(data.channel) + ")'>";
  html += "<label><b>Tilstand:</b></label>";
  html += "<select id='mode" + String(data.channel) + "'>";
  html += "<option value='0'" + String(data.mode == WavinController::PACKED_DATA_CONFIGURATION_MODE_MANUAL ? " selected" : "") + ">Manuel</option>";
  html += "<option value='1'" + String(data.mode == WavinController::PACKED_DATA_CONFIGURATION_MODE_STANDBY ? " selected" : "") + ">Standby</option>";
  html += "</select>";
  html += "<button type='submit'>Gem</button>";
  html += "</form>";

  html += "<form onsubmit='setIntLock(event," + String(data.channel) + ")'>";
  html += "<label><b>Service menu lås (INT_LOCK):</b></label>";
  html += "<select id='intlock" + String(data.channel) + "'>";
  html += "<option value='0'" + String(!data.intLock ? " selected" : "") + ">Fra</option>";
  html += "<option value='1'" + String(data.intLock ? " selected" : "") + ">Til</option>";
  html += "</select>";
  html += "<button type='submit'>Gem</button>";
  html += "</form>";

  html += "<form onsubmit='setTemp(event," + String(data.channel) + ")'>";
  html += "<label><b>Ny setpoint:</b></label>";
  html += "<input id='temp" + String(data.channel) + "' type='number' step='0.5' min='5' max='35' value='" + String(data.manualTemp, 1) + "'>";
  html += "<button type='submit'>Sæt</button>";
  html += "</form></div>";
}

void handleData() {
  String json = "[";
  bool first = true;

  for (uint8_t ch = 0; ch < WavinController::NUMBER_OF_CHANNELS; ch++) {
    ChannelData data;
    if (readChannelData(ch, data) && data.active) {
      if (!first) json += ",";
      first = false;

      json += "{\"ch\":" + String(data.channel + 1);
      json += ",\"air\":" + (isnan(data.airTemp) ? String("null") : String(data.airTemp, 1));
      json += ",\"floor\":" + (isnan(data.floorTemp) ? String("null") : String(data.floorTemp, 1));
      json += ",\"set\":" + String(data.manualTemp, 1);
      json += ",\"mode\":" + String(data.mode);
      json += ",\"intlock\":" + String(data.intLock ? "true" : "false");
      json += ",\"out\":" + String(data.outputOn ? "true" : "false") + "}";
    }
  }

  json += "]";
  server.send(200, "application/json", json);
}

void handleRoot() {
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 1, 6, nameReg);
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 7, 6, hwReg);
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 8, 6, swReg);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(F("<!doctype html><html><head><meta charset='UTF-8'><title>Wavin Styring</title>"));
  server.sendContent(F("<style>"
                       "body{font-family:Arial;background:#f4f7fa;margin:0;padding:20px;color:#333}"
                       "h1{color:#005b96}"
                       ".channel-card{background:white;padding:15px;margin:10px 0;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,0.1)}"
                       ".channel-card.on{border-left:6px solid #28a745;background:#f0fff4}"
                       "form{margin-top:8px;display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
                       "input,select,button{padding:6px;font-size:14px}"
                       "button{background:#007acc;color:white;border:none;border-radius:4px;cursor:pointer}"
                       "button:hover{background:#005f99}"
                       ".footer{margin-top:20px;font-size:12px;color:#555}"
                       "</style>"));
  server.sendContent(F("<script>"
                       "function refreshData(){fetch('/data').then(r=>r.json()).then(d=>console.log('updated',d));}"
                       "function setTemp(e,ch){e.preventDefault();const v=document.getElementById('temp'+ch).value;fetch('/set?ch='+ch+'&val='+v).then(r=>r.text()).then(alert);}"
                       "function setMode(e,ch){e.preventDefault();const m=document.getElementById('mode'+ch).value;fetch('/setmode?ch='+ch+'&mode='+m).then(r=>r.text()).then(alert);}"
                       "function setIntLock(e,ch){e.preventDefault();const l=document.getElementById('intlock'+ch).value;fetch('/setintlock?ch='+ch+'&lock='+l).then(r=>r.text()).then(alert);}"
                       "setInterval(refreshData,10000);"
                       "</script></head><body><h1>Wavin Styring</h1>"));

  for (uint8_t ch = 0; ch < WavinController::NUMBER_OF_CHANNELS; ch++) {
    ChannelData data;
    if (readChannelData(ch, data) && data.active) {
      String card;
      card.reserve(900);
      appendChannelCard(card, data);
      server.sendContent(card);
      yield();
    }
  }

  String footer;
  footer.reserve(300);
  footer += F("<div class='footer'><b>System Info:</b><br>Model: AC-");
  footer += String(nameReg[0]);
  footer += F(" | HW: MC110");
  footer += String(hwReg[0]);
  footer += F(" | SW: MC610");
  footer += String(swReg[0] >> 4, HEX);
  footer += String(swReg[0] & 0x0F);
  footer += F("</div></body></html>");
  server.sendContent(footer);
  server.sendContent("");
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

void handleSetIntLock() {
  if (!server.hasArg("ch") || !server.hasArg("lock")) {
    server.send(400, "text/plain", "Manglende parameter");
    return;
  }

  int channel = server.arg("ch").toInt();
  if (channel < 0 || channel >= WavinController::NUMBER_OF_CHANNELS) {
    server.send(400, "text/plain", "Ugyldig kanal");
    return;
  }

  int lock = server.arg("lock").toInt();
  if (lock != 0 && lock != 1) {
    server.send(400, "text/plain", "Ugyldig lock værdi");
    return;
  }

  uint16_t value = lock ? WavinController::PACKED_DATA_CONFIGURATION_INT_LOCK_MASK : 0;
  if (wavin.writeMaskedRegister(WavinController::CATEGORY_PACKED_DATA, channel, WavinController::PACKED_DATA_CONFIGURATION, value, WavinController::PACKED_DATA_CONFIGURATION_INT_LOCK_MASK)) {
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
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(ESP.getResetReason());
  Serial.printf("Free heap at boot: %u\n", ESP.getFreeHeap());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/set", handleSetTemp);
  server.on("/setmode", handleSetMode);
  server.on("/setintlock", handleSetIntLock);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}

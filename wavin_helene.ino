#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include "WavinController.h"

const char* ssid = "Fam.Scherrebeck.Net";
const char* password = "Jacob1905";

ESP8266WebServer server(80);
WavinController wavin(0, false, 1000); 

void handleRoot() {
  // Hent System Info én gang
  uint16_t hwReg[1], swReg[1], nameReg[1];
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x02, 1, hwReg); // HW
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x03, 1, swReg); // SW
  wavin.readRegisters(WavinController::CATEGORY_INFO, 0, 0x04, 1, nameReg); // Name

  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>";
  html += "body { font-family: -apple-system, sans-serif; background: #f0f2f5; padding: 15px; }";
  html += ".card { background: white; border-radius: 12px; padding: 15px; margin-bottom: 12px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".row { display: flex; justify-content: space-between; align-items: center; }";
  html += ".btn { background: #007bff; color: white; border: none; padding: 10px 18px; border-radius: 8px; font-size: 18px; cursor: pointer; }";
  html += ".battery { font-size: 0.8em; color: #666; margin-top: 5px; }";
  html += ".footer { background: #dee2e6; padding: 15px; border-radius: 12px; font-size: 0.85em; color: #444; }";
  html += "</style><script>";
  html += "function adjust(ch, val) { fetch('/set?ch=' + ch + '&val=' + val).then(() => { setTimeout(location.reload.bind(location), 200); }); }";
  html += "</script></head><body><h1>Wavin Styring</h1>";

  uint16_t reg[11];
  uint16_t setpointReg[1];
  
  for(int ch = 0; ch < 16; ch++) {
    if (wavin.readRegisters(WavinController::CATEGORY_CHANNELS, ch, WavinController::CHANNELS_PRIMARY_ELEMENT, 1, reg)) {
      uint8_t element = reg[0] & WavinController::CHANNELS_PRIMARY_ELEMENT_ELEMENT_MASK;
      if (element > 0) {
        wavin.readRegisters(WavinController::CATEGORY_ELEMENTS, element - 1, 0, 11, reg);
        wavin.readRegisters(WavinController::CATEGORY_PACKED_DATA, ch, WavinController::PACKED_DATA_MANUAL_TEMPERATURE, 1, setpointReg);
        
        float temp = reg[WavinController::ELEMENTS_AIR_TEMPERATURE] / 10.0;
        float target = setpointReg[0] / 10.0;
        int battery = reg[WavinController::ELEMENTS_BATTERY_STATUS] * 10;
        
        html += "<div class='card'><div class='row'><div><b>Kanal " + String(ch+1) + "</b><br>" + String(temp, 1) + "°C";
        html += "<div class='battery'>Batteri: " + String(battery) + "%</div></div>";
        html += "<div><button class='btn' onclick='adjust(" + String(ch) + "," + String(target - 0.5) + ")'>-</button>";
        html += "<span style='margin:0 15px; font-weight:bold'>" + String(target, 1) + "°C</span>";
        html += "<button class='btn' onclick='adjust(" + String(ch) + "," + String(target + 0.5) + ")'>+</button></div></div></div>";
      }
    }
  }

  // System Info Footer
  html += "<div class='footer'><b>System Info:</b><br>Model: AC-" + String(nameReg[0]) + " | HW: MC110" + String(hwReg[0]) + " | SW: MC610" + String(swReg[0] >> 4, HEX) + String(swReg[0] & 0x0F) + "</div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetTemp() {
  if (server.hasArg("ch") && server.hasArg("val")) {
    int channel = server.arg("ch").toInt();
    int tempValue = (int)(server.arg("val").toFloat() * 10);
    wavin.writeRegister(WavinController::CATEGORY_PACKED_DATA, channel, WavinController::PACKED_DATA_MANUAL_TEMPERATURE, tempValue);
    server.send(200, "text/plain", "OK");
  }
}

void setup() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  ArduinoOTA.begin();
  server.on("/", handleRoot);
  server.on("/set", handleSetTemp);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}

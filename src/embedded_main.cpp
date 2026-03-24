#include <Arduino.h>
#include <ArduinoJson.h>
#include <LovyanGFX.hpp>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>
#include <stdio.h>
#include <esp_heap_caps.h>

#include "app_config.h"
#include "printer_state.h"
#include "ui.h"

namespace {

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX() {
    auto busCfg = bus_.config();
    busCfg.spi_host = SPI2_HOST;
    busCfg.spi_mode = 0;
    busCfg.freq_write = 27000000;
    busCfg.freq_read = 16000000;
    busCfg.spi_3wire = false;
    busCfg.use_lock = true;
    busCfg.dma_channel = SPI_DMA_CH_AUTO;
    busCfg.pin_sclk = LCD_SCLK_PIN;
    busCfg.pin_mosi = LCD_MOSI_PIN;
    busCfg.pin_miso = LCD_MISO_PIN;
    busCfg.pin_dc = LCD_DC_PIN;
    bus_.config(busCfg);
    panel_.setBus(&bus_);

    auto panelCfg = panel_.config();
    panelCfg.pin_cs = LCD_CS_PIN;
    panelCfg.pin_rst = LCD_RST_PIN;
    panelCfg.pin_busy = -1;
    panelCfg.memory_width = 240;
    panelCfg.memory_height = 320;
    panelCfg.panel_width = 240;
    panelCfg.panel_height = 320;
    panelCfg.offset_x = 0;
    panelCfg.offset_y = 0;
    panelCfg.offset_rotation = 0;
    panelCfg.readable = false;
    panelCfg.invert = false;
    panelCfg.rgb_order = true;
    panelCfg.bus_shared = false;
    panel_.config(panelCfg);

    auto lightCfg = light_.config();
    lightCfg.pin_bl = LCD_BL_PIN;
    lightCfg.invert = LCD_BL_INVERT;
    lightCfg.freq = 12000;
    lightCfg.pwm_channel = 7;
    light_.config(lightCfg);
    panel_.setLight(&light_);

    setPanel(&panel_);
  }

 private:
  lgfx::Panel_ILI9341 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
};

LGFX lcd;
WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
PrinterState printer = makeDefaultPrinterState();

String reportTopic;
String requestTopic;
String clientId;
bool configReady = false;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastPushallMs = 0;
unsigned long lastTickMs = 0;
unsigned long lastUiRefreshMs = 0;

lv_disp_draw_buf_t drawBuf;
lv_color_t *drawBuffer = nullptr;
lv_disp_drv_t dispDrv;

String makeClientId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "leaf-s3-%04X", static_cast<unsigned>(chipId & 0xFFFF));
  return String(buffer);
}

template <typename T>
void updateIfPresent(JsonVariantConst obj, const char *key, T &target) {
  if (obj[key].is<T>()) {
    target = obj[key].as<T>();
  }
}

void copyStringIfPresent(JsonVariantConst obj, const char *key, char *target, size_t size) {
  if (!obj[key].is<const char *>()) {
    return;
  }

  snprintf(target, size, "%s", obj[key].as<const char *>());
}

void applyPrintPayload(JsonObjectConst print) {
  updateIfPresent<int>(print, "mc_percent", printer.percent);
  updateIfPresent<int>(print, "mc_remaining_time", printer.remainingMinutes);
  updateIfPresent<int>(print, "layer_num", printer.currentLayer);
  updateIfPresent<int>(print, "total_layer_num", printer.totalLayers);
  updateIfPresent<float>(print, "nozzle_temper", printer.nozzleTemp);
  updateIfPresent<float>(print, "nozzle_target_temper", printer.nozzleTargetTemp);
  updateIfPresent<float>(print, "bed_temper", printer.bedTemp);
  updateIfPresent<float>(print, "bed_target_temper", printer.bedTargetTemp);
  updateIfPresent<int>(print, "spd_lvl", printer.speedLevel);
  updateIfPresent<int>(print, "stg_cur", printer.stageCurrent);

  copyStringIfPresent(print, "gcode_state", printer.stageText, sizeof(printer.stageText));
  copyStringIfPresent(print, "subtask_name", printer.jobName, sizeof(printer.jobName));
  copyStringIfPresent(print, "wifi_signal", printer.wifiSignal, sizeof(printer.wifiSignal));
  copyStringIfPresent(print, "print_type", printer.printType, sizeof(printer.printType));
  copyStringIfPresent(print, "command", printer.lastCommand, sizeof(printer.lastCommand));

  if (print["gcode_state"].is<const char *>()) {
    const char *gcodeState = print["gcode_state"].as<const char *>();
    printer.printing = strcmp(gcodeState, "RUNNING") == 0 || strcmp(gcodeState, "PREPARE") == 0 ||
                       strcmp(gcodeState, "SLICING") == 0 || strcmp(gcodeState, "PAUSE") == 0;
  }
  printer.hasData = true;
  printer.lastUpdateMs = millis();
  printer.stale = false;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  StaticJsonDocument<4096> doc;
  auto err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("JSON parse failed on topic %s: %s\n", topic, err.c_str());
    return;
  }

  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root["print"].is<JsonObjectConst>()) {
    applyPrintPayload(root["print"].as<JsonObjectConst>());
    ui::applyState(printer);
  }
}

void publishPushall() {
  StaticJsonDocument<128> doc;
  doc["pushing"]["sequence_id"] = "0";
  doc["pushing"]["command"] = "pushall";
  doc["pushing"]["version"] = 1;
  doc["pushing"]["push_target"] = 1;

  char payload[128];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  mqttClient.publish(requestTopic.c_str(), reinterpret_cast<const uint8_t *>(payload), len);
  snprintf(printer.lastCommand, sizeof(printer.lastCommand), "%s", "pushall");
}

void connectWifi() {
  const unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED || (now - lastWifiAttemptMs) < WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptMs = now;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi %s\n", WIFI_SSID);
}

void connectMqtt() {
  const unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED || mqttClient.connected() ||
      (now - lastMqttAttemptMs) < MQTT_RETRY_MS) {
    return;
  }

  lastMqttAttemptMs = now;
  mqttClient.setServer(BAMBU_PRINTER_IP, BAMBU_MQTT_PORT);
  mqttClient.setBufferSize(4096);

  Serial.printf("Connecting to MQTT %s:%u\n", BAMBU_PRINTER_IP, BAMBU_MQTT_PORT);
  const bool connected =
      mqttClient.connect(clientId.c_str(), BAMBU_MQTT_USERNAME, BAMBU_ACCESS_CODE);
  if (!connected) {
    Serial.printf("MQTT connect failed, rc=%d\n", mqttClient.state());
    return;
  }

  mqttClient.subscribe(reportTopic.c_str());
  publishPushall();
  lastPushallMs = now;
  Serial.printf("MQTT connected, subscribed to %s\n", reportTopic.c_str());
}

void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  lcd.startWrite();
  lcd.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<uint16_t *>(&colorP->full));
  lcd.endWrite();

  lv_disp_flush_ready(disp);
}

void setupDisplay() {
  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  lcd.setSwapBytes(true);
  lcd.setBrightness(192);

  lv_init();
  drawBuffer = static_cast<lv_color_t *>(
      heap_caps_malloc(sizeof(lv_color_t) * 240 * 40, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, 240 * 40);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = 240;
  dispDrv.ver_res = 320;
  dispDrv.flush_cb = lvglFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  ui::init();
  ui::applyState(printer);
}

void validateConfig() {
  configReady = !(String(WIFI_SSID).startsWith("REPLACE_") ||
                  String(BAMBU_PRINTER_SERIAL).startsWith("REPLACE_") ||
                  String(BAMBU_ACCESS_CODE).startsWith("REPLACE_"));
  if (!configReady) {
    Serial.println("Please edit include/app_config.h before flashing.");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  setupDisplay();
  validateConfig();

  clientId = makeClientId();
  reportTopic = "device/" + String(BAMBU_PRINTER_SERIAL) + "/report";
  requestTopic = "device/" + String(BAMBU_PRINTER_SERIAL) + "/request";

  secureClient.setInsecure();
  mqttClient.setCallback(mqttCallback);
  WiFi.disconnect(true, true);
  lastTickMs = millis();
  lastUiRefreshMs = millis();
}

void loop() {
  const unsigned long now = millis();
  const unsigned long elapsed = now - lastTickMs;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    lastTickMs = now;
  }
  lv_timer_handler();

  if (!configReady) {
    delay(10);
    return;
  }

  connectWifi();
  connectMqtt();

  if (mqttClient.connected()) {
    mqttClient.loop();
    if ((now - lastPushallMs) > PUSHALL_INTERVAL_MS) {
      publishPushall();
      lastPushallMs = now;
    }
  }

  printer.stale = !printer.hasData || (now - printer.lastUpdateMs) > STALE_AFTER_MS;
  if ((now - lastUiRefreshMs) >= UI_REFRESH_MS) {
    ui::applyState(printer);
    lastUiRefreshMs = now;
  }
  delay(5);
}

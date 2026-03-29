#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>
#include <stdio.h>
#include <ctype.h>
#include <esp_heap_caps.h>

#include "app_config.h"
#include "printer_state.h"
#include "ui.h"

namespace {

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX() {
    auto busCfg = bus_.config();
    busCfg.spi_host = HSPI_HOST;
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
WiFiClientSecure telegramClient;
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
unsigned long lastTelegramPollMs = 0;
unsigned long lastTelegramProfileMs = 0;
uint32_t mqttSequenceId = 1000;
int64_t telegramLastUpdateId = 0;
uint8_t currentBrightness = 192;
SPIClass touchSpi(VSPI);
lv_indev_drv_t indevDrv;
Preferences preferences;
char telegramBotUsername[40] = "-";
char telegramBotStatus[24] = "Disabled";
volatile bool telegramUiDirty = true;

enum class PendingTelegramAction : uint8_t {
  None,
  PauseResume,
  Stop,
  SpeedSilent,
  SpeedStandard,
  SpeedSport,
  SpeedLudicrous,
};

volatile PendingTelegramAction pendingTelegramAction = PendingTelegramAction::None;

lv_disp_draw_buf_t drawBuf;
lv_color_t *drawBuffer = nullptr;
lv_disp_drv_t dispDrv;

struct TouchCalibration {
  int rawMinX = TOUCH_RAW_MIN_X;
  int rawMaxX = TOUCH_RAW_MAX_X;
  int rawMinY = TOUCH_RAW_MIN_Y;
  int rawMaxY = TOUCH_RAW_MAX_Y;
  bool invertX = TOUCH_INVERT_X;
  bool invertY = TOUCH_INVERT_Y;
};

TouchCalibration touchCalibration;
bool calibrationActive = false;
bool calibrationTouchHeld = false;
uint8_t calibrationStep = 0;
uint32_t calibrationAccumX = 0;
uint32_t calibrationAccumY = 0;
uint16_t calibrationSampleCount = 0;
struct RawPoint {
  int x;
  int y;
};
RawPoint calibrationPoints[4] = {};

constexpr int kCalibrationTargetXs[4] = {24, LCD_HOR_RES - 25, LCD_HOR_RES - 25, 24};
constexpr int kCalibrationTargetYs[4] = {24, 24, LCD_VER_RES - 25, LCD_VER_RES - 25};

bool readTouchRaw(int &rawX, int &rawY);
void publishPauseOrResume();
void publishStop();
void publishSpeed(uint8_t speedLevel);

bool telegramEnabled() {
  return TELEGRAM_BOT_TOKEN[0] != '\0' && TELEGRAM_CHAT_ID[0] != '\0';
}

void updateTelegramUiStatus() { telegramUiDirty = true; }

const char *nextSequenceId() {
  static char buffer[16];
  snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(mqttSequenceId++));
  return buffer;
}

String urlEncode(const char *input) {
  String encoded;
  while (*input) {
    const unsigned char c = static_cast<unsigned char>(*input++);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}

const char *speedTextLocal(int speedLevel) {
  switch (speedLevel) {
    case 1:
      return "Silent";
    case 2:
      return "Standard";
    case 3:
      return "Sport";
    case 4:
      return "Ludicrous";
    default:
      return "Normal";
  }
}

void formatDurationLocal(int minutes, char *buffer, size_t size) {
  if (minutes <= 0) {
    snprintf(buffer, size, "Now");
    return;
  }
  const int hours = minutes / 60;
  const int mins = minutes % 60;
  if (hours > 0) {
    snprintf(buffer, size, "%dh %02dm", hours, mins);
  } else {
    snprintf(buffer, size, "%dm", mins);
  }
}

void sendTelegramMessage(const String &text) {
  if (!telegramEnabled() || WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  telegramClient.setInsecure();
  http.setConnectTimeout(400);
  http.setTimeout(500);
  const String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  if (!http.begin(telegramClient, url)) return;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body = String("chat_id=") + TELEGRAM_CHAT_ID + "&text=" + urlEncode(text.c_str());
  http.POST(body);
  http.end();
}

void updateTelegramProfile() {
  if (!telegramEnabled() || WiFi.status() != WL_CONNECTED) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", telegramEnabled() ? "Offline" : "Disabled");
    if (!telegramEnabled()) snprintf(telegramBotUsername, sizeof(telegramBotUsername), "%s", "-");
    updateTelegramUiStatus();
    return;
  }

  const unsigned long now = millis();
  if ((now - lastTelegramProfileMs) < 60000UL) return;
  lastTelegramProfileMs = now;

  HTTPClient http;
  telegramClient.setInsecure();
  http.setConnectTimeout(400);
  http.setTimeout(500);
  const String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/getMe";
  if (!http.begin(telegramClient, url)) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err || !doc["ok"].as<bool>()) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }

  snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Enabled");
  if (doc["result"]["username"].is<const char *>()) {
    snprintf(telegramBotUsername, sizeof(telegramBotUsername), "@%s", doc["result"]["username"].as<const char *>());
  } else {
    snprintf(telegramBotUsername, sizeof(telegramBotUsername), "%s", "-");
  }
  updateTelegramUiStatus();
}

void buildStatusMessage(char *buffer, size_t size) {
  char remaining[24];
  char layers[24];
  formatDurationLocal(printer.remainingMinutes, remaining, sizeof(remaining));
  if (printer.totalLayers > 0) {
    snprintf(layers, sizeof(layers), "%d/%d", printer.currentLayer, printer.totalLayers);
  } else {
    snprintf(layers, sizeof(layers), "%d", printer.currentLayer);
  }

  snprintf(buffer, size,
           "State: %s\nJob: %s\nProgress: %d%%\nRemaining: %s\nLayers: %s\nNozzle: %.0f/%.0f C\nBed: %.0f/%.0f C"
           "\nSpeed: %s\nWiFi: %s",
           printer.stageText, printer.jobName, printer.percent, remaining, layers, printer.nozzleTemp,
           printer.nozzleTargetTemp, printer.bedTemp, printer.bedTargetTemp, speedTextLocal(printer.speedLevel),
           printer.wifiSignal);
}

void requestTelegramAction(PendingTelegramAction action) {
  if (pendingTelegramAction == PendingTelegramAction::None) {
    pendingTelegramAction = action;
  }
}

void handleTelegramCommand(const char *messageText) {
  if (!messageText || messageText[0] == '\0') return;

  char command[96];
  snprintf(command, sizeof(command), "%s", messageText);
  for (char *p = command; *p; ++p) {
    *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }

  if (strcmp(command, "/status") == 0) {
    char status[256];
    buildStatusMessage(status, sizeof(status));
    sendTelegramMessage(status);
    return;
  }
  if (strcmp(command, "/pause") == 0) {
    if (printer.printing && strcmp(printer.stageText, "PAUSE") != 0) {
      requestTelegramAction(PendingTelegramAction::PauseResume);
      sendTelegramMessage("Pause requested.");
    } else {
      sendTelegramMessage("Pause is not available right now.");
    }
    return;
  }
  if (strcmp(command, "/resume") == 0) {
    if (printer.printing && strcmp(printer.stageText, "PAUSE") == 0) {
      requestTelegramAction(PendingTelegramAction::PauseResume);
      sendTelegramMessage("Resume requested.");
    } else {
      sendTelegramMessage("Resume is not available right now.");
    }
    return;
  }
  if (strcmp(command, "/stop") == 0) {
    if (printer.printing) {
      requestTelegramAction(PendingTelegramAction::Stop);
      sendTelegramMessage("Stop requested.");
    } else {
      sendTelegramMessage("Stop is not available right now.");
    }
    return;
  }
  if (strcmp(command, "/speed_silent") == 0 || strcmp(command, "/speed silent") == 0) {
    requestTelegramAction(PendingTelegramAction::SpeedSilent);
    sendTelegramMessage("Speed set to Silent.");
    return;
  }
  if (strcmp(command, "/speed_standard") == 0 || strcmp(command, "/speed standard") == 0) {
    requestTelegramAction(PendingTelegramAction::SpeedStandard);
    sendTelegramMessage("Speed set to Standard.");
    return;
  }
  if (strcmp(command, "/speed_sport") == 0 || strcmp(command, "/speed sport") == 0) {
    requestTelegramAction(PendingTelegramAction::SpeedSport);
    sendTelegramMessage("Speed set to Sport.");
    return;
  }
  if (strcmp(command, "/speed_ludicrous") == 0 || strcmp(command, "/speed ludicrous") == 0) {
    requestTelegramAction(PendingTelegramAction::SpeedLudicrous);
    sendTelegramMessage("Speed set to Ludicrous.");
    return;
  }
  sendTelegramMessage(
      "Commands: /status, /pause, /resume, /stop, /speed_silent, /speed_standard, /speed_sport, "
      "/speed_ludicrous");
}

void pollTelegram() {
  if (!telegramEnabled()) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Disabled");
    snprintf(telegramBotUsername, sizeof(telegramBotUsername), "%s", "-");
    updateTelegramUiStatus();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Offline");
    updateTelegramUiStatus();
    return;
  }
  const unsigned long now = millis();
  if ((now - lastTelegramPollMs) < TELEGRAM_POLL_MS) return;
  lastTelegramPollMs = now;

  HTTPClient http;
  telegramClient.setInsecure();
  http.setConnectTimeout(400);
  http.setTimeout(500);
  const String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN +
                     "/getUpdates?offset=" + String(telegramLastUpdateId + 1) + "&timeout=0";
  if (!http.begin(telegramClient, url)) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err || !doc["ok"].as<bool>()) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Error");
    updateTelegramUiStatus();
    return;
  }

  if (strcmp(telegramBotStatus, "Enabled") != 0 && strcmp(telegramBotUsername, "-") == 0) {
    snprintf(telegramBotStatus, sizeof(telegramBotStatus), "%s", "Enabled");
    updateTelegramUiStatus();
  }

  JsonArray results = doc["result"].as<JsonArray>();
  for (JsonObject update : results) {
    const int64_t updateId = update["update_id"] | 0;
    if (updateId > telegramLastUpdateId) telegramLastUpdateId = updateId;

    JsonObject message = update["message"].as<JsonObject>();
    if (message.isNull()) continue;

    const char *chatIdRaw = message["chat"]["id"].is<int64_t>() ? nullptr : nullptr;
    (void)chatIdRaw;
    char chatIdBuffer[32];
    if (message["chat"]["id"].is<int64_t>()) {
      snprintf(chatIdBuffer, sizeof(chatIdBuffer), "%lld",
               static_cast<long long>(message["chat"]["id"].as<int64_t>()));
    } else {
      continue;
    }
    if (strcmp(chatIdBuffer, TELEGRAM_CHAT_ID) != 0) continue;

    if (!message["text"].is<const char *>()) continue;
    handleTelegramCommand(message["text"].as<const char *>());
  }
}

void notifyTelegramStateChange(const PrinterState &previous, const PrinterState &next) {
  if (!telegramEnabled()) return;
  if (previous.hasData != next.hasData && !next.hasData) return;

  if (previous.stale != next.stale) {
    if (next.stale) {
      sendTelegramMessage("Printer went offline.");
    } else if (next.hasData) {
      sendTelegramMessage("Printer is back online.");
    }
  }

  if (strcmp(previous.stageText, next.stageText) == 0) return;
  if (strcmp(next.stageText, "PAUSE") == 0) {
    sendTelegramMessage(String("Print paused: ") + next.jobName);
  } else if (strcmp(next.stageText, "FINISH") == 0) {
    sendTelegramMessage(String("Print finished: ") + next.jobName);
  } else if (strcmp(next.stageText, "RUNNING") == 0 && strcmp(previous.stageText, "PAUSE") == 0) {
    sendTelegramMessage(String("Print resumed: ") + next.jobName);
  }
}

void telegramTask(void *) {
  PrinterState previousState = makeDefaultPrinterState();
  bool previousStateValid = false;

  for (;;) {
    updateTelegramProfile();
    pollTelegram();

    PrinterState snapshot = printer;
    if (!previousStateValid) {
      previousState = snapshot;
      previousStateValid = true;
    } else {
      notifyTelegramStateChange(previousState, snapshot);
      previousState = snapshot;
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

String makeClientId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "leaf-s3-%04X", static_cast<unsigned>(chipId & 0xFFFF));
  return String(buffer);
}

uint8_t brightnessToPercent(uint8_t value) {
  const uint8_t logicalValue = LCD_BL_INVERT ? static_cast<uint8_t>(255U - value) : value;
  return static_cast<uint8_t>((static_cast<uint16_t>(logicalValue) * 100U) / 255U);
}

void applyBrightness(uint8_t brightness) {
  currentBrightness = brightness;
  lcd.setBrightness(currentBrightness);
  ui::setBrightnessPercent(brightnessToPercent(currentBrightness));
}

uint8_t logicalBrightnessToHardware(uint8_t logicalValue) {
  return LCD_BL_INVERT ? static_cast<uint8_t>(255U - logicalValue) : logicalValue;
}

int extrapolateRaw(int screenValueA, int screenValueB, int rawValueA, int rawValueB, int targetScreenValue) {
  const int screenSpan = screenValueB - screenValueA;
  if (screenSpan == 0) return rawValueA;
  const long rawSpan = static_cast<long>(rawValueB) - static_cast<long>(rawValueA);
  const long screenOffset = static_cast<long>(targetScreenValue) - static_cast<long>(screenValueA);
  return rawValueA + static_cast<int>((rawSpan * screenOffset) / screenSpan);
}

void saveTouchCalibration() {
  if (!preferences.begin("touch", false)) return;
  preferences.putBool("valid", true);
  preferences.putInt("minx", touchCalibration.rawMinX);
  preferences.putInt("maxx", touchCalibration.rawMaxX);
  preferences.putInt("miny", touchCalibration.rawMinY);
  preferences.putInt("maxy", touchCalibration.rawMaxY);
  preferences.putBool("invx", touchCalibration.invertX);
  preferences.putBool("invy", touchCalibration.invertY);
  preferences.end();
}

void saveBrightnessPreference() {
  if (!preferences.begin("display", false)) return;
  preferences.putUChar("brightness", currentBrightness);
  preferences.end();
}

void loadTouchCalibration() {
  if (!preferences.begin("touch", true)) return;
  const bool valid = preferences.getBool("valid", false);
  if (valid) {
    touchCalibration.rawMinX = preferences.getInt("minx", TOUCH_RAW_MIN_X);
    touchCalibration.rawMaxX = preferences.getInt("maxx", TOUCH_RAW_MAX_X);
    touchCalibration.rawMinY = preferences.getInt("miny", TOUCH_RAW_MIN_Y);
    touchCalibration.rawMaxY = preferences.getInt("maxy", TOUCH_RAW_MAX_Y);
    touchCalibration.invertX = preferences.getBool("invx", TOUCH_INVERT_X);
    touchCalibration.invertY = preferences.getBool("invy", TOUCH_INVERT_Y);
  }
  preferences.end();
}

void loadBrightnessPreference() {
  if (!preferences.begin("display", true)) return;
  currentBrightness = preferences.getUChar("brightness", logicalBrightnessToHardware(192));
  preferences.end();
  if (LCD_BL_INVERT && currentBrightness >= 250) {
    currentBrightness = logicalBrightnessToHardware(192);
  }
}

void beginTouchCalibration() {
  calibrationActive = true;
  calibrationTouchHeld = false;
  calibrationStep = 0;
  calibrationAccumX = 0;
  calibrationAccumY = 0;
  calibrationSampleCount = 0;
  ui::showCalibrationStep(1, 4, kCalibrationTargetXs[0], kCalibrationTargetYs[0]);
  Serial.println("Touch calibration started");
}

void finishTouchCalibration() {
  const int leftRawX = (calibrationPoints[0].x + calibrationPoints[3].x) / 2;
  const int rightRawX = (calibrationPoints[1].x + calibrationPoints[2].x) / 2;
  const int topRawY = (calibrationPoints[0].y + calibrationPoints[1].y) / 2;
  const int bottomRawY = (calibrationPoints[2].y + calibrationPoints[3].y) / 2;

  const int rawXAtZero = extrapolateRaw(kCalibrationTargetXs[0], kCalibrationTargetXs[1], leftRawX,
                                        rightRawX, 0);
  const int rawXAtMax = extrapolateRaw(kCalibrationTargetXs[0], kCalibrationTargetXs[1], leftRawX,
                                       rightRawX, LCD_HOR_RES - 1);
  const int rawYAtZero = extrapolateRaw(kCalibrationTargetYs[0], kCalibrationTargetYs[3], topRawY,
                                        bottomRawY, 0);
  const int rawYAtMax = extrapolateRaw(kCalibrationTargetYs[0], kCalibrationTargetYs[3], topRawY,
                                       bottomRawY, LCD_VER_RES - 1);

  touchCalibration.rawMinX = min(rawXAtZero, rawXAtMax);
  touchCalibration.rawMaxX = max(rawXAtZero, rawXAtMax);
  touchCalibration.rawMinY = min(rawYAtZero, rawYAtMax);
  touchCalibration.rawMaxY = max(rawYAtZero, rawYAtMax);
  touchCalibration.invertX = leftRawX > rightRawX;
  touchCalibration.invertY = topRawY > bottomRawY;

  saveTouchCalibration();
  calibrationActive = false;
  ui::hideCalibration();

  Serial.printf("Touch calibration saved: minX=%d maxX=%d minY=%d maxY=%d invX=%d invY=%d\n",
                touchCalibration.rawMinX, touchCalibration.rawMaxX, touchCalibration.rawMinY,
                touchCalibration.rawMaxY, touchCalibration.invertX, touchCalibration.invertY);
}

void processTouchCalibration() {
  if (!calibrationActive) return;

  int rawX = 0;
  int rawY = 0;
  const bool pressed = readTouchRaw(rawX, rawY);
  if (!pressed) {
    if (calibrationTouchHeld && calibrationSampleCount > 0) {
      calibrationPoints[calibrationStep].x = calibrationAccumX / calibrationSampleCount;
      calibrationPoints[calibrationStep].y = calibrationAccumY / calibrationSampleCount;
      calibrationStep++;
      calibrationAccumX = 0;
      calibrationAccumY = 0;
      calibrationSampleCount = 0;
      calibrationTouchHeld = false;

      if (calibrationStep >= 4) {
        finishTouchCalibration();
      } else {
        ui::showCalibrationStep(calibrationStep + 1, 4, kCalibrationTargetXs[calibrationStep],
                                kCalibrationTargetYs[calibrationStep]);
      }
    }
    return;
  }

  calibrationTouchHeld = true;
  calibrationAccumX += rawX;
  calibrationAccumY += rawY;
  calibrationSampleCount++;
}

void handleUiAction(ui::Action action) {
  switch (action) {
    case ui::Action::StartTouchCalibration:
      beginTouchCalibration();
      break;
    case ui::Action::SetBrightnessLow:
      applyBrightness(logicalBrightnessToHardware(72));
      saveBrightnessPreference();
      break;
    case ui::Action::SetBrightnessMid:
      applyBrightness(logicalBrightnessToHardware(160));
      saveBrightnessPreference();
      break;
    case ui::Action::SetBrightnessMax:
      applyBrightness(logicalBrightnessToHardware(255));
      saveBrightnessPreference();
      break;
    case ui::Action::PauseOrResumePrint:
      publishPauseOrResume();
      break;
    case ui::Action::StopPrint:
      publishStop();
      break;
    case ui::Action::SetSpeedSilent:
      publishSpeed(1);
      break;
    case ui::Action::SetSpeedStandard:
      publishSpeed(2);
      break;
    case ui::Action::SetSpeedSport:
      publishSpeed(3);
      break;
    case ui::Action::SetSpeedLudicrous:
      publishSpeed(4);
      break;
  }
}

uint16_t readTouchChannel(uint8_t command) {
  SPISettings settings(2500000, MSBFIRST, SPI_MODE0);
  touchSpi.beginTransaction(settings);
  digitalWrite(TOUCH_CS_PIN, LOW);
  touchSpi.transfer(command);
  uint16_t raw = touchSpi.transfer16(0x0000) >> 3;
  digitalWrite(TOUCH_CS_PIN, HIGH);
  touchSpi.endTransaction();
  return raw;
}

bool readTouchRaw(int &rawX, int &rawY) {
  if (TOUCH_IRQ_PIN >= 0 && digitalRead(TOUCH_IRQ_PIN) == HIGH) {
    return false;
  }

  uint32_t rawXAccum = 0;
  uint32_t rawYAccum = 0;
  constexpr int samples = 4;
  for (int i = 0; i < samples; ++i) {
    rawXAccum += readTouchChannel(0xD0);
    rawYAccum += readTouchChannel(0x90);
  }
  rawXAccum /= samples;
  rawYAccum /= samples;

  int tx = static_cast<int>(rawXAccum);
  int ty = static_cast<int>(rawYAccum);

  if (TOUCH_SWAP_XY) {
    const int tmp = tx;
    tx = ty;
    ty = tmp;
  }

  rawX = tx;
  rawY = ty;
  return true;
}

bool readTouchPoint(int &x, int &y) {
  int rawX = 0;
  int rawY = 0;
  if (!readTouchRaw(rawX, rawY)) {
    return false;
  }

  int tx = map(rawX, touchCalibration.rawMinX, touchCalibration.rawMaxX, 0, LCD_HOR_RES - 1);
  int ty = map(rawY, touchCalibration.rawMinY, touchCalibration.rawMaxY, 0, LCD_VER_RES - 1);

  if (touchCalibration.invertX) tx = (LCD_HOR_RES - 1) - tx;
  if (touchCalibration.invertY) ty = (LCD_VER_RES - 1) - ty;

  x = constrain(tx, 0, LCD_HOR_RES - 1);
  y = constrain(ty, 0, LCD_VER_RES - 1);
  return true;
}

void lvglTouchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  LV_UNUSED(drv);
  if (calibrationActive) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  int x = 0;
  int y = 0;
  if (readTouchPoint(x, y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
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

template <size_t N>
bool publishPrintCommand(StaticJsonDocument<N> &doc, const char *command, const char *lastCommand,
                         const char *param = nullptr, const char *reason = nullptr) {
  if (!mqttClient.connected()) return false;

  JsonObject print = doc.createNestedObject("print");
  print["sequence_id"] = nextSequenceId();
  print["command"] = command;
  if (param) print["param"] = param;
  if (reason) print["reason"] = reason;

  char payload[256];
  const size_t len = serializeJson(doc, payload, sizeof(payload));
  const bool published =
      mqttClient.publish(requestTopic.c_str(), reinterpret_cast<const uint8_t *>(payload), len);
  if (published) {
    snprintf(printer.lastCommand, sizeof(printer.lastCommand), "%s", lastCommand);
    printer.lastUpdateMs = millis();
  }
  return published;
}

void publishPauseOrResume() {
  if (!printer.hasData || printer.stale || !printer.printing) return;
  StaticJsonDocument<128> doc;
  const bool paused = strcmp(printer.stageText, "PAUSE") == 0;
  publishPrintCommand(doc, paused ? "resume" : "pause", paused ? "resume" : "pause");
}

void publishStop() {
  if (!printer.hasData || printer.stale || !printer.printing) return;
  StaticJsonDocument<128> doc;
  publishPrintCommand(doc, "stop", "stop", nullptr, "success");
}

void publishSpeed(uint8_t speedLevel) {
  if (!printer.hasData || printer.stale || !printer.printing) return;
  if (speedLevel < 1 || speedLevel > 4) return;
  StaticJsonDocument<128> doc;
  char param[2];
  snprintf(param, sizeof(param), "%u", speedLevel);
  if (publishPrintCommand(doc, "print_speed", "print_speed", param)) {
    printer.speedLevel = speedLevel;
  }
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
  applyBrightness(currentBrightness);

  pinMode(TOUCH_CS_PIN, OUTPUT);
  digitalWrite(TOUCH_CS_PIN, HIGH);
  if (TOUCH_IRQ_PIN >= 0) {
    pinMode(TOUCH_IRQ_PIN, INPUT_PULLUP);
  }
  touchSpi.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);

  lv_init();
  drawBuffer = static_cast<lv_color_t *>(
      heap_caps_malloc(sizeof(lv_color_t) * LCD_HOR_RES * 24, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  lv_disp_draw_buf_init(&drawBuf, drawBuffer, nullptr, LCD_HOR_RES * 24);
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = LCD_HOR_RES;
  dispDrv.ver_res = LCD_VER_RES;
  dispDrv.flush_cb = lvglFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = lvglTouchRead;
  lv_indev_drv_register(&indevDrv);

  ui::init();
  ui::setActionHandler(handleUiAction);
  updateTelegramUiStatus();
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

  loadTouchCalibration();
  loadBrightnessPreference();
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
  if (telegramEnabled()) {
    xTaskCreatePinnedToCore(telegramTask, "telegram", 8192, nullptr, 1, nullptr, 0);
  }
}

void loop() {
  const unsigned long now = millis();
  const unsigned long elapsed = now - lastTickMs;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    lastTickMs = now;
  }
  processTouchCalibration();
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

  const PendingTelegramAction action = pendingTelegramAction;
  if (action != PendingTelegramAction::None) {
    pendingTelegramAction = PendingTelegramAction::None;
    switch (action) {
      case PendingTelegramAction::PauseResume:
        publishPauseOrResume();
        break;
      case PendingTelegramAction::Stop:
        publishStop();
        break;
      case PendingTelegramAction::SpeedSilent:
        publishSpeed(1);
        break;
      case PendingTelegramAction::SpeedStandard:
        publishSpeed(2);
        break;
      case PendingTelegramAction::SpeedSport:
        publishSpeed(3);
        break;
      case PendingTelegramAction::SpeedLudicrous:
        publishSpeed(4);
        break;
      case PendingTelegramAction::None:
        break;
    }
  }

  printer.stale = !printer.hasData || (now - printer.lastUpdateMs) > STALE_AFTER_MS;
  if ((now - lastUiRefreshMs) >= UI_REFRESH_MS) {
    if (telegramUiDirty) {
      telegramUiDirty = false;
      ui::setTelegramStatus(telegramBotStatus, telegramBotUsername);
    }
    ui::applyState(printer);
    lastUiRefreshMs = now;
  }
  delay(5);
}

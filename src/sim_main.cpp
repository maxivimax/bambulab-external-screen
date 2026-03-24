#include <lvgl.h>
#include <fstream>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>

#include "printer_state.h"
#include "ui.h"

#include "sdl/sdl.h"

namespace {

lv_disp_draw_buf_t drawBuf;
lv_color_t buf1[240 * 40];
PrinterState mockState = makeDefaultPrinterState();
uint32_t lastTickMs = 0;
uint32_t lastMockUpdateMs = 0;
uint32_t lastLiveReadMs = 0;
std::string liveStatePath;

uint32_t millis32();

std::string trim(const std::string &value) {
  const size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

void copyToBuffer(const std::string &value, char *target, size_t size) {
  snprintf(target, size, "%s", value.c_str());
}

bool loadLiveState(PrinterState &state) {
  if (liveStatePath.empty()) {
    return false;
  }

  const uint32_t now = millis32();
  if ((now - lastLiveReadMs) < 500) {
    return true;
  }
  lastLiveReadMs = now;

  std::ifstream input(liveStatePath);
  if (!input.is_open()) {
    return true;
  }

  std::map<std::string, std::string> kv;
  std::string line;
  while (std::getline(input, line)) {
    const size_t sep = line.find('=');
    if (sep == std::string::npos) {
      continue;
    }
    kv[trim(line.substr(0, sep))] = trim(line.substr(sep + 1));
  }

  if (kv.empty()) {
    return true;
  }

  state.hasData = kv["has_data"] == "1";
  state.printing = kv["printing"] == "1";
  state.stale = kv["stale"] != "0";
  state.percent = kv["percent"].empty() ? state.percent : atoi(kv["percent"].c_str());
  state.remainingMinutes =
      kv["remaining_minutes"].empty() ? state.remainingMinutes : atoi(kv["remaining_minutes"].c_str());
  state.currentLayer =
      kv["current_layer"].empty() ? state.currentLayer : atoi(kv["current_layer"].c_str());
  state.totalLayers =
      kv["total_layers"].empty() ? state.totalLayers : atoi(kv["total_layers"].c_str());
  state.nozzleTemp =
      kv["nozzle_temp"].empty() ? state.nozzleTemp : static_cast<float>(atof(kv["nozzle_temp"].c_str()));
  state.nozzleTargetTemp = kv["nozzle_target_temp"].empty()
                               ? state.nozzleTargetTemp
                               : static_cast<float>(atof(kv["nozzle_target_temp"].c_str()));
  state.bedTemp =
      kv["bed_temp"].empty() ? state.bedTemp : static_cast<float>(atof(kv["bed_temp"].c_str()));
  state.bedTargetTemp = kv["bed_target_temp"].empty()
                            ? state.bedTargetTemp
                            : static_cast<float>(atof(kv["bed_target_temp"].c_str()));
  state.speedLevel =
      kv["speed_level"].empty() ? state.speedLevel : atoi(kv["speed_level"].c_str());
  state.stageCurrent =
      kv["stage_current"].empty() ? state.stageCurrent : atoi(kv["stage_current"].c_str());

  if (kv.count("stage_text")) copyToBuffer(kv["stage_text"], state.stageText, sizeof(state.stageText));
  if (kv.count("job_name")) copyToBuffer(kv["job_name"], state.jobName, sizeof(state.jobName));
  if (kv.count("wifi_signal")) copyToBuffer(kv["wifi_signal"], state.wifiSignal, sizeof(state.wifiSignal));
  if (kv.count("print_type")) copyToBuffer(kv["print_type"], state.printType, sizeof(state.printType));
  if (kv.count("last_command")) copyToBuffer(kv["last_command"], state.lastCommand, sizeof(state.lastCommand));

  state.lastUpdateMs = now;
  return true;
}

uint32_t millis32() {
  return SDL_GetTicks();
}

void initDisplay() {
  lv_init();
  sdl_init();

  lv_disp_draw_buf_init(&drawBuf, buf1, nullptr, 240 * 40);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.flush_cb = sdl_display_flush;
  dispDrv.draw_buf = &drawBuf;
  dispDrv.hor_res = 240;
  dispDrv.ver_res = 320;
  lv_disp_drv_register(&dispDrv);

  static lv_indev_drv_t mouseDrv;
  lv_indev_drv_init(&mouseDrv);
  mouseDrv.type = LV_INDEV_TYPE_POINTER;
  mouseDrv.read_cb = sdl_mouse_read;
  lv_indev_drv_register(&mouseDrv);

  static lv_indev_drv_t mouseWheelDrv;
  lv_indev_drv_init(&mouseWheelDrv);
  mouseWheelDrv.type = LV_INDEV_TYPE_ENCODER;
  mouseWheelDrv.read_cb = sdl_mousewheel_read;
  lv_indev_drv_register(&mouseWheelDrv);

  static lv_indev_drv_t keyboardDrv;
  lv_indev_drv_init(&keyboardDrv);
  keyboardDrv.type = LV_INDEV_TYPE_KEYPAD;
  keyboardDrv.read_cb = sdl_keyboard_read;
  lv_indev_drv_register(&keyboardDrv);
}

void updateMockState() {
  const uint32_t now = millis32();
  if ((now - lastMockUpdateMs) < 1000) {
    return;
  }

  lastMockUpdateMs = now;
  mockState.hasData = true;
  mockState.stale = false;
  mockState.printing = true;
  mockState.percent = (mockState.percent + 1) % 101;
  mockState.remainingMinutes = 245 - (mockState.percent * 2);
  mockState.currentLayer = mockState.percent;
  mockState.totalLayers = 100;
  mockState.nozzleTemp = 219.0f + (mockState.percent % 5);
  mockState.nozzleTargetTemp = 220.0f;
  mockState.bedTemp = 64.0f + (mockState.percent % 3);
  mockState.bedTargetTemp = 65.0f;
  mockState.speedLevel = 2;
  mockState.stageCurrent = 1;
  snprintf(mockState.stageText, sizeof(mockState.stageText), "%s",
           mockState.percent >= 100 ? "FINISH" : "RUNNING");
  snprintf(mockState.jobName, sizeof(mockState.jobName), "%s", "Benchy_A1_0.2mm_PLA");
  snprintf(mockState.wifiSignal, sizeof(mockState.wifiSignal), "%s", "-46dBm");
  snprintf(mockState.printType, sizeof(mockState.printType), "%s", "local");
  snprintf(mockState.lastCommand, sizeof(mockState.lastCommand), "%s", "push_status");
  mockState.lastUpdateMs = now;
}

}  // namespace

int main() {
  if (const char *path = getenv("BAMBU_LIVE_STATE_FILE")) {
    liveStatePath = path;
  }

  initDisplay();
  ui::init();
  ui::applyState(mockState);
  lastTickMs = millis32();

  while (true) {
    const uint32_t now = millis32();
    lv_tick_inc(now - lastTickMs);
    lastTickMs = now;

    if (liveStatePath.empty()) {
      updateMockState();
    } else {
      loadLiveState(mockState);
    }
    ui::applyState(mockState);
    lv_timer_handler();
    usleep(5000);
  }

  return 0;
}

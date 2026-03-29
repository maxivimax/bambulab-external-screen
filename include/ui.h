#pragma once

#include <lvgl.h>

#include "printer_state.h"

namespace ui {

enum class Action {
  StartTouchCalibration,
  SetBrightnessLow,
  SetBrightnessMid,
  SetBrightnessMax,
  PauseOrResumePrint,
  StopPrint,
  SetSpeedSilent,
  SetSpeedStandard,
  SetSpeedSport,
  SetSpeedLudicrous,
};

using ActionHandler = void (*)(Action action);

void init();
void applyState(const PrinterState &state);
void setActionHandler(ActionHandler handler);
void setBrightnessPercent(uint8_t percent);
void setTelegramStatus(const char *status, const char *username);
void showCalibrationStep(uint8_t step, uint8_t total, int x, int y);
void hideCalibration();

}  // namespace ui

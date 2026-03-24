#pragma once

#include <lvgl.h>

#include "printer_state.h"

namespace ui {

void init();
void applyState(const PrinterState &state);

}  // namespace ui


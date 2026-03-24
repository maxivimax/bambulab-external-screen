#pragma once

#include <stdint.h>
#include <string.h>

struct PrinterState {
  bool hasData;
  bool printing;
  bool stale;
  int percent;
  int remainingMinutes;
  int currentLayer;
  int totalLayers;
  float nozzleTemp;
  float nozzleTargetTemp;
  float bedTemp;
  float bedTargetTemp;
  int speedLevel;
  int stageCurrent;
  char stageText[32];
  char jobName[64];
  char wifiSignal[24];
  char printType[24];
  char lastCommand[24];
  uint32_t lastUpdateMs;
};

inline PrinterState makeDefaultPrinterState() {
  PrinterState state{};
  state.hasData = false;
  state.printing = false;
  state.stale = true;
  state.percent = 0;
  state.remainingMinutes = 0;
  state.currentLayer = 0;
  state.totalLayers = 0;
  state.nozzleTemp = 0.0f;
  state.nozzleTargetTemp = 0.0f;
  state.bedTemp = 0.0f;
  state.bedTargetTemp = 0.0f;
  state.speedLevel = 0;
  state.stageCurrent = -1;
  strcpy(state.stageText, "Waiting for data");
  strcpy(state.jobName, "-");
  strcpy(state.wifiSignal, "-");
  strcpy(state.printType, "-");
  strcpy(state.lastCommand, "-");
  state.lastUpdateMs = 0;
  return state;
}


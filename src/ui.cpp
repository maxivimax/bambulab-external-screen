#include "ui.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

namespace ui {
namespace {

constexpr lv_coord_t kScreenW = 320;
constexpr lv_coord_t kScreenH = 240;
constexpr lv_coord_t kHeaderH = 22;
constexpr lv_coord_t kPageY = 18;
constexpr lv_coord_t kPageH = 218;
constexpr lv_coord_t kPad = 12;

constexpr uint32_t kAccentColor = 0xFF7A86;
constexpr uint32_t kAccentSoft = 0x5A2B31;
constexpr uint32_t kLiveColor = 0xFF98A0;
constexpr uint32_t kTrackColor = 0x3A1B1F;
constexpr uint32_t kOfflineDebounceMs = 4000;
constexpr uint32_t kMenuAutoCloseMs = 10000;

enum class PageId : uint8_t { Summary, Heating, Temps, Details, Idle, Finish, Notice };
enum class ViewMode : uint8_t { Heating, Printing, Paused, Finished, Idle, Offline };
enum class MenuAction : uint8_t { AutoRotate, Summary, Temps, Details, Close };

struct PageSpec {
  PageId id;
  uint32_t durationMs;
};

constexpr PageSpec kPrintingPages[] = {
    {PageId::Summary, 7000}, {PageId::Temps, 7000}, {PageId::Details, 3000}};
constexpr PageSpec kHeatingPages[] = {
    {PageId::Heating, 7000}, {PageId::Summary, 4000}, {PageId::Temps, 6000}};
constexpr PageSpec kIdlePages[] = {{PageId::Idle, 8000}, {PageId::Temps, 6000}, {PageId::Details, 3000}};
constexpr PageSpec kPausedPages[] = {{PageId::Summary, 7000}, {PageId::Temps, 7000}, {PageId::Details, 3000}};
constexpr PageSpec kFinishedPages[] = {{PageId::Finish, 9000}, {PageId::Details, 4000}, {PageId::Temps, 5000}};
constexpr PageSpec kOfflinePages[] = {{PageId::Notice, 8000}};

lv_obj_t *screenObj = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *pageIndicatorTrack = nullptr;
lv_obj_t *pageIndicatorFill = nullptr;
lv_obj_t *tapLayer = nullptr;
lv_obj_t *menuOverlay = nullptr;
lv_obj_t *menuPanel = nullptr;
lv_obj_t *menuTitle = nullptr;
lv_obj_t *menuHint = nullptr;
lv_obj_t *menuAutoBtn = nullptr;
lv_obj_t *menuSummaryBtn = nullptr;
lv_obj_t *menuTempsBtn = nullptr;
lv_obj_t *menuDetailsBtn = nullptr;
lv_obj_t *menuBrightnessLowBtn = nullptr;
lv_obj_t *menuBrightnessMidBtn = nullptr;
lv_obj_t *menuBrightnessMaxBtn = nullptr;
lv_obj_t *menuCalibrateBtn = nullptr;
lv_obj_t *menuCloseBtn = nullptr;
lv_obj_t *calibrationOverlay = nullptr;
lv_obj_t *calibrationTitle = nullptr;
lv_obj_t *calibrationHint = nullptr;
lv_obj_t *calibrationStepLabel = nullptr;
lv_obj_t *calibrationCrossH = nullptr;
lv_obj_t *calibrationCrossV = nullptr;
lv_obj_t *calibrationStartBtn = nullptr;
lv_obj_t *calibrationCancelBtn = nullptr;

lv_obj_t *summaryPage = nullptr;
lv_obj_t *heatingPage = nullptr;
lv_obj_t *tempsPage = nullptr;
lv_obj_t *detailsPage = nullptr;
lv_obj_t *idlePage = nullptr;
lv_obj_t *finishPage = nullptr;
lv_obj_t *noticePage = nullptr;

lv_obj_t *summaryStage = nullptr;
lv_obj_t *summaryJob = nullptr;
lv_obj_t *summaryProgress = nullptr;
lv_obj_t *summaryPercent = nullptr;
lv_obj_t *summaryEta = nullptr;
lv_obj_t *summaryDoneAt = nullptr;
lv_obj_t *summaryLayers = nullptr;
lv_obj_t *summaryTimeline[4] = {};
lv_obj_t *summaryTimelineLabels[4] = {};

lv_obj_t *heatingTitle = nullptr;
lv_obj_t *heatingSubtitle = nullptr;
lv_obj_t *heatingNozzle = nullptr;
lv_obj_t *heatingBed = nullptr;

lv_obj_t *tempNozzle = nullptr;
lv_obj_t *tempBed = nullptr;
lv_obj_t *tempChart = nullptr;
lv_obj_t *tempLegendNozzle = nullptr;
lv_obj_t *tempLegendBed = nullptr;
lv_chart_series_t *tempNozzleSeries = nullptr;
lv_chart_series_t *tempBedSeries = nullptr;
lv_coord_t nozzleHistory[20] = {};
lv_coord_t bedHistory[20] = {};
int lastHistoryPercent = -1;
uint32_t lastHistorySampleMs = 0;

lv_obj_t *detailState = nullptr;
lv_obj_t *detailWifi = nullptr;
lv_obj_t *detailJob = nullptr;
lv_obj_t *detailType = nullptr;
lv_obj_t *detailSpeed = nullptr;

lv_obj_t *idleStatus = nullptr;
lv_obj_t *idleJob = nullptr;
lv_obj_t *idleTemp = nullptr;

lv_obj_t *finishTitle = nullptr;
lv_obj_t *finishJob = nullptr;
lv_obj_t *finishLayers = nullptr;
lv_obj_t *finishType = nullptr;

lv_obj_t *noticeTitle = nullptr;
lv_obj_t *noticeSubtitle = nullptr;
lv_obj_t *noticeMetaA = nullptr;

lv_style_t pageStyle;
lv_style_t cardStyle;
lv_style_t titleStyle;
lv_style_t valueStyle;
lv_style_t compactValueStyle;
lv_style_t heroStyle;
lv_style_t subtleStyle;
lv_style_t chipStyle;

const PageSpec *activePages = nullptr;
size_t activePageCount = 0;
size_t currentPageIndex = 0;
uint32_t pageStartedAtMs = 0;
bool pagerInitialized = false;
ViewMode currentMode = ViewMode::Idle;
uint32_t offlineCandidateSinceMs = 0;
bool menuOpen = false;
uint32_t lastMenuInteractionMs = 0;
bool manualPageActive = false;
PageId manualPageId = PageId::Summary;
bool calibrationVisible = false;
ActionHandler actionHandler = nullptr;
uint8_t currentBrightnessPercent = 100;

const char *effectiveStage(const PrinterState &state) {
  if (state.stageText[0] != '\0' && strcmp(state.stageText, "UNKNOWN") != 0) return state.stageText;
  return state.printing ? "Printing" : "Idle";
}

bool isPausedState(const char *stage) {
  return strcmp(stage, "PAUSE") == 0 || strcmp(stage, "PAUSED") == 0;
}

bool isFinishedState(const char *stage) {
  return strcmp(stage, "FINISH") == 0 || strcmp(stage, "FINISHED") == 0 || strcmp(stage, "COMPLETED") == 0;
}

bool isHeatingState(const PrinterState &state) {
  if (!state.printing) return false;
  const bool nozzleHeating = state.nozzleTargetTemp > 0.0f && (state.nozzleTargetTemp - state.nozzleTemp) > 8.0f;
  const bool bedHeating = state.bedTargetTemp > 0.0f && (state.bedTargetTemp - state.bedTemp) > 4.0f;
  return nozzleHeating || bedHeating;
}

const char *speedLabel(int level) {
  switch (level) {
    case 1: return "Silent";
    case 2: return "Standard";
    case 3: return "Sport";
    case 4: return "Ludicrous";
    default: return "Standard";
  }
}

void formatDuration(int minutes, char *out, size_t size) {
  if (minutes <= 0) {
    snprintf(out, size, "0m");
    return;
  }
  const int hours = minutes / 60;
  const int mins = minutes % 60;
  if (hours > 0) {
    snprintf(out, size, "%dh %02dm", hours, mins);
  } else {
    snprintf(out, size, "%dm", mins);
  }
}

void formatDisplayText(const char *input, char *output, size_t size) {
  if (size == 0) return;
  if (input == nullptr || input[0] == '\0') {
    snprintf(output, size, "-");
    return;
  }

  size_t out = 0;
  bool newWord = true;
  for (size_t i = 0; input[i] != '\0' && out + 1 < size; ++i) {
    char c = input[i];
    if (c == '_' || c == '-') {
      if (out > 0 && output[out - 1] != ' ') output[out++] = ' ';
      newWord = true;
      continue;
    }
    const unsigned char uc = static_cast<unsigned char>(c);
    output[out++] = newWord ? static_cast<char>(toupper(uc)) : static_cast<char>(tolower(uc));
    newWord = false;
  }
  output[out] = '\0';
}

void formatDoneAt(const PrinterState &state, char *output, size_t size) {
  if (state.remainingMinutes <= 0) {
    snprintf(output, size, "Done soon");
    return;
  }
  const uint32_t nowMin = lv_tick_get() / 60000U;
  const uint32_t doneMin = nowMin + static_cast<uint32_t>(state.remainingMinutes);
  snprintf(output, size, "%02lu:%02lu",
           static_cast<unsigned long>((doneMin / 60U) % 24U),
           static_cast<unsigned long>(doneMin % 60U));
}

void setLabelText(lv_obj_t *obj, const char *text) { lv_label_set_text(obj, text); }
void showOnly(PageId id);
void setMenuVisible(bool visible);

void setCalibrationVisible(bool visible) {
  calibrationVisible = visible;
  if (calibrationOverlay == nullptr) return;
  if (visible) {
    setMenuVisible(false);
    if (tapLayer != nullptr) lv_obj_add_flag(tapLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(calibrationOverlay);
  } else {
    lv_obj_add_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN);
    if (tapLayer != nullptr) lv_obj_clear_flag(tapLayer, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_width(pageIndicatorFill, 0);
}

void setMenuVisible(bool visible) {
  menuOpen = visible;
  if (menuOverlay == nullptr) return;
  if (visible) {
    lv_obj_clear_flag(menuOverlay, LV_OBJ_FLAG_HIDDEN);
    lastMenuInteractionMs = lv_tick_get();
  } else {
    lv_obj_add_flag(menuOverlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void touchMenu() { lastMenuInteractionMs = lv_tick_get(); }

void tapEventHandler(lv_event_t *event) {
  LV_UNUSED(event);
  if (!menuOpen) {
    setMenuVisible(true);
  } else {
    touchMenu();
  }
}

void setManualPage(PageId id) {
  manualPageActive = true;
  manualPageId = id;
  pageStartedAtMs = lv_tick_get();
  showOnly(id);
  lv_obj_set_width(pageIndicatorFill, 0);
}

void resumeAutoPaging() {
  manualPageActive = false;
  pageStartedAtMs = lv_tick_get();
  if (pagerInitialized && activePageCount > 0) {
    showOnly(activePages[currentPageIndex].id);
  }
}

void menuOverlayEventHandler(lv_event_t *event) {
  if (lv_event_get_target(event) == menuOverlay) {
    setMenuVisible(false);
  } else {
    touchMenu();
  }
}

void menuPanelEventHandler(lv_event_t *event) {
  LV_UNUSED(event);
  touchMenu();
}

void calibrateButtonHandler(lv_event_t *event) {
  LV_UNUSED(event);
  touchMenu();
  setMenuVisible(false);
  setCalibrationVisible(true);
  lv_label_set_text(calibrationTitle, "Start Calibration?");
  lv_label_set_text(calibrationHint, "Touch will be remapped after this step. Continue only if taps are off.");
  lv_label_set_text(calibrationStepLabel, "Confirm");
  lv_obj_add_flag(calibrationCrossH, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calibrationCrossV, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(calibrationStartBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(calibrationCancelBtn, LV_OBJ_FLAG_HIDDEN);
}

void calibrationStartHandler(lv_event_t *event) {
  LV_UNUSED(event);
  if (actionHandler != nullptr) {
    actionHandler(Action::StartTouchCalibration);
  }
}

void calibrationCancelHandler(lv_event_t *event) {
  LV_UNUSED(event);
  hideCalibration();
}

void brightnessButtonHandler(lv_event_t *event) {
  touchMenu();
  const auto action = static_cast<Action>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (actionHandler != nullptr) {
    actionHandler(action);
  }
}

void menuActionHandler(lv_event_t *event) {
  touchMenu();
  const auto action =
      static_cast<MenuAction>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  switch (action) {
    case MenuAction::AutoRotate:
      resumeAutoPaging();
      setMenuVisible(false);
      break;
    case MenuAction::Summary:
      setManualPage(PageId::Summary);
      setMenuVisible(false);
      break;
    case MenuAction::Temps:
      setManualPage(PageId::Temps);
      setMenuVisible(false);
      break;
    case MenuAction::Details:
      setManualPage(PageId::Details);
      setMenuVisible(false);
      break;
    case MenuAction::Close:
      setMenuVisible(false);
      break;
  }
}

lv_obj_t *createMenuButton(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                           const char *text, MenuAction action) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_add_style(btn, &cardStyle, 0);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, menuActionHandler, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(action)));

  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

lv_obj_t *createActionButton(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                             const char *text, Action action) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_remove_style_all(btn);
  lv_obj_add_style(btn, &cardStyle, 0);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, brightnessButtonHandler, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(action)));

  lv_obj_t *label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}

void buildCalibrationOverlay() {
  calibrationOverlay = lv_obj_create(screenObj);
  lv_obj_remove_style_all(calibrationOverlay);
  lv_obj_set_size(calibrationOverlay, kScreenW, kScreenH);
  lv_obj_set_pos(calibrationOverlay, 0, 0);
  lv_obj_set_style_bg_color(calibrationOverlay, lv_color_hex(0x05070A), 0);
  lv_obj_set_style_bg_opa(calibrationOverlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN);

  calibrationTitle = lv_label_create(calibrationOverlay);
  lv_obj_set_style_text_font(calibrationTitle, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(calibrationTitle, lv_color_hex(0xF5F7FA), 0);
  lv_obj_set_pos(calibrationTitle, 14, 14);
  lv_label_set_text(calibrationTitle, "Touch Calibration");

  calibrationHint = lv_label_create(calibrationOverlay);
  lv_obj_set_style_text_font(calibrationHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(calibrationHint, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(calibrationHint, 290);
  lv_label_set_long_mode(calibrationHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(calibrationHint, 14, 44);
  lv_label_set_text(calibrationHint, "Tap the pink cross, then release.");

  calibrationStepLabel = lv_label_create(calibrationOverlay);
  lv_obj_add_style(calibrationStepLabel, &titleStyle, 0);
  lv_obj_set_pos(calibrationStepLabel, 14, 84);
  lv_label_set_text(calibrationStepLabel, "1 / 4");

  calibrationStartBtn = lv_btn_create(calibrationOverlay);
  lv_obj_remove_style_all(calibrationStartBtn);
  lv_obj_add_style(calibrationStartBtn, &cardStyle, 0);
  lv_obj_set_size(calibrationStartBtn, 132, 38);
  lv_obj_set_pos(calibrationStartBtn, 14, 120);
  lv_obj_add_flag(calibrationStartBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calibrationStartBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(calibrationStartBtn, calibrationStartHandler, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *startLabel = lv_label_create(calibrationStartBtn);
  lv_obj_set_style_text_font(startLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(startLabel, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(startLabel, "Start");
  lv_obj_center(startLabel);

  calibrationCancelBtn = lv_btn_create(calibrationOverlay);
  lv_obj_remove_style_all(calibrationCancelBtn);
  lv_obj_add_style(calibrationCancelBtn, &cardStyle, 0);
  lv_obj_set_size(calibrationCancelBtn, 132, 38);
  lv_obj_set_pos(calibrationCancelBtn, 174, 120);
  lv_obj_add_flag(calibrationCancelBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calibrationCancelBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(calibrationCancelBtn, calibrationCancelHandler, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancelLabel = lv_label_create(calibrationCancelBtn);
  lv_obj_set_style_text_font(cancelLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(cancelLabel, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(cancelLabel, "Cancel");
  lv_obj_center(cancelLabel);

  calibrationCrossH = lv_obj_create(calibrationOverlay);
  lv_obj_remove_style_all(calibrationCrossH);
  lv_obj_set_size(calibrationCrossH, 26, 2);
  lv_obj_set_style_bg_color(calibrationCrossH, lv_color_hex(kAccentColor), 0);
  lv_obj_set_style_bg_opa(calibrationCrossH, LV_OPA_COVER, 0);
  lv_obj_add_flag(calibrationCrossH, LV_OBJ_FLAG_HIDDEN);

  calibrationCrossV = lv_obj_create(calibrationOverlay);
  lv_obj_remove_style_all(calibrationCrossV);
  lv_obj_set_size(calibrationCrossV, 2, 26);
  lv_obj_set_style_bg_color(calibrationCrossV, lv_color_hex(kAccentColor), 0);
  lv_obj_set_style_bg_opa(calibrationCrossV, LV_OPA_COVER, 0);
  lv_obj_add_flag(calibrationCrossV, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *createPage() {
  lv_obj_t *page = lv_obj_create(screenObj);
  lv_obj_remove_style_all(page);
  lv_obj_add_style(page, &pageStyle, 0);
  lv_obj_set_pos(page, 0, kPageY);
  lv_obj_set_size(page, kScreenW, kPageH);
  return page;
}

lv_obj_t *createCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &cardStyle, 0);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  return card;
}

lv_obj_t *createCardTitle(lv_obj_t *card, const char *title) {
  lv_obj_t *label = lv_label_create(card);
  lv_obj_add_style(label, &titleStyle, 0);
  lv_label_set_text(label, title);
  lv_obj_set_pos(label, 8, 8);
  return label;
}

lv_obj_t *createCardValue(lv_obj_t *card, lv_coord_t y, lv_style_t *style, lv_coord_t width) {
  lv_obj_t *label = lv_label_create(card);
  lv_obj_add_style(label, style, 0);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(label, 8, y);
  lv_label_set_text(label, "-");
  return label;
}

void showOnly(PageId id) {
  lv_obj_add_flag(summaryPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(heatingPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tempsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(detailsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(idlePage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(finishPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(noticePage, LV_OBJ_FLAG_HIDDEN);

  switch (id) {
    case PageId::Summary: lv_obj_clear_flag(summaryPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Heating: lv_obj_clear_flag(heatingPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Temps: lv_obj_clear_flag(tempsPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Details: lv_obj_clear_flag(detailsPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Idle: lv_obj_clear_flag(idlePage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Finish: lv_obj_clear_flag(finishPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Notice: lv_obj_clear_flag(noticePage, LV_OBJ_FLAG_HIDDEN); break;
  }
}

void setActiveSequence(ViewMode mode) {
  currentMode = mode;
  switch (mode) {
    case ViewMode::Heating: activePages = kHeatingPages; activePageCount = sizeof(kHeatingPages) / sizeof(kHeatingPages[0]); break;
    case ViewMode::Printing: activePages = kPrintingPages; activePageCount = sizeof(kPrintingPages) / sizeof(kPrintingPages[0]); break;
    case ViewMode::Paused: activePages = kPausedPages; activePageCount = sizeof(kPausedPages) / sizeof(kPausedPages[0]); break;
    case ViewMode::Finished: activePages = kFinishedPages; activePageCount = sizeof(kFinishedPages) / sizeof(kFinishedPages[0]); break;
    case ViewMode::Idle: activePages = kIdlePages; activePageCount = sizeof(kIdlePages) / sizeof(kIdlePages[0]); break;
    case ViewMode::Offline: activePages = kOfflinePages; activePageCount = sizeof(kOfflinePages) / sizeof(kOfflinePages[0]); break;
  }
  currentPageIndex = 0;
  pageStartedAtMs = lv_tick_get();
  pagerInitialized = true;
  showOnly(manualPageActive ? manualPageId : activePages[0].id);
}

void updatePager() {
  if (!pagerInitialized || activePageCount == 0) return;
  if (menuOpen || manualPageActive || calibrationVisible) {
    lv_obj_set_width(pageIndicatorFill, 0);
    return;
  }
  const uint32_t now = lv_tick_get();
  const uint32_t duration = activePages[currentPageIndex].durationMs;
  if ((now - pageStartedAtMs) >= duration) {
    currentPageIndex = (currentPageIndex + 1) % activePageCount;
    pageStartedAtMs = now;
    showOnly(activePages[currentPageIndex].id);
  }
  const uint32_t progress = ((now - pageStartedAtMs) * kScreenW) / duration;
  lv_obj_set_width(pageIndicatorFill, progress > kScreenW ? kScreenW : progress);
}

void updateMenuTimeout() {
  if (!menuOpen) return;
  const uint32_t now = lv_tick_get();
  if ((now - lastMenuInteractionMs) >= kMenuAutoCloseMs) {
    setMenuVisible(false);
  }
}

ViewMode detectMode(const PrinterState &state) {
  const char *stage = effectiveStage(state);
  if (state.stale || !state.hasData) {
    const uint32_t now = lv_tick_get();
    if (offlineCandidateSinceMs == 0) offlineCandidateSinceMs = now;
    if ((now - offlineCandidateSinceMs) >= kOfflineDebounceMs) return ViewMode::Offline;
    return currentMode == ViewMode::Offline ? ViewMode::Offline : ViewMode::Idle;
  }
  offlineCandidateSinceMs = 0;
  if (isPausedState(stage)) return ViewMode::Paused;
  if (isFinishedState(stage)) return ViewMode::Finished;
  if (isHeatingState(state)) return ViewMode::Heating;
  if (state.printing) return ViewMode::Printing;
  return ViewMode::Idle;
}

void initStyles() {
  lv_style_init(&pageStyle);
  lv_style_set_bg_opa(&pageStyle, LV_OPA_TRANSP);
  lv_style_set_border_width(&pageStyle, 0);
  lv_style_set_pad_all(&pageStyle, 0);

  lv_style_init(&cardStyle);
  lv_style_set_radius(&cardStyle, 10);
  lv_style_set_bg_color(&cardStyle, lv_color_hex(0x101419));
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_color(&cardStyle, lv_color_hex(0x2C3642));
  lv_style_set_border_width(&cardStyle, 1);

  lv_style_init(&titleStyle);
  lv_style_set_text_color(&titleStyle, lv_color_hex(0x93A1B2));
  lv_style_set_text_font(&titleStyle, &lv_font_montserrat_12);

  lv_style_init(&valueStyle);
  lv_style_set_text_color(&valueStyle, lv_color_hex(0xF5F7FA));
  lv_style_set_text_font(&valueStyle, &lv_font_montserrat_18);

  lv_style_init(&compactValueStyle);
  lv_style_set_text_color(&compactValueStyle, lv_color_hex(0xF5F7FA));
  lv_style_set_text_font(&compactValueStyle, &lv_font_montserrat_14);

  lv_style_init(&heroStyle);
  lv_style_set_text_color(&heroStyle, lv_color_hex(0xF5F7FA));
  lv_style_set_text_font(&heroStyle, &lv_font_montserrat_24);

  lv_style_init(&subtleStyle);
  lv_style_set_text_color(&subtleStyle, lv_color_hex(0x93A1B2));
  lv_style_set_text_font(&subtleStyle, &lv_font_montserrat_12);

  lv_style_init(&chipStyle);
  lv_style_set_bg_color(&chipStyle, lv_color_hex(0x2A2230));
  lv_style_set_bg_opa(&chipStyle, LV_OPA_COVER);
  lv_style_set_radius(&chipStyle, LV_RADIUS_CIRCLE);
}

void updateMenuButtons() {
  if (menuAutoBtn == nullptr) return;
  const lv_color_t activeBg = lv_color_hex(kAccentSoft);
  const lv_color_t inactiveBg = lv_color_hex(0x101419);

  lv_obj_set_style_bg_color(menuAutoBtn, !manualPageActive ? activeBg : inactiveBg, 0);
  lv_obj_set_style_bg_color(menuSummaryBtn,
                            manualPageActive && manualPageId == PageId::Summary ? activeBg : inactiveBg,
                            0);
  lv_obj_set_style_bg_color(menuTempsBtn,
                            manualPageActive && manualPageId == PageId::Temps ? activeBg : inactiveBg, 0);
  lv_obj_set_style_bg_color(menuDetailsBtn,
                            manualPageActive && manualPageId == PageId::Details ? activeBg : inactiveBg,
                            0);
  if (menuBrightnessLowBtn != nullptr)
    lv_obj_set_style_bg_color(menuBrightnessLowBtn, currentBrightnessPercent <= 35 ? activeBg : inactiveBg, 0);
  if (menuBrightnessMidBtn != nullptr)
    lv_obj_set_style_bg_color(menuBrightnessMidBtn,
                              currentBrightnessPercent > 35 && currentBrightnessPercent < 85 ? activeBg
                                                                                                : inactiveBg,
                              0);
  if (menuBrightnessMaxBtn != nullptr)
    lv_obj_set_style_bg_color(menuBrightnessMaxBtn, currentBrightnessPercent >= 85 ? activeBg : inactiveBg, 0);
  if (menuCalibrateBtn != nullptr) lv_obj_set_style_bg_color(menuCalibrateBtn, inactiveBg, 0);
}

void buildSummaryPage() {
  summaryPage = createPage();

  summaryStage = lv_label_create(summaryPage);
  lv_obj_set_style_text_font(summaryStage, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(summaryStage, lv_color_hex(0xF5F7FA), 0);
  lv_obj_set_width(summaryStage, 220);
  lv_label_set_long_mode(summaryStage, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(summaryStage, kPad, 8);

  summaryPercent = lv_label_create(summaryPage);
  lv_obj_add_style(summaryPercent, &heroStyle, 0);
  lv_obj_align(summaryPercent, LV_ALIGN_TOP_RIGHT, -kPad, 6);

  summaryJob = lv_label_create(summaryPage);
  lv_obj_add_style(summaryJob, &subtleStyle, 0);
  lv_obj_set_width(summaryJob, 296);
  lv_label_set_long_mode(summaryJob, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(summaryJob, kPad, 34);

  summaryProgress = lv_bar_create(summaryPage);
  lv_obj_set_size(summaryProgress, 296, 16);
  lv_obj_set_pos(summaryProgress, kPad, 56);
  lv_obj_set_style_bg_color(summaryProgress, lv_color_hex(0x202734), LV_PART_MAIN);
  lv_obj_set_style_bg_color(summaryProgress, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
  lv_obj_set_style_radius(summaryProgress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(summaryProgress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_bar_set_range(summaryProgress, 0, 100);

  lv_obj_t *etaCard = createCard(summaryPage, 12, 90, 144, 72);
  createCardTitle(etaCard, "Remaining");
  summaryEta = createCardValue(etaCard, 24, &valueStyle, 128);
  summaryDoneAt = createCardValue(etaCard, 46, &titleStyle, 128);

  lv_obj_t *layerCard = createCard(summaryPage, 164, 90, 144, 72);
  createCardTitle(layerCard, "Layers");
  summaryLayers = createCardValue(layerCard, 24, &valueStyle, 128);

  static const char *timelineNames[4] = {"Heat", "Print", "Pause", "Done"};
  for (int i = 0; i < 4; ++i) {
    summaryTimeline[i] = lv_obj_create(summaryPage);
    lv_obj_remove_style_all(summaryTimeline[i]);
    lv_obj_add_style(summaryTimeline[i], &chipStyle, 0);
    lv_obj_set_size(summaryTimeline[i], 56, 4);
    lv_obj_set_pos(summaryTimeline[i], 12 + (i * 76), 180);

    summaryTimelineLabels[i] = lv_label_create(summaryPage);
    lv_obj_add_style(summaryTimelineLabels[i], &titleStyle, 0);
    lv_label_set_text(summaryTimelineLabels[i], timelineNames[i]);
    lv_obj_set_pos(summaryTimelineLabels[i], 12 + (i * 76), 188);
  }
}

void buildHeatingPage() {
  heatingPage = createPage();

  heatingTitle = lv_label_create(heatingPage);
  lv_obj_add_style(heatingTitle, &heroStyle, 0);
  lv_obj_set_pos(heatingTitle, kPad, 18);
  lv_label_set_text(heatingTitle, "Heating");

  heatingSubtitle = lv_label_create(heatingPage);
  lv_obj_set_style_text_font(heatingSubtitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(heatingSubtitle, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(heatingSubtitle, 296);
  lv_label_set_long_mode(heatingSubtitle, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(heatingSubtitle, kPad, 54);

  lv_obj_t *nozzleCard = createCard(heatingPage, 12, 100, 144, 72);
  createCardTitle(nozzleCard, "Nozzle");
  heatingNozzle = createCardValue(nozzleCard, 26, &valueStyle, 128);

  lv_obj_t *bedCard = createCard(heatingPage, 164, 100, 144, 72);
  createCardTitle(bedCard, "Bed");
  heatingBed = createCardValue(bedCard, 26, &valueStyle, 128);
}

void buildTempsPage() {
  tempsPage = createPage();

  lv_obj_t *title = lv_label_create(tempsPage);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(title, "Temperatures");
  lv_obj_set_pos(title, kPad, 8);

  tempChart = lv_chart_create(tempsPage);
  lv_obj_set_size(tempChart, 206, 120);
  lv_obj_set_pos(tempChart, 12, 36);
  lv_obj_set_style_bg_opa(tempChart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tempChart, 0, 0);
  lv_obj_set_style_line_color(tempChart, lv_color_hex(0x312838), LV_PART_MAIN);
  lv_obj_set_style_line_opa(tempChart, LV_OPA_40, LV_PART_MAIN);
  lv_chart_set_type(tempChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(tempChart, 20);
  lv_chart_set_range(tempChart, LV_CHART_AXIS_PRIMARY_Y, 0, 260);
  lv_chart_set_div_line_count(tempChart, 2, 4);
  tempNozzleSeries = lv_chart_add_series(tempChart, lv_color_hex(kAccentColor), LV_CHART_AXIS_PRIMARY_Y);
  tempBedSeries = lv_chart_add_series(tempChart, lv_color_hex(0x7DD3FC), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_ext_y_array(tempChart, tempNozzleSeries, nozzleHistory);
  lv_chart_set_ext_y_array(tempChart, tempBedSeries, bedHistory);

  tempLegendNozzle = lv_label_create(tempsPage);
  lv_obj_add_style(tempLegendNozzle, &titleStyle, 0);
  lv_label_set_text(tempLegendNozzle, "Pink: Nozzle");
  lv_obj_set_pos(tempLegendNozzle, 12, 164);

  tempLegendBed = lv_label_create(tempsPage);
  lv_obj_add_style(tempLegendBed, &titleStyle, 0);
  lv_label_set_text(tempLegendBed, "Blue: Bed");
  lv_obj_set_pos(tempLegendBed, 110, 164);

  lv_obj_t *nozzleCard = createCard(tempsPage, 226, 36, 82, 62);
  createCardTitle(nozzleCard, "Nozzle");
  tempNozzle = createCardValue(nozzleCard, 26, &valueStyle, 66);

  lv_obj_t *bedCard = createCard(tempsPage, 226, 106, 82, 62);
  createCardTitle(bedCard, "Bed");
  tempBed = createCardValue(bedCard, 26, &valueStyle, 66);
}

void buildDetailsPage() {
  detailsPage = createPage();

  lv_obj_t *title = lv_label_create(detailsPage);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(title, "Details");
  lv_obj_set_pos(title, kPad, 8);

  lv_obj_t *stateCard = createCard(detailsPage, 12, 36, 144, 48);
  createCardTitle(stateCard, "State");
  detailState = createCardValue(stateCard, 22, &compactValueStyle, 128);

  lv_obj_t *wifiCard = createCard(detailsPage, 164, 36, 144, 48);
  createCardTitle(wifiCard, "WiFi");
  detailWifi = createCardValue(wifiCard, 22, &compactValueStyle, 128);

  lv_obj_t *jobCard = createCard(detailsPage, 12, 92, 296, 52);
  createCardTitle(jobCard, "Job");
  detailJob = createCardValue(jobCard, 22, &compactValueStyle, 280);

  lv_obj_t *typeCard = createCard(detailsPage, 12, 152, 144, 56);
  createCardTitle(typeCard, "Type");
  detailType = createCardValue(typeCard, 22, &compactValueStyle, 128);

  lv_obj_t *speedCard = createCard(detailsPage, 164, 152, 144, 56);
  createCardTitle(speedCard, "Speed");
  detailSpeed = createCardValue(speedCard, 22, &compactValueStyle, 128);
}

void buildIdlePage() {
  idlePage = createPage();

  idleStatus = lv_label_create(idlePage);
  lv_obj_add_style(idleStatus, &heroStyle, 0);
  lv_obj_set_pos(idleStatus, 12, 18);
  lv_label_set_text(idleStatus, "Ready");

  idleJob = lv_label_create(idlePage);
  lv_obj_set_style_text_font(idleJob, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(idleJob, lv_color_hex(0xC8D0DA), 0);
  lv_obj_set_width(idleJob, 296);
  lv_label_set_long_mode(idleJob, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(idleJob, 12, 54);

  lv_obj_t *tempCard = createCard(idlePage, 12, 112, 296, 54);
  createCardTitle(tempCard, "Temperatures");
  idleTemp = createCardValue(tempCard, 22, &valueStyle, 280);
}

void buildFinishPage() {
  finishPage = createPage();

  finishTitle = lv_label_create(finishPage);
  lv_obj_add_style(finishTitle, &heroStyle, 0);
  lv_obj_set_pos(finishTitle, 12, 18);
  lv_label_set_text(finishTitle, "Print Finished");

  finishJob = lv_label_create(finishPage);
  lv_obj_set_style_text_font(finishJob, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(finishJob, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(finishJob, 296);
  lv_label_set_long_mode(finishJob, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(finishJob, 12, 54);

  lv_obj_t *layersCard = createCard(finishPage, 12, 112, 144, 60);
  createCardTitle(layersCard, "Layers");
  finishLayers = createCardValue(layersCard, 24, &valueStyle, 128);

  lv_obj_t *typeCard = createCard(finishPage, 164, 112, 144, 60);
  createCardTitle(typeCard, "Type");
  finishType = createCardValue(typeCard, 24, &compactValueStyle, 128);
}

void buildNoticePage() {
  noticePage = createPage();

  noticeTitle = lv_label_create(noticePage);
  lv_obj_add_style(noticeTitle, &heroStyle, 0);
  lv_obj_set_width(noticeTitle, 296);
  lv_obj_set_pos(noticeTitle, 12, 20);

  noticeSubtitle = lv_label_create(noticePage);
  lv_obj_set_style_text_font(noticeSubtitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(noticeSubtitle, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(noticeSubtitle, 296);
  lv_label_set_long_mode(noticeSubtitle, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(noticeSubtitle, 12, 56);

  lv_obj_t *metaA = createCard(noticePage, 12, 146, 296, 56);
  createCardTitle(metaA, "Status");
  noticeMetaA = createCardValue(metaA, 22, &compactValueStyle, 280);
}

void updateNoticePage(const PrinterState &state) {
  char prettyState[48];
  char layersText[32];
  char timeText[32];
  formatDisplayText(effectiveStage(state), prettyState, sizeof(prettyState));
  snprintf(layersText, sizeof(layersText), "%d/%d", state.currentLayer, state.totalLayers);
  formatDuration(state.remainingMinutes, timeText, sizeof(timeText));

  switch (currentMode) {
    case ViewMode::Offline:
      setLabelText(noticeTitle, "Printer Offline");
      setLabelText(noticeSubtitle, "No fresh LAN/MQTT data. Waiting for the printer to respond.");
      setLabelText(noticeMetaA, "Stale");
      break;
    case ViewMode::Paused:
      setLabelText(noticeTitle, "Print Paused");
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, layersText);
      break;
    case ViewMode::Finished:
      setLabelText(noticeTitle, "Print Finished");
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, layersText);
      break;
    case ViewMode::Heating:
    case ViewMode::Printing:
    case ViewMode::Idle:
      setLabelText(noticeTitle, prettyState);
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, timeText);
      break;
  }
}

}  // namespace

void init() {
  initStyles();

  screenObj = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screenObj);
  lv_obj_set_style_bg_color(screenObj, lv_color_hex(0x05070A), 0);
  lv_obj_set_style_bg_opa(screenObj, LV_OPA_COVER, 0);

  statusLabel = lv_label_create(screenObj);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_12, 0);
  lv_label_set_text(statusLabel, "LIVE");
  lv_obj_align(statusLabel, LV_ALIGN_TOP_RIGHT, -12, 6);

  buildSummaryPage();
  buildHeatingPage();
  buildTempsPage();
  buildDetailsPage();
  buildIdlePage();
  buildFinishPage();
  buildNoticePage();
  buildCalibrationOverlay();

  tapLayer = lv_obj_create(screenObj);
  lv_obj_remove_style_all(tapLayer);
  lv_obj_set_size(tapLayer, kScreenW, kScreenH);
  lv_obj_set_pos(tapLayer, 0, 0);
  lv_obj_set_style_bg_opa(tapLayer, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(tapLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tapLayer, tapEventHandler, LV_EVENT_CLICKED, nullptr);

  menuOverlay = lv_obj_create(screenObj);
  lv_obj_remove_style_all(menuOverlay);
  lv_obj_set_size(menuOverlay, kScreenW, kScreenH);
  lv_obj_set_pos(menuOverlay, 0, 0);
  lv_obj_set_style_bg_color(menuOverlay, lv_color_hex(0x030407), 0);
  lv_obj_set_style_bg_opa(menuOverlay, LV_OPA_70, 0);
  lv_obj_add_flag(menuOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(menuOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(menuOverlay, menuOverlayEventHandler, LV_EVENT_CLICKED, nullptr);

  menuPanel = lv_obj_create(menuOverlay);
  lv_obj_remove_style_all(menuPanel);
  lv_obj_add_style(menuPanel, &cardStyle, 0);
  lv_obj_set_size(menuPanel, 248, 254);
  lv_obj_center(menuPanel);
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(menuPanel, menuPanelEventHandler, LV_EVENT_CLICKED, nullptr);

  menuTitle = lv_label_create(menuPanel);
  lv_obj_set_style_text_font(menuTitle, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(menuTitle, lv_color_hex(0xF5F7FA), 0);
  lv_obj_set_pos(menuTitle, 14, 14);
  lv_label_set_text(menuTitle, "Menu");

  menuCloseBtn = createMenuButton(menuPanel, 194, 10, 40, 32, "X", MenuAction::Close);
  lv_obj_set_style_bg_color(menuCloseBtn, lv_color_hex(0x2A1620), 0);

  menuHint = lv_label_create(menuPanel);
  lv_obj_set_style_text_font(menuHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(menuHint, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(menuHint, 220);
  lv_label_set_long_mode(menuHint, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(menuHint, 14, 46);
  lv_label_set_text(menuHint, "Pick a screen, set brightness, or run touch calibration.");

  menuAutoBtn = createMenuButton(menuPanel, 14, 88, 104, 30, "Auto", MenuAction::AutoRotate);
  menuSummaryBtn = createMenuButton(menuPanel, 130, 88, 104, 30, "Summary", MenuAction::Summary);
  menuTempsBtn = createMenuButton(menuPanel, 14, 128, 104, 30, "Temps", MenuAction::Temps);
  menuDetailsBtn = createMenuButton(menuPanel, 130, 128, 104, 30, "Details", MenuAction::Details);
  menuBrightnessLowBtn = createActionButton(menuPanel, 14, 168, 68, 30, "Low", Action::SetBrightnessLow);
  menuBrightnessMidBtn = createActionButton(menuPanel, 90, 168, 68, 30, "Mid", Action::SetBrightnessMid);
  menuBrightnessMaxBtn = createActionButton(menuPanel, 166, 168, 68, 30, "Max", Action::SetBrightnessMax);
  menuCalibrateBtn = lv_btn_create(menuPanel);
  lv_obj_remove_style_all(menuCalibrateBtn);
  lv_obj_add_style(menuCalibrateBtn, &cardStyle, 0);
  lv_obj_set_size(menuCalibrateBtn, 220, 34);
  lv_obj_set_pos(menuCalibrateBtn, 14, 208);
  lv_obj_add_flag(menuCalibrateBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(menuCalibrateBtn, calibrateButtonHandler, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *calLabel = lv_label_create(menuCalibrateBtn);
  lv_obj_set_style_text_font(calLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(calLabel, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(calLabel, "Calibrate Touch");
  lv_obj_center(calLabel);
  updateMenuButtons();

  pageIndicatorTrack = lv_obj_create(screenObj);
  lv_obj_remove_style_all(pageIndicatorTrack);
  lv_obj_set_size(pageIndicatorTrack, kScreenW, 2);
  lv_obj_set_pos(pageIndicatorTrack, 0, kScreenH - 2);
  lv_obj_set_style_bg_color(pageIndicatorTrack, lv_color_hex(kTrackColor), 0);
  lv_obj_set_style_bg_opa(pageIndicatorTrack, LV_OPA_COVER, 0);

  pageIndicatorFill = lv_obj_create(screenObj);
  lv_obj_remove_style_all(pageIndicatorFill);
  lv_obj_set_size(pageIndicatorFill, 0, 2);
  lv_obj_set_pos(pageIndicatorFill, 0, kScreenH - 2);
  lv_obj_set_style_bg_color(pageIndicatorFill, lv_color_hex(kAccentColor), 0);
  lv_obj_set_style_bg_opa(pageIndicatorFill, LV_OPA_COVER, 0);

  setActiveSequence(ViewMode::Idle);
  lv_scr_load(screenObj);
}

void applyState(const PrinterState &state) {
  char buffer[96];
  char prettyState[48];
  char prettyType[48];
  char doneAtText[32];
  char idleTempText[48];
  char heatingText[48];

  const ViewMode mode = detectMode(state);
  if (mode != currentMode || !pagerInitialized) setActiveSequence(mode);
  updateMenuButtons();

  lv_obj_set_style_text_color(statusLabel, state.stale ? lv_color_hex(0xD16B72) : lv_color_hex(kLiveColor), 0);
  lv_label_set_text(statusLabel, state.stale ? "STALE" : "LIVE");

  formatDisplayText(effectiveStage(state), prettyState, sizeof(prettyState));
  formatDisplayText(state.printType, prettyType, sizeof(prettyType));
  formatDoneAt(state, doneAtText, sizeof(doneAtText));

  setLabelText(summaryStage, prettyState);
  setLabelText(summaryJob, state.jobName);
  lv_bar_set_value(summaryProgress, state.percent, LV_ANIM_OFF);
  snprintf(buffer, sizeof(buffer), "%d%%", state.percent);
  setLabelText(summaryPercent, buffer);
  formatDuration(state.remainingMinutes, buffer, sizeof(buffer));
  setLabelText(summaryEta, buffer);
  snprintf(buffer, sizeof(buffer), "Done %s", doneAtText);
  setLabelText(summaryDoneAt, buffer);
  snprintf(buffer, sizeof(buffer), "%d/%d", state.currentLayer, state.totalLayers);
  setLabelText(summaryLayers, buffer);

  for (int i = 0; i < 4; ++i) lv_obj_set_style_bg_color(summaryTimeline[i], lv_color_hex(0x2A2230), 0);
  lv_obj_set_style_bg_color(summaryTimeline[0], isHeatingState(state) ? lv_color_hex(kAccentColor) : lv_color_hex(kAccentSoft), 0);
  lv_obj_set_style_bg_color(summaryTimeline[1], state.printing && !isPausedState(effectiveStage(state)) && !isFinishedState(effectiveStage(state)) && !isHeatingState(state) ? lv_color_hex(kAccentColor) : lv_color_hex(kAccentSoft), 0);
  lv_obj_set_style_bg_color(summaryTimeline[2], isPausedState(effectiveStage(state)) ? lv_color_hex(kAccentColor) : lv_color_hex(kAccentSoft), 0);
  lv_obj_set_style_bg_color(summaryTimeline[3], isFinishedState(effectiveStage(state)) ? lv_color_hex(kAccentColor) : lv_color_hex(kAccentSoft), 0);

  snprintf(buffer, sizeof(buffer), "%.0f/%.0fC", state.nozzleTemp, state.nozzleTargetTemp);
  setLabelText(heatingNozzle, buffer);
  setLabelText(tempNozzle, buffer);
  snprintf(buffer, sizeof(buffer), "%.0f/%.0fC", state.bedTemp, state.bedTargetTemp);
  setLabelText(heatingBed, buffer);
  setLabelText(tempBed, buffer);

  if (mode == ViewMode::Heating) {
    if ((state.nozzleTargetTemp - state.nozzleTemp) >= (state.bedTargetTemp - state.bedTemp)) {
      snprintf(heatingText, sizeof(heatingText), "Nozzle to %.0fC", state.nozzleTargetTemp);
    } else {
      snprintf(heatingText, sizeof(heatingText), "Bed to %.0fC", state.bedTargetTemp);
    }
  } else {
    snprintf(heatingText, sizeof(heatingText), "%s", state.jobName);
  }
  setLabelText(heatingSubtitle, heatingText);

  if (state.percent != lastHistoryPercent || (lv_tick_get() - lastHistorySampleMs) >= 5000U) {
    for (int i = 0; i < 19; ++i) {
      nozzleHistory[i] = nozzleHistory[i + 1];
      bedHistory[i] = bedHistory[i + 1];
    }
    nozzleHistory[19] = static_cast<lv_coord_t>(state.nozzleTemp);
    bedHistory[19] = static_cast<lv_coord_t>(state.bedTemp);
    lastHistoryPercent = state.percent;
    lastHistorySampleMs = lv_tick_get();
    if (tempChart != nullptr) lv_chart_refresh(tempChart);
  }

  setLabelText(detailState, prettyState);
  setLabelText(detailWifi, state.wifiSignal);
  setLabelText(detailJob, state.jobName);
  setLabelText(detailType, prettyType);
  setLabelText(detailSpeed, speedLabel(state.speedLevel));

  setLabelText(idleStatus, mode == ViewMode::Idle ? "Ready" : prettyState);
  setLabelText(idleJob, state.jobName);
  snprintf(idleTempText, sizeof(idleTempText), "%.0fC nozzle  %.0fC bed", state.nozzleTemp, state.bedTemp);
  setLabelText(idleTemp, idleTempText);

  setLabelText(finishJob, state.jobName);
  snprintf(buffer, sizeof(buffer), "%d/%d", state.currentLayer, state.totalLayers);
  setLabelText(finishLayers, buffer);
  setLabelText(finishType, prettyType);

  setLabelText(tempLegendNozzle, "Pink: Nozzle");
  setLabelText(tempLegendBed, "Blue: Bed");

  updateNoticePage(state);
  updateMenuTimeout();
  updatePager();
}

void setActionHandler(ActionHandler handler) { actionHandler = handler; }

void setBrightnessPercent(uint8_t percent) {
  currentBrightnessPercent = percent;
  updateMenuButtons();
}

void showCalibrationStep(uint8_t step, uint8_t total, int x, int y) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%u / %u", static_cast<unsigned>(step), static_cast<unsigned>(total));
  lv_label_set_text(calibrationTitle, "Touch Calibration");
  lv_label_set_text(calibrationHint, "Tap the pink cross, then release.");
  lv_label_set_text(calibrationStepLabel, buffer);
  lv_obj_add_flag(calibrationStartBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(calibrationCancelBtn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(calibrationCrossH, x - 13, y - 1);
  lv_obj_set_pos(calibrationCrossV, x - 1, y - 13);
  lv_obj_clear_flag(calibrationCrossH, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(calibrationCrossV, LV_OBJ_FLAG_HIDDEN);
  setCalibrationVisible(true);
}

void hideCalibration() {
  if (calibrationStartBtn != nullptr) lv_obj_add_flag(calibrationStartBtn, LV_OBJ_FLAG_HIDDEN);
  if (calibrationCancelBtn != nullptr) lv_obj_add_flag(calibrationCancelBtn, LV_OBJ_FLAG_HIDDEN);
  if (calibrationCrossH != nullptr) lv_obj_add_flag(calibrationCrossH, LV_OBJ_FLAG_HIDDEN);
  if (calibrationCrossV != nullptr) lv_obj_add_flag(calibrationCrossV, LV_OBJ_FLAG_HIDDEN);
  setCalibrationVisible(false);
}

}  // namespace ui

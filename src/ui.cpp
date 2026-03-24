#include "ui.h"

#include <ctype.h>
#include <stdio.h>

namespace ui {
namespace {

constexpr uint32_t kAccentColor = 0xFF6B8F;
constexpr uint32_t kLiveColor = 0xFF8FB1;
constexpr uint32_t kTrackColor = 0x341823;
constexpr uint32_t kOfflineDebounceMs = 4000;
constexpr lv_coord_t kPageTitleY = 10;
constexpr lv_coord_t kContentTopY = 42;
constexpr lv_coord_t kPageSidePadding = 12;

enum class PageId : uint8_t {
  Summary,
  Temps,
  Details,
  Idle,
  Notice,
};

enum class ViewMode : uint8_t {
  Printing,
  Paused,
  Finished,
  Idle,
  Offline,
};

struct PageSpec {
  PageId id;
  uint32_t durationMs;
};

constexpr PageSpec kPrintingPages[] = {
    {PageId::Summary, 7000}, {PageId::Temps, 4000}, {PageId::Details, 3000}};
constexpr PageSpec kIdlePages[] = {{PageId::Idle, 8000}, {PageId::Temps, 4000}, {PageId::Details, 3000}};
constexpr PageSpec kPausedPages[] = {{PageId::Summary, 7000}, {PageId::Temps, 4000}, {PageId::Details, 3000}};
constexpr PageSpec kFinishedPages[] = {{PageId::Summary, 7000}, {PageId::Details, 4000}, {PageId::Temps, 3000}};
constexpr PageSpec kOfflinePages[] = {{PageId::Notice, 8000}};

lv_obj_t *screenObj = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *pageIndicatorTrack = nullptr;
lv_obj_t *pageIndicatorFill = nullptr;

lv_obj_t *summaryPage = nullptr;
lv_obj_t *tempsPage = nullptr;
lv_obj_t *detailsPage = nullptr;
lv_obj_t *idlePage = nullptr;
lv_obj_t *noticePage = nullptr;

lv_obj_t *summaryStage = nullptr;
lv_obj_t *summaryJob = nullptr;
lv_obj_t *summaryProgress = nullptr;
lv_obj_t *summaryPercent = nullptr;
lv_obj_t *summaryTime = nullptr;
lv_obj_t *summaryLayers = nullptr;
lv_obj_t *summaryDoneAt = nullptr;

lv_obj_t *tempNozzle = nullptr;
lv_obj_t *tempBed = nullptr;
lv_obj_t *tempWifi = nullptr;
lv_obj_t *tempSpeed = nullptr;

lv_obj_t *detailType = nullptr;
lv_obj_t *detailUpdated = nullptr;
lv_obj_t *detailState = nullptr;
lv_obj_t *detailJob = nullptr;

lv_obj_t *idleStatus = nullptr;
lv_obj_t *idleJob = nullptr;
lv_obj_t *idleTemp = nullptr;
lv_obj_t *idleUpdated = nullptr;

lv_obj_t *noticeTitle = nullptr;
lv_obj_t *noticeSubtitle = nullptr;
lv_obj_t *noticeMetaA = nullptr;
lv_obj_t *noticeMetaB = nullptr;

lv_style_t cardStyle;
lv_style_t titleStyle;
lv_style_t valueStyle;
lv_style_t compactValueStyle;
lv_style_t pageStyle;
lv_style_t heroStyle;
lv_style_t subtleStyle;

const PageSpec *activePages = nullptr;
size_t activePageCount = 0;
size_t currentPageIndex = 0;
uint32_t pageStartedAtMs = 0;
bool pagerInitialized = false;
ViewMode currentMode = ViewMode::Idle;
uint32_t offlineCandidateSinceMs = 0;

const char *effectiveStage(const PrinterState &state) {
  if (state.stageText[0] != '\0' && strcmp(state.stageText, "UNKNOWN") != 0) {
    return state.stageText;
  }
  return state.printing ? "Printing" : "Idle";
}

bool isPausedState(const char *stage) {
  return strcmp(stage, "PAUSE") == 0 || strcmp(stage, "PAUSED") == 0;
}

bool isFinishedState(const char *stage) {
  return strcmp(stage, "FINISH") == 0 || strcmp(stage, "FINISHED") == 0 || strcmp(stage, "COMPLETED") == 0;
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
  if (size == 0) {
    return;
  }
  if (input == nullptr || input[0] == '\0') {
    snprintf(output, size, "-");
    return;
  }

  size_t out = 0;
  bool newWord = true;
  for (size_t i = 0; input[i] != '\0' && out + 1 < size; ++i) {
    char c = input[i];
    if (c == '_' || c == '-') {
      if (out > 0 && output[out - 1] != ' ' && out + 1 < size) {
        output[out++] = ' ';
      }
      newWord = true;
      continue;
    }

    const unsigned char uc = static_cast<unsigned char>(c);
    output[out++] = newWord ? static_cast<char>(toupper(uc)) : static_cast<char>(tolower(uc));
    newWord = false;
  }
  output[out] = '\0';
}

void formatAge(uint32_t lastUpdateMs, char *output, size_t size) {
  const uint32_t now = lv_tick_get();
  if (lastUpdateMs == 0) {
    snprintf(output, size, "No data");
    return;
  }

  const uint32_t ageMs = now > lastUpdateMs ? now - lastUpdateMs : 0;
  const uint32_t ageSec = ageMs / 1000U;
  if (ageSec < 2) {
    snprintf(output, size, "Just now");
    return;
  }
  if (ageSec < 60) {
    snprintf(output, size, "%lus ago", static_cast<unsigned long>(ageSec));
    return;
  }

  const uint32_t ageMin = ageSec / 60U;
  if (ageMin < 60) {
    snprintf(output, size, "%lum ago", static_cast<unsigned long>(ageMin));
    return;
  }

  const uint32_t ageHr = ageMin / 60U;
  snprintf(output, size, "%luh ago", static_cast<unsigned long>(ageHr));
}

void formatDoneAt(const PrinterState &state, char *output, size_t size) {
  if (state.remainingMinutes <= 0) {
    snprintf(output, size, "Done soon");
    return;
  }

  const uint32_t nowMin = lv_tick_get() / 60000U;
  const uint32_t doneMin = nowMin + static_cast<uint32_t>(state.remainingMinutes);
  const uint32_t hh = (doneMin / 60U) % 24U;
  const uint32_t mm = doneMin % 60U;
  snprintf(output, size, "%02lu:%02lu", static_cast<unsigned long>(hh), static_cast<unsigned long>(mm));
}

void setLabelText(lv_obj_t *obj, const char *text) {
  lv_label_set_text(obj, text);
}

lv_obj_t *createPage() {
  lv_obj_t *page = lv_obj_create(screenObj);
  lv_obj_remove_style_all(page);
  lv_obj_add_style(page, &pageStyle, 0);
  lv_obj_set_pos(page, 0, 18);
  lv_obj_set_size(page, 240, 298);
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
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 8);
  return label;
}

lv_obj_t *createCardValue(lv_obj_t *card, lv_coord_t y, lv_style_t *style, lv_coord_t width) {
  lv_obj_t *label = lv_label_create(card);
  lv_obj_add_style(label, style, 0);
  lv_obj_set_width(label, width);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, y);
  lv_label_set_text(label, "-");
  return label;
}

void showOnly(PageId id) {
  lv_obj_add_flag(summaryPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tempsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(detailsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(idlePage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(noticePage, LV_OBJ_FLAG_HIDDEN);

  switch (id) {
    case PageId::Summary: lv_obj_clear_flag(summaryPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Temps: lv_obj_clear_flag(tempsPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Details: lv_obj_clear_flag(detailsPage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Idle: lv_obj_clear_flag(idlePage, LV_OBJ_FLAG_HIDDEN); break;
    case PageId::Notice: lv_obj_clear_flag(noticePage, LV_OBJ_FLAG_HIDDEN); break;
  }
}

void setActiveSequence(ViewMode mode) {
  currentMode = mode;
  switch (mode) {
    case ViewMode::Printing:
      activePages = kPrintingPages;
      activePageCount = sizeof(kPrintingPages) / sizeof(kPrintingPages[0]);
      break;
    case ViewMode::Paused:
      activePages = kPausedPages;
      activePageCount = sizeof(kPausedPages) / sizeof(kPausedPages[0]);
      break;
    case ViewMode::Finished:
      activePages = kFinishedPages;
      activePageCount = sizeof(kFinishedPages) / sizeof(kFinishedPages[0]);
      break;
    case ViewMode::Idle:
      activePages = kIdlePages;
      activePageCount = sizeof(kIdlePages) / sizeof(kIdlePages[0]);
      break;
    case ViewMode::Offline:
      activePages = kOfflinePages;
      activePageCount = sizeof(kOfflinePages) / sizeof(kOfflinePages[0]);
      break;
  }
  currentPageIndex = 0;
  pageStartedAtMs = lv_tick_get();
  pagerInitialized = true;
  showOnly(activePages[0].id);
}

void updatePager() {
  if (!pagerInitialized || activePageCount == 0) {
    return;
  }

  const uint32_t now = lv_tick_get();
  const uint32_t duration = activePages[currentPageIndex].durationMs;
  const uint32_t elapsed = now - pageStartedAtMs;
  if (elapsed >= duration) {
    currentPageIndex = (currentPageIndex + 1) % activePageCount;
    pageStartedAtMs = now;
    showOnly(activePages[currentPageIndex].id);
  }

  const uint32_t progress = ((now - pageStartedAtMs) * 240U) / activePages[currentPageIndex].durationMs;
  lv_obj_set_width(pageIndicatorFill, progress > 240U ? 240 : progress);
}

ViewMode detectMode(const PrinterState &state) {
  const char *stage = effectiveStage(state);
  if (state.stale || !state.hasData) {
    const uint32_t now = lv_tick_get();
    if (offlineCandidateSinceMs == 0) {
      offlineCandidateSinceMs = now;
    }
    if ((now - offlineCandidateSinceMs) >= kOfflineDebounceMs) {
      return ViewMode::Offline;
    }
    return currentMode == ViewMode::Offline ? ViewMode::Offline : ViewMode::Idle;
  }

  offlineCandidateSinceMs = 0;

  if (isPausedState(stage)) {
    return ViewMode::Paused;
  }
  if (isFinishedState(stage)) {
    return ViewMode::Finished;
  }
  if (state.printing) {
    return ViewMode::Printing;
  }
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
  lv_style_set_pad_all(&cardStyle, 0);

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
}

void buildSummaryPage() {
  summaryPage = createPage();

  summaryStage = lv_label_create(summaryPage);
  lv_obj_set_style_text_font(summaryStage, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(summaryStage, lv_color_hex(0xF5F7FA), 0);
  lv_obj_set_width(summaryStage, 204);
  lv_label_set_long_mode(summaryStage, LV_LABEL_LONG_DOT);
  lv_obj_align(summaryStage, LV_ALIGN_TOP_LEFT, kPageSidePadding, kPageTitleY);
  lv_label_set_text(summaryStage, "Waiting");

  summaryJob = lv_label_create(summaryPage);
  lv_obj_add_style(summaryJob, &subtleStyle, 0);
  lv_obj_set_width(summaryJob, 216);
  lv_label_set_long_mode(summaryJob, LV_LABEL_LONG_DOT);
  lv_obj_align(summaryJob, LV_ALIGN_TOP_LEFT, kPageSidePadding, 36);
  lv_label_set_text(summaryJob, "-");

  summaryProgress = lv_bar_create(summaryPage);
  lv_obj_set_size(summaryProgress, 216, 18);
  lv_obj_set_style_bg_color(summaryProgress, lv_color_hex(0x202734), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(summaryProgress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(summaryProgress, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(summaryProgress, lv_color_hex(kAccentColor), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(summaryProgress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(summaryProgress, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_bar_set_range(summaryProgress, 0, 100);
  lv_bar_set_value(summaryProgress, 0, LV_ANIM_OFF);
  lv_obj_align(summaryProgress, LV_ALIGN_TOP_LEFT, kPageSidePadding, 70);

  summaryPercent = lv_label_create(summaryPage);
  lv_obj_add_style(summaryPercent, &heroStyle, 0);
  lv_obj_align(summaryPercent, LV_ALIGN_TOP_MID, 0, 94);
  lv_label_set_text(summaryPercent, "0%");

  lv_obj_t *timeCard = createCard(summaryPage, 12, 148, 102, 80);
  createCardTitle(timeCard, "Remaining");
  summaryTime = createCardValue(timeCard, 32, &valueStyle, 86);
  summaryDoneAt = createCardValue(timeCard, 56, &titleStyle, 86);

  lv_obj_t *layerCard = createCard(summaryPage, 126, 148, 102, 80);
  createCardTitle(layerCard, "Layers");
  summaryLayers = createCardValue(layerCard, 32, &valueStyle, 86);
}

void buildTempsPage() {
  tempsPage = createPage();

  lv_obj_t *title = lv_label_create(tempsPage);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(title, "Temperatures");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kPageSidePadding, kPageTitleY);

  lv_obj_t *nozzleCard = createCard(tempsPage, 12, kContentTopY, 216, 72);
  createCardTitle(nozzleCard, "Nozzle");
  tempNozzle = createCardValue(nozzleCard, 30, &valueStyle, 198);

  lv_obj_t *bedCard = createCard(tempsPage, 12, 124, 216, 72);
  createCardTitle(bedCard, "Bed");
  tempBed = createCardValue(bedCard, 30, &valueStyle, 198);

  lv_obj_t *wifiCard = createCard(tempsPage, 12, 206, 102, 60);
  createCardTitle(wifiCard, "WiFi");
  tempWifi = createCardValue(wifiCard, 28, &compactValueStyle, 86);

  lv_obj_t *speedCard = createCard(tempsPage, 126, 206, 102, 60);
  createCardTitle(speedCard, "Speed");
  tempSpeed = createCardValue(speedCard, 28, &compactValueStyle, 86);
}

void buildDetailsPage() {
  detailsPage = createPage();

  lv_obj_t *title = lv_label_create(detailsPage);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FA), 0);
  lv_label_set_text(title, "Details");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kPageSidePadding, kPageTitleY);

  lv_obj_t *stateCard = createCard(detailsPage, 12, kContentTopY, 216, 50);
  createCardTitle(stateCard, "State");
  detailState = createCardValue(stateCard, 24, &compactValueStyle, 198);

  lv_obj_t *jobCard = createCard(detailsPage, 12, 102, 216, 56);
  createCardTitle(jobCard, "Job");
  detailJob = createCardValue(jobCard, 24, &compactValueStyle, 198);

  lv_obj_t *typeCard = createCard(detailsPage, 12, 168, 216, 50);
  createCardTitle(typeCard, "Type");
  detailType = createCardValue(typeCard, 24, &compactValueStyle, 198);

  lv_obj_t *updatedCard = createCard(detailsPage, 12, 228, 216, 50);
  createCardTitle(updatedCard, "Updated");
  detailUpdated = createCardValue(updatedCard, 24, &compactValueStyle, 198);
}

void buildIdlePage() {
  idlePage = createPage();

  idleStatus = lv_label_create(idlePage);
  lv_obj_add_style(idleStatus, &heroStyle, 0);
  lv_obj_align(idleStatus, LV_ALIGN_TOP_LEFT, 12, 20);
  lv_label_set_text(idleStatus, "Idle");

  idleJob = lv_label_create(idlePage);
  lv_obj_set_style_text_font(idleJob, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(idleJob, lv_color_hex(0xC8D0DA), 0);
  lv_obj_set_width(idleJob, 216);
  lv_label_set_long_mode(idleJob, LV_LABEL_LONG_DOT);
  lv_obj_align(idleJob, LV_ALIGN_TOP_LEFT, 12, 64);
  lv_label_set_text(idleJob, "No active job");

  lv_obj_t *tempCard = createCard(idlePage, 12, 126, 216, 66);
  createCardTitle(tempCard, "Temperatures");
  idleTemp = createCardValue(tempCard, 28, &valueStyle, 198);

  lv_obj_t *updatedCard = createCard(idlePage, 12, 204, 216, 54);
  createCardTitle(updatedCard, "Updated");
  idleUpdated = createCardValue(updatedCard, 24, &compactValueStyle, 198);
}

void buildNoticePage() {
  noticePage = createPage();

  noticeTitle = lv_label_create(noticePage);
  lv_obj_add_style(noticeTitle, &heroStyle, 0);
  lv_obj_set_width(noticeTitle, 216);
  lv_label_set_long_mode(noticeTitle, LV_LABEL_LONG_DOT);
  lv_obj_align(noticeTitle, LV_ALIGN_TOP_LEFT, 12, 26);
  lv_label_set_text(noticeTitle, "Offline");

  noticeSubtitle = lv_label_create(noticePage);
  lv_obj_set_style_text_font(noticeSubtitle, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(noticeSubtitle, lv_color_hex(0xD8DEE7), 0);
  lv_obj_set_width(noticeSubtitle, 216);
  lv_label_set_long_mode(noticeSubtitle, LV_LABEL_LONG_WRAP);
  lv_obj_align(noticeSubtitle, LV_ALIGN_TOP_LEFT, 12, 74);
  lv_label_set_text(noticeSubtitle, "Waiting for printer telemetry");

  lv_obj_t *metaA = createCard(noticePage, 12, 164, 102, 66);
  createCardTitle(metaA, "Status");
  noticeMetaA = createCardValue(metaA, 28, &compactValueStyle, 86);

  lv_obj_t *metaB = createCard(noticePage, 126, 164, 102, 66);
  createCardTitle(metaB, "Updated");
  noticeMetaB = createCardValue(metaB, 28, &compactValueStyle, 86);
}

void updateNoticePage(const PrinterState &state) {
  char prettyState[48];
  char updatedText[48];
  char layersText[32];
  char timeText[32];

  formatDisplayText(effectiveStage(state), prettyState, sizeof(prettyState));
  formatAge(state.lastUpdateMs, updatedText, sizeof(updatedText));
  snprintf(layersText, sizeof(layersText), "%d/%d", state.currentLayer, state.totalLayers);
  formatDuration(state.remainingMinutes, timeText, sizeof(timeText));

  switch (currentMode) {
    case ViewMode::Offline:
      setLabelText(noticeTitle, "Printer Offline");
      setLabelText(noticeSubtitle, "No fresh LAN/MQTT data. Waiting for the printer to respond.");
      setLabelText(noticeMetaA, "Stale");
      setLabelText(noticeMetaB, updatedText);
      break;
    case ViewMode::Paused:
      setLabelText(noticeTitle, "Print Paused");
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, layersText);
      setLabelText(noticeMetaB, updatedText);
      break;
    case ViewMode::Finished:
      setLabelText(noticeTitle, "Print Finished");
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, layersText);
      setLabelText(noticeMetaB, "Done");
      break;
    case ViewMode::Printing:
    case ViewMode::Idle:
      setLabelText(noticeTitle, prettyState);
      setLabelText(noticeSubtitle, state.jobName);
      setLabelText(noticeMetaA, timeText);
      setLabelText(noticeMetaB, updatedText);
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
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x8FA3B8), 0);
  lv_label_set_text(statusLabel, "LIVE");
  lv_obj_align(statusLabel, LV_ALIGN_TOP_RIGHT, -12, 8);

  buildSummaryPage();
  buildTempsPage();
  buildDetailsPage();
  buildIdlePage();
  buildNoticePage();

  pageIndicatorTrack = lv_obj_create(screenObj);
  lv_obj_remove_style_all(pageIndicatorTrack);
  lv_obj_set_size(pageIndicatorTrack, 240, 2);
  lv_obj_set_pos(pageIndicatorTrack, 0, 318);
  lv_obj_set_style_bg_color(pageIndicatorTrack, lv_color_hex(kTrackColor), 0);
  lv_obj_set_style_bg_opa(pageIndicatorTrack, LV_OPA_COVER, 0);

  pageIndicatorFill = lv_obj_create(screenObj);
  lv_obj_remove_style_all(pageIndicatorFill);
  lv_obj_set_size(pageIndicatorFill, 0, 2);
  lv_obj_set_pos(pageIndicatorFill, 0, 318);
  lv_obj_set_style_bg_color(pageIndicatorFill, lv_color_hex(kAccentColor), 0);
  lv_obj_set_style_bg_opa(pageIndicatorFill, LV_OPA_COVER, 0);

  setActiveSequence(ViewMode::Idle);
  lv_scr_load(screenObj);
}

void applyState(const PrinterState &state) {
  char buffer[96];
  char prettyState[48];
  char prettyType[48];
  char updatedText[48];
  char doneAtText[32];
  char idleTempText[48];

  const ViewMode mode = detectMode(state);
  if (mode != currentMode || !pagerInitialized) {
    setActiveSequence(mode);
  }

  lv_obj_set_style_text_color(statusLabel, state.stale ? lv_color_hex(0xD16B72) : lv_color_hex(kLiveColor), 0);
  lv_label_set_text(statusLabel, state.stale ? "STALE" : "LIVE");

  formatDisplayText(effectiveStage(state), prettyState, sizeof(prettyState));
  formatDisplayText(state.printType, prettyType, sizeof(prettyType));
  formatAge(state.lastUpdateMs, updatedText, sizeof(updatedText));
  formatDoneAt(state, doneAtText, sizeof(doneAtText));

  setLabelText(summaryStage, prettyState);
  setLabelText(summaryJob, state.jobName);
  lv_bar_set_value(summaryProgress, state.percent, LV_ANIM_OFF);
  snprintf(buffer, sizeof(buffer), "%d%%", state.percent);
  setLabelText(summaryPercent, buffer);
  formatDuration(state.remainingMinutes, buffer, sizeof(buffer));
  setLabelText(summaryTime, buffer);
  snprintf(buffer, sizeof(buffer), "Done %s", doneAtText);
  setLabelText(summaryDoneAt, buffer);
  snprintf(buffer, sizeof(buffer), "%d/%d", state.currentLayer, state.totalLayers);
  setLabelText(summaryLayers, buffer);

  snprintf(buffer, sizeof(buffer), "%.0f/%.0fC", state.nozzleTemp, state.nozzleTargetTemp);
  setLabelText(tempNozzle, buffer);
  snprintf(buffer, sizeof(buffer), "%.0f/%.0fC", state.bedTemp, state.bedTargetTemp);
  setLabelText(tempBed, buffer);
  setLabelText(tempWifi, state.wifiSignal);
  snprintf(buffer, sizeof(buffer), "L%d", state.speedLevel);
  setLabelText(tempSpeed, buffer);

  setLabelText(detailState, prettyState);
  setLabelText(detailJob, state.jobName);
  setLabelText(detailType, prettyType);
  setLabelText(detailUpdated, updatedText);

  setLabelText(idleStatus, mode == ViewMode::Idle ? "Ready" : prettyState);
  setLabelText(idleJob, state.jobName);
  snprintf(idleTempText, sizeof(idleTempText), "%.0fC nozzle  %.0fC bed", state.nozzleTemp, state.bedTemp);
  setLabelText(idleTemp, idleTempText);
  setLabelText(idleUpdated, updatedText);

  updateNoticePage(state);
  updatePager();
}

}  // namespace ui

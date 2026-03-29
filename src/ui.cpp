#include "ui.h"
#include "ui_icons.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ui {
namespace {

constexpr lv_coord_t kScreenW = 320;
constexpr lv_coord_t kScreenH = 240;
constexpr lv_coord_t kRailW = 48;
constexpr lv_coord_t kRailShellW = 60;
constexpr lv_coord_t kContentX = kRailW;
constexpr lv_coord_t kContentW = kScreenW - kContentX;
constexpr lv_coord_t kCardRadius = 4;
constexpr uint8_t kTempHistorySize = 24;
constexpr uint32_t kTempSampleIntervalMs = 5000;

enum class Section : uint8_t { Home = 0, Temps = 1, Print = 2, System = 3, Menu = 4 };
enum class IconId : uint8_t { Home, Temps, Print, System, Menu, Nozzle, Bed, Wifi, Speed, Layers };
enum class SystemPane : uint8_t { Info = 0, Settings = 1 };

ActionHandler actionHandler = nullptr;
PrinterState currentState = makeDefaultPrinterState();
Section activeSection = Section::Home;
uint8_t brightnessPercent = 75;
SystemPane activeSystemPane = SystemPane::Info;
char systemIpText[32] = "-";
char systemSerialText[32] = "-";
char systemHeapText[24] = "-";
char systemFirmwareText[24] = "-";
char systemMqttText[24] = "-";
char systemUpdatedText[24] = "-";

lv_style_t screenStyle;
lv_style_t railStyle;
lv_style_t pageStyle;
lv_style_t cardStyle;
lv_style_t accentCardStyle;
lv_style_t navButtonStyle;
lv_style_t navButtonActiveStyle;
lv_style_t navIconImageStyle;
lv_style_t navIconImageActiveStyle;
lv_style_t subNavButtonActiveStyle;
lv_style_t tinyStyle;
lv_style_t labelStyle;
lv_style_t valueStyle;
lv_style_t bigValueStyle;
lv_style_t navIconStyle;
lv_style_t navIconActiveStyle;
lv_style_t overlayStyle;
lv_style_t modalStyle;
lv_style_t buttonStyle;
lv_style_t buttonAccentStyle;
lv_style_t liveStyle;
lv_style_t staleStyle;
lv_style_t homeCardTopStyle;
lv_style_t homeCardBottomStyle;
lv_style_t homeControlTextStyle;
lv_style_t homeTitleStyle;
lv_style_t homeBodyStyle;

lv_obj_t *root = nullptr;
lv_obj_t *rail = nullptr;
lv_obj_t *liveBadge = nullptr;
lv_obj_t *navButtons[5] = {};
lv_obj_t *navIcons[5] = {};

lv_obj_t *homePage = nullptr;
lv_obj_t *tempsPage = nullptr;
lv_obj_t *printPage = nullptr;
lv_obj_t *systemPage = nullptr;
lv_obj_t *menuPage = nullptr;
lv_obj_t *systemInfoPane = nullptr;
lv_obj_t *systemSettingsPane = nullptr;
lv_obj_t *systemSubButtons[2] = {};
lv_obj_t *systemSubIcons[2] = {};

lv_obj_t *calibrationConfirmOverlay = nullptr;
lv_obj_t *calibrationOverlay = nullptr;
lv_obj_t *calibrationCross = nullptr;
lv_obj_t *calibrationStepLabel = nullptr;

lv_obj_t *homeJobLabel = nullptr;
lv_obj_t *homePercentLabel = nullptr;
lv_obj_t *homeProgressBar = nullptr;
lv_obj_t *homeTitleLabel = nullptr;
lv_obj_t *homeInitLabel = nullptr;
lv_obj_t *homeProgressGroup = nullptr;
lv_obj_t *homeStatsGroup = nullptr;
lv_obj_t *homeControlsGroup = nullptr;
lv_obj_t *homeLayersValue = nullptr;
lv_obj_t *homeBedValue = nullptr;
lv_obj_t *homeTimeValue = nullptr;
lv_obj_t *homeNozzleValue = nullptr;
lv_obj_t *homeControlBars[4] = {};

lv_obj_t *tempsNozzleValue = nullptr;
lv_obj_t *tempsBedValue = nullptr;
lv_obj_t *tempsChart = nullptr;
lv_chart_series_t *tempsNozzleSeries = nullptr;
lv_chart_series_t *tempsBedSeries = nullptr;

lv_obj_t *printJobLabel = nullptr;
lv_obj_t *printProgressValue = nullptr;
lv_obj_t *printRemainValue = nullptr;
lv_obj_t *printLayersValue = nullptr;
lv_obj_t *printSpeedValue = nullptr;
lv_obj_t *printStateValue = nullptr;

lv_obj_t *systemIpValue = nullptr;
lv_obj_t *systemSerialValue = nullptr;
lv_obj_t *systemHeapValue = nullptr;
lv_obj_t *systemFirmwareValue = nullptr;
lv_obj_t *systemMqttValue = nullptr;
lv_obj_t *systemUpdatedValue = nullptr;
lv_obj_t *systemTouchValue = nullptr;
lv_obj_t *systemBrightnessButtons[3] = {};

lv_obj_t *menuBrightnessValue = nullptr;
lv_obj_t *menuWifiValue = nullptr;
lv_obj_t *menuMqttValue = nullptr;
lv_obj_t *menuPauseButton = nullptr;
lv_obj_t *menuStopButton = nullptr;
lv_obj_t *menuPauseLabel = nullptr;
lv_obj_t *menuSpeedButtons[4] = {};

int16_t nozzleHistory[kTempHistorySize] = {};
int16_t bedHistory[kTempHistorySize] = {};
bool historySeeded = false;
uint32_t lastTempSampleMs = 0;

const lv_color_t kBg = lv_color_hex(0x0F060D);
const lv_color_t kRail = lv_color_hex(0x201720);
const lv_color_t kPanel = lv_color_hex(0x201720);
const lv_color_t kPanelAlt = lv_color_hex(0x201720);
const lv_color_t kStroke = lv_color_hex(0x201720);
const lv_color_t kText = lv_color_hex(0xFFFFFF);
const lv_color_t kMuted = lv_color_hex(0xFFFFFF);
const lv_color_t kAccent = lv_color_hex(0xEA12CD);
const lv_color_t kAccentSoft = lv_color_hex(0xEA12CD);
const lv_color_t kLive = lv_color_hex(0xEA12CD);
const lv_color_t kStale = lv_color_hex(0x86909E);
const lv_color_t kLineBed = lv_color_hex(0xFFFFFF);

const char *iconText(IconId id) {
  switch (id) {
    case IconId::Home:
      return LV_SYMBOL_HOME;
    case IconId::Temps:
      return LV_SYMBOL_SETTINGS;
    case IconId::Print:
      return LV_SYMBOL_EDIT;
    case IconId::System:
      return LV_SYMBOL_WIFI;
    case IconId::Menu:
      return LV_SYMBOL_LIST;
    case IconId::Nozzle:
      return LV_SYMBOL_CHARGE;
    case IconId::Bed:
      return LV_SYMBOL_DRIVE;
    case IconId::Wifi:
      return LV_SYMBOL_WIFI;
    case IconId::Speed:
      return LV_SYMBOL_LOOP;
    case IconId::Layers:
      return LV_SYMBOL_COPY;
  }
  return "";
}

const lv_img_dsc_t *railIconAsset(Section section) {
  switch (section) {
    case Section::Home:
      return &ui_icon_home;
    case Section::Temps:
      return &ui_icon_temps;
    case Section::Print:
      return &ui_icon_print;
    case Section::System:
      return &ui_icon_system;
    case Section::Menu:
      return &ui_icon_menu;
  }
  return &ui_icon_home;
}

const lv_img_dsc_t *homeStatAsset(IconId id) {
  switch (id) {
    case IconId::Layers:
      return &ui_icon_stack;
    case IconId::Print:
      return &ui_icon_timer;
    case IconId::Bed:
    case IconId::Nozzle:
      return &ui_icon_thermo_small;
    default:
      return &ui_icon_stack;
  }
}

const lv_img_dsc_t *printStatAsset(IconId id) {
  switch (id) {
    case IconId::Print:
      return &ui_icon_timer;
    case IconId::Layers:
      return &ui_icon_stack;
    case IconId::Speed:
      return &ui_icon_dashboard;
    case IconId::Menu:
      return &ui_icon_file_list;
    case IconId::System:
      return &ui_icon_restart;
    default:
      return &ui_icon_stack;
  }
}

lv_coord_t homeStatIconX(IconId id) {
  switch (id) {
    case IconId::Layers:
      return 88;
    case IconId::Print:
      return 89;
    case IconId::Bed:
    case IconId::Nozzle:
      return 89;
    default:
      return 89;
  }
}

lv_coord_t homeStatIconY(IconId id) {
  switch (id) {
    case IconId::Layers:
      return 2;
    case IconId::Print:
      return 1;
    case IconId::Bed:
    case IconId::Nozzle:
      return 1;
    default:
      return 1;
  }
}

const char *speedText(int speedLevel) {
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

bool isHeating(const PrinterState &state) {
  if (!state.hasData) return false;
  if (strcmp(state.stageText, "PREPARE") == 0) return true;
  if (state.nozzleTargetTemp > 0.0f && state.nozzleTemp + 5.0f < state.nozzleTargetTemp) return true;
  if (state.bedTargetTemp > 0.0f && state.bedTemp + 3.0f < state.bedTargetTemp) return true;
  return false;
}

const char *statusText(const PrinterState &state) {
  if (!state.hasData || state.stale) return "Offline";
  if (strcmp(state.stageText, "PAUSE") == 0) return "Paused";
  if (strcmp(state.stageText, "FINISH") == 0) return "Finished";
  if (isHeating(state)) return "Heating";
  if (state.printing) return "Printing";
  return "Ready";
}

void formatDuration(int minutes, char *buffer, size_t size) {
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

void formatTemp(float current, float target, char *buffer, size_t size) {
  snprintf(buffer, size, "%.0f / %.0f C", current, target);
}

void formatLayers(const PrinterState &state, char *buffer, size_t size) {
  if (state.totalLayers > 0) {
    snprintf(buffer, size, "%d / %d", state.currentLayer, state.totalLayers);
  } else {
    snprintf(buffer, size, "%d", state.currentLayer);
  }
}

const char *mqttStatus(const PrinterState &state) {
  if (!state.hasData) return "No data";
  if (state.stale) return "Offline";
  return "Connected";
}

bool canControlPrint(const PrinterState &state) {
  return state.hasData && !state.stale && state.printing;
}

bool isPaused(const PrinterState &state) { return strcmp(state.stageText, "PAUSE") == 0; }

void seedHistory() {
  for (uint8_t i = 0; i < kTempHistorySize; ++i) {
    nozzleHistory[i] = static_cast<int16_t>(currentState.nozzleTemp);
    bedHistory[i] = static_cast<int16_t>(currentState.bedTemp);
  }
  historySeeded = true;
  lastTempSampleMs = lv_tick_get();
}

void pushTempSample() {
  if (!historySeeded) {
    seedHistory();
    return;
  }
  const uint32_t now = lv_tick_get();
  if ((now - lastTempSampleMs) < kTempSampleIntervalMs) return;
  lastTempSampleMs = now;
  memmove(&nozzleHistory[0], &nozzleHistory[1], sizeof(nozzleHistory[0]) * (kTempHistorySize - 1));
  memmove(&bedHistory[0], &bedHistory[1], sizeof(bedHistory[0]) * (kTempHistorySize - 1));
  nozzleHistory[kTempHistorySize - 1] = static_cast<int16_t>(lroundf(currentState.nozzleTemp));
  bedHistory[kTempHistorySize - 1] = static_cast<int16_t>(lroundf(currentState.bedTemp));
}

void initStyles() {
  lv_style_init(&screenStyle);
  lv_style_set_bg_color(&screenStyle, kBg);
  lv_style_set_bg_opa(&screenStyle, LV_OPA_COVER);
  lv_style_set_border_width(&screenStyle, 0);
  lv_style_set_radius(&screenStyle, 0);
  lv_style_set_pad_all(&screenStyle, 0);

  lv_style_init(&railStyle);
  lv_style_set_bg_color(&railStyle, kRail);
  lv_style_set_bg_opa(&railStyle, LV_OPA_COVER);
  lv_style_set_border_width(&railStyle, 0);
  lv_style_set_radius(&railStyle, 12);
  lv_style_set_pad_all(&railStyle, 0);

  lv_style_init(&pageStyle);
  lv_style_set_bg_opa(&pageStyle, LV_OPA_TRANSP);
  lv_style_set_border_width(&pageStyle, 0);
  lv_style_set_radius(&pageStyle, 0);
  lv_style_set_pad_all(&pageStyle, 0);

  lv_style_init(&cardStyle);
  lv_style_set_bg_color(&cardStyle, kPanel);
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&cardStyle, 0);
  lv_style_set_radius(&cardStyle, kCardRadius);
  lv_style_set_pad_all(&cardStyle, 8);

  lv_style_init(&accentCardStyle);
  lv_style_set_bg_color(&accentCardStyle, kPanelAlt);
  lv_style_set_bg_opa(&accentCardStyle, LV_OPA_COVER);
  lv_style_set_border_color(&accentCardStyle, kAccent);
  lv_style_set_border_width(&accentCardStyle, 1);
  lv_style_set_radius(&accentCardStyle, kCardRadius);
  lv_style_set_pad_all(&accentCardStyle, 8);

  lv_style_init(&navButtonStyle);
  lv_style_set_bg_opa(&navButtonStyle, LV_OPA_TRANSP);
  lv_style_set_border_width(&navButtonStyle, 0);
  lv_style_set_radius(&navButtonStyle, 0);

  lv_style_init(&navButtonActiveStyle);
  lv_style_set_bg_color(&navButtonActiveStyle, kAccent);
  lv_style_set_bg_opa(&navButtonActiveStyle, 13);
  lv_style_set_border_side(&navButtonActiveStyle, LV_BORDER_SIDE_LEFT);
  lv_style_set_border_color(&navButtonActiveStyle, kAccent);
  lv_style_set_border_width(&navButtonActiveStyle, 3);
  lv_style_set_radius(&navButtonActiveStyle, 0);

  lv_style_init(&subNavButtonActiveStyle);
  lv_style_set_bg_color(&subNavButtonActiveStyle, kAccent);
  lv_style_set_bg_opa(&subNavButtonActiveStyle, 13);
  lv_style_set_border_side(&subNavButtonActiveStyle, LV_BORDER_SIDE_RIGHT);
  lv_style_set_border_color(&subNavButtonActiveStyle, kAccent);
  lv_style_set_border_width(&subNavButtonActiveStyle, 3);
  lv_style_set_radius(&subNavButtonActiveStyle, 0);

  lv_style_init(&navIconImageStyle);
  lv_style_set_img_recolor(&navIconImageStyle, kText);
  lv_style_set_img_recolor_opa(&navIconImageStyle, LV_OPA_COVER);

  lv_style_init(&navIconImageActiveStyle);
  lv_style_set_img_recolor(&navIconImageActiveStyle, kAccent);
  lv_style_set_img_recolor_opa(&navIconImageActiveStyle, LV_OPA_COVER);

  lv_style_init(&tinyStyle);
  lv_style_set_text_color(&tinyStyle, kText);
  lv_style_set_text_font(&tinyStyle, &lv_font_montserrat_12);

  lv_style_init(&labelStyle);
  lv_style_set_text_color(&labelStyle, kText);
  lv_style_set_text_font(&labelStyle, &lv_font_montserrat_14);

  lv_style_init(&valueStyle);
  lv_style_set_text_color(&valueStyle, kText);
  lv_style_set_text_font(&valueStyle, &lv_font_montserrat_18);

  lv_style_init(&bigValueStyle);
  lv_style_set_text_color(&bigValueStyle, kText);
  lv_style_set_text_font(&bigValueStyle, &lv_font_montserrat_24);

  lv_style_init(&homeTitleStyle);
  lv_style_set_text_color(&homeTitleStyle, kText);
  lv_style_set_text_font(&homeTitleStyle, &lv_font_montserrat_16);

  lv_style_init(&homeBodyStyle);
  lv_style_set_text_color(&homeBodyStyle, kText);
  lv_style_set_text_font(&homeBodyStyle, &lv_font_montserrat_12);

  lv_style_init(&homeControlTextStyle);
  lv_style_set_text_color(&homeControlTextStyle, kText);
  lv_style_set_text_font(&homeControlTextStyle, &lv_font_montserrat_12);

  lv_style_init(&navIconStyle);
  lv_style_set_text_color(&navIconStyle, kMuted);
  lv_style_set_text_font(&navIconStyle, &lv_font_montserrat_18);

  lv_style_init(&navIconActiveStyle);
  lv_style_set_text_color(&navIconActiveStyle, kAccent);
  lv_style_set_text_font(&navIconActiveStyle, &lv_font_montserrat_18);

  lv_style_init(&overlayStyle);
  lv_style_set_bg_color(&overlayStyle, lv_color_hex(0x000000));
  lv_style_set_bg_opa(&overlayStyle, LV_OPA_60);
  lv_style_set_border_width(&overlayStyle, 0);

  lv_style_init(&modalStyle);
  lv_style_set_bg_color(&modalStyle, lv_color_hex(0x141921));
  lv_style_set_bg_opa(&modalStyle, LV_OPA_COVER);
  lv_style_set_border_color(&modalStyle, kStroke);
  lv_style_set_border_width(&modalStyle, 1);
  lv_style_set_radius(&modalStyle, 6);
  lv_style_set_pad_all(&modalStyle, 12);

  lv_style_init(&buttonStyle);
  lv_style_set_bg_color(&buttonStyle, lv_color_hex(0x232935));
  lv_style_set_bg_opa(&buttonStyle, LV_OPA_COVER);
  lv_style_set_border_color(&buttonStyle, kStroke);
  lv_style_set_border_width(&buttonStyle, 1);
  lv_style_set_radius(&buttonStyle, 4);

  lv_style_init(&buttonAccentStyle);
  lv_style_set_bg_color(&buttonAccentStyle, kAccentSoft);
  lv_style_set_bg_opa(&buttonAccentStyle, LV_OPA_COVER);
  lv_style_set_border_color(&buttonAccentStyle, kAccent);
  lv_style_set_border_width(&buttonAccentStyle, 1);
  lv_style_set_radius(&buttonAccentStyle, 4);

  lv_style_init(&liveStyle);
  lv_style_set_text_color(&liveStyle, kLive);
  lv_style_set_text_font(&liveStyle, &lv_font_montserrat_12);

  lv_style_init(&staleStyle);
  lv_style_set_text_color(&staleStyle, kStale);
  lv_style_set_text_font(&staleStyle, &lv_font_montserrat_12);

  lv_style_init(&homeCardTopStyle);
  lv_style_set_bg_color(&homeCardTopStyle, kPanel);
  lv_style_set_bg_opa(&homeCardTopStyle, LV_OPA_COVER);
  lv_style_set_border_width(&homeCardTopStyle, 0);
  lv_style_set_radius(&homeCardTopStyle, 12);
  lv_style_set_pad_left(&homeCardTopStyle, 10);
  lv_style_set_pad_right(&homeCardTopStyle, 10);
  lv_style_set_pad_top(&homeCardTopStyle, 6);
  lv_style_set_pad_bottom(&homeCardTopStyle, 6);

  lv_style_init(&homeCardBottomStyle);
  lv_style_set_bg_color(&homeCardBottomStyle, kPanel);
  lv_style_set_bg_opa(&homeCardBottomStyle, LV_OPA_COVER);
  lv_style_set_border_width(&homeCardBottomStyle, 0);
  lv_style_set_radius(&homeCardBottomStyle, 8);
  lv_style_set_pad_left(&homeCardBottomStyle, 10);
  lv_style_set_pad_right(&homeCardBottomStyle, 10);
  lv_style_set_pad_top(&homeCardBottomStyle, 6);
  lv_style_set_pad_bottom(&homeCardBottomStyle, 6);
}

lv_obj_t *createText(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *text,
                     lv_style_t *style) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_add_style(label, style, 0);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
  return label;
}

lv_obj_t *createImage(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const lv_img_dsc_t *asset,
                      lv_style_t *style = nullptr) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, asset);
  lv_obj_set_pos(img, x, y);
  if (style) lv_obj_add_style(img, style, 0);
  return img;
}

lv_obj_t *createCardShell(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          bool accent = false) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, accent ? &accentCardStyle : &cardStyle, 0);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  return card;
}

lv_obj_t *createIconValueCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                              IconId icon, lv_obj_t **valueObj, const char *value, bool accent = false) {
  lv_obj_t *card = createCardShell(parent, x, y, w, h, accent);
  lv_obj_t *iconLabel = createText(card, 0, 0, iconText(icon), &tinyStyle);
  lv_obj_set_style_text_color(iconLabel, kMuted, 0);
  lv_obj_t *valueLabel = createText(card, 0, 18, value, &valueStyle);
  lv_obj_set_width(valueLabel, w - 16);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
  *valueObj = valueLabel;
  return card;
}

lv_obj_t *createActionButton(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                             const char *text, lv_event_cb_t cb, bool accent = false) {
  lv_obj_t *button = lv_obj_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_add_style(button, accent ? &buttonAccentStyle : &buttonStyle, 0);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *label = lv_label_create(button);
  lv_obj_add_style(label, &labelStyle, 0);
  lv_obj_set_style_text_color(label, kText, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return button;
}

lv_obj_t *createSystemInfoCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                               const char *title, const lv_img_dsc_t *iconAsset, lv_obj_t **valueObj,
                               const char *value) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &homeCardTopStyle, 0);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  createText(card, 0, 0, title, &homeBodyStyle);
  lv_obj_t *iconObj = createImage(card, w - 44, 0, iconAsset);
  lv_img_set_zoom(iconObj, 149);

  lv_obj_t *valueLabel = createText(card, 0, 20, value, &homeBodyStyle);
  lv_obj_set_width(valueLabel, w - 20);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
  *valueObj = valueLabel;
  return card;
}

lv_obj_t *createTinyStatCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                             const char *title, IconId icon, lv_obj_t **valueObj, const char *value) {
  lv_obj_t *card = createCardShell(parent, x, y, w, h, false);
  lv_obj_t *titleLabel = createText(card, 0, 0, title, &tinyStyle);
  lv_obj_set_style_text_color(titleLabel, kText, 0);
  lv_obj_t *iconLabel = createText(card, w - 30, 0, iconText(icon), &tinyStyle);
  lv_obj_set_style_text_color(iconLabel, kText, 0);
  lv_obj_t *valueLabel = createText(card, 0, 19, value, &tinyStyle);
  lv_obj_set_width(valueLabel, w - 16);
  lv_obj_set_style_text_color(valueLabel, kText, 0);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
  *valueObj = valueLabel;
  return card;
}

lv_obj_t *createHomeStatCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                             lv_style_t *style, const char *title, IconId icon, lv_obj_t **valueObj,
                             const char *value) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, style, 0);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *titleLabel = createText(card, 0, 0, title, &homeBodyStyle);
  lv_obj_t *iconObj =
      createImage(card, homeStatIconX(icon), homeStatIconY(icon), homeStatAsset(icon), &navIconImageStyle);
  lv_img_set_zoom(iconObj, 149);
  lv_obj_t *valueLabel = createText(card, 0, 19, value, &homeBodyStyle);
  lv_obj_set_width(valueLabel, w - 20);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
  *valueObj = valueLabel;
  (void)titleLabel;
  return card;
}

lv_obj_t *createPrintStatCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char *title,
                              IconId icon, lv_obj_t **valueObj, const char *value) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &homeCardTopStyle, 0);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, 46);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  createText(card, 0, 0, title, &homeBodyStyle);
  lv_obj_t *iconObj = createImage(card, w - 44, 0, printStatAsset(icon));
  lv_img_set_zoom(iconObj, 149);

  lv_obj_t *valueLabel = createText(card, 0, 20, value, &homeBodyStyle);
  lv_obj_set_width(valueLabel, w - 20);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_DOT);
  *valueObj = valueLabel;
  return card;
}

void setLiveBadge(bool stale) {
  if (!liveBadge) return;
  lv_obj_remove_style_all(liveBadge);
  lv_obj_add_style(liveBadge, stale ? &staleStyle : &liveStyle, 0);
  lv_label_set_text(liveBadge, stale ? "OFFLINE" : "LIVE");
}

void showPage(Section section) {
  activeSection = section;
  lv_obj_add_flag(homePage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(tempsPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(printPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(systemPage, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(menuPage, LV_OBJ_FLAG_HIDDEN);

  switch (section) {
    case Section::Home:
      lv_obj_clear_flag(homePage, LV_OBJ_FLAG_HIDDEN);
      break;
    case Section::Temps:
      lv_obj_clear_flag(tempsPage, LV_OBJ_FLAG_HIDDEN);
      break;
    case Section::Print:
      lv_obj_clear_flag(printPage, LV_OBJ_FLAG_HIDDEN);
      break;
    case Section::System:
      lv_obj_clear_flag(systemPage, LV_OBJ_FLAG_HIDDEN);
      break;
    case Section::Menu:
      lv_obj_clear_flag(menuPage, LV_OBJ_FLAG_HIDDEN);
      break;
  }

  for (uint8_t i = 0; i < 5; ++i) {
    lv_obj_remove_style(navButtons[i], &navButtonActiveStyle, 0);
    lv_obj_remove_style(navIcons[i], &navIconImageActiveStyle, 0);
    lv_obj_add_style(navIcons[i], &navIconImageStyle, 0);
    lv_obj_set_style_opa(navButtons[i], LV_OPA_COVER, 0);
  }
  const uint8_t idx = static_cast<uint8_t>(section);
  lv_obj_add_style(navButtons[idx], &navButtonActiveStyle, 0);
  lv_obj_remove_style(navIcons[idx], &navIconImageStyle, 0);
  lv_obj_add_style(navIcons[idx], &navIconImageActiveStyle, 0);
}

void closeCalibrationConfirm() { lv_obj_add_flag(calibrationConfirmOverlay, LV_OBJ_FLAG_HIDDEN); }

void showSystemPane(SystemPane pane) {
  activeSystemPane = pane;
  if (systemInfoPane) {
    if (pane == SystemPane::Info) {
      lv_obj_clear_flag(systemInfoPane, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(systemSettingsPane, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(systemInfoPane, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(systemSettingsPane, LV_OBJ_FLAG_HIDDEN);
    }
  }

  for (uint8_t i = 0; i < 2; ++i) {
    lv_obj_remove_style(systemSubButtons[i], &subNavButtonActiveStyle, 0);
    lv_obj_remove_style(systemSubIcons[i], &navIconImageActiveStyle, 0);
    lv_obj_add_style(systemSubIcons[i], &navIconImageStyle, 0);
  }
  const uint8_t idx = static_cast<uint8_t>(pane);
  lv_obj_add_style(systemSubButtons[idx], &subNavButtonActiveStyle, 0);
  lv_obj_remove_style(systemSubIcons[idx], &navIconImageStyle, 0);
  lv_obj_add_style(systemSubIcons[idx], &navIconImageActiveStyle, 0);
}

void openCalibrationConfirm() {
  lv_obj_clear_flag(calibrationConfirmOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(calibrationConfirmOverlay);
}

void navEvent(lv_event_t *event) {
  const intptr_t idx = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  closeCalibrationConfirm();
  showPage(static_cast<Section>(idx));
}

void systemInfoPaneEvent(lv_event_t *) { showSystemPane(SystemPane::Info); }

void systemSettingsPaneEvent(lv_event_t *) { showSystemPane(SystemPane::Settings); }

void brightnessLowEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetBrightnessLow);
}

void brightnessMidEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetBrightnessMid);
}

void brightnessMaxEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetBrightnessMax);
}

void calibrateTouchEvent(lv_event_t *) { openCalibrationConfirm(); }

void calibrationCancelEvent(lv_event_t *) { closeCalibrationConfirm(); }

void calibrationStartEvent(lv_event_t *) {
  closeCalibrationConfirm();
  if (actionHandler) actionHandler(Action::StartTouchCalibration);
}

void menuPauseEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::PauseOrResumePrint);
}

void menuStopEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::StopPrint);
}

void menuSpeedSilentEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetSpeedSilent);
}

void menuSpeedStandardEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetSpeedStandard);
}

void menuSpeedSportEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetSpeedSport);
}

void menuSpeedLudicrousEvent(lv_event_t *) {
  if (actionHandler) actionHandler(Action::SetSpeedLudicrous);
}

void initRail() {
  rail = lv_obj_create(root);
  lv_obj_remove_style_all(rail);
  lv_obj_add_style(rail, &railStyle, 0);
  lv_obj_set_pos(rail, -12, 0);
  lv_obj_set_size(rail, kRailShellW, kScreenH);
  lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < 5; ++i) {
    navButtons[i] = lv_obj_create(rail);
    lv_obj_remove_style_all(navButtons[i]);
    lv_obj_add_style(navButtons[i], &navButtonStyle, 0);
    lv_obj_set_pos(navButtons[i], 12, i * 48);
    lv_obj_set_size(navButtons[i], kRailW, 48);
    lv_obj_clear_flag(navButtons[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(navButtons[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(navButtons[i], navEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    navIcons[i] = lv_img_create(navButtons[i]);
    lv_obj_add_style(navIcons[i], &navIconImageStyle, 0);
    lv_img_set_src(navIcons[i], railIconAsset(static_cast<Section>(i)));
    lv_obj_center(navIcons[i]);
  }

  liveBadge = nullptr;
}

void initHomePage() {
  homePage = lv_obj_create(root);
  lv_obj_remove_style_all(homePage);
  lv_obj_add_style(homePage, &pageStyle, 0);
  lv_obj_set_pos(homePage, kContentX, 0);
  lv_obj_set_size(homePage, kContentW, kScreenH);
  lv_obj_clear_flag(homePage, LV_OBJ_FLAG_SCROLLABLE);

  homeTitleLabel = createText(homePage, 10, 10, "Home", &homeTitleStyle);

  homeInitLabel = createText(homePage, 52, 112, "BlPad is initializing...", &valueStyle);
  lv_obj_add_flag(homeInitLabel, LV_OBJ_FLAG_HIDDEN);

  homeProgressGroup = lv_obj_create(homePage);
  lv_obj_remove_style_all(homeProgressGroup);
  lv_obj_add_style(homeProgressGroup, &pageStyle, 0);
  lv_obj_set_pos(homeProgressGroup, 10, 40);
  lv_obj_set_size(homeProgressGroup, 252, 38);
  lv_obj_clear_flag(homeProgressGroup, LV_OBJ_FLAG_SCROLLABLE);

  homePercentLabel = createText(homeProgressGroup, 0, 0, "0%", &labelStyle);
  lv_obj_set_style_text_color(homePercentLabel, kText, 0);

  homeProgressBar = lv_bar_create(homeProgressGroup);
  lv_obj_set_pos(homeProgressBar, 39, 1);
  lv_obj_set_size(homeProgressBar, 213, 16);
  lv_bar_set_range(homeProgressBar, 0, 100);
  lv_obj_set_style_bg_color(homeProgressBar, kAccent, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(homeProgressBar, 38, LV_PART_MAIN);
  lv_obj_set_style_bg_color(homeProgressBar, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(homeProgressBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(homeProgressBar, 999, LV_PART_MAIN);
  lv_obj_set_style_radius(homeProgressBar, 999, LV_PART_INDICATOR);
  lv_obj_set_style_border_width(homeProgressBar, 0, 0);

  homeJobLabel = createText(homeProgressGroup, 129, 21, "-", &labelStyle);
  lv_obj_set_width(homeJobLabel, 123);
  lv_label_set_long_mode(homeJobLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(homeJobLabel, kText, 0);

  homeStatsGroup = lv_obj_create(homePage);
  lv_obj_remove_style_all(homeStatsGroup);
  lv_obj_add_style(homeStatsGroup, &pageStyle, 0);
  lv_obj_set_pos(homeStatsGroup, 0, 0);
  lv_obj_set_size(homeStatsGroup, kContentW, kScreenH);
  lv_obj_clear_flag(homeStatsGroup, LV_OBJ_FLAG_SCROLLABLE);

  createHomeStatCard(homeStatsGroup, 10, 101, 124, 46, &homeCardTopStyle, "Layer", IconId::Layers,
                     &homeLayersValue, "38 / 100");
  createHomeStatCard(homeStatsGroup, 138, 101, 124, 46, &homeCardTopStyle, "Time", IconId::Print,
                     &homeTimeValue, "2h 21m");
  createHomeStatCard(homeStatsGroup, 10, 151, 124, 46, &homeCardBottomStyle, "Bed", IconId::Bed,
                     &homeBedValue, "69 / 70 C");
  createHomeStatCard(homeStatsGroup, 138, 151, 124, 46, &homeCardBottomStyle, "Nozzle", IconId::Nozzle,
                     &homeNozzleValue, "129 / 130 C");

  const char *controlLabels[4] = {"Pause", "Heat", "Print", "Finish"};
  homeControlsGroup = lv_obj_create(homePage);
  lv_obj_remove_style_all(homeControlsGroup);
  lv_obj_add_style(homeControlsGroup, &pageStyle, 0);
  lv_obj_set_pos(homeControlsGroup, 0, 0);
  lv_obj_set_size(homeControlsGroup, kContentW, kScreenH);
  lv_obj_clear_flag(homeControlsGroup, LV_OBJ_FLAG_SCROLLABLE);
  for (uint8_t i = 0; i < 4; ++i) {
    const lv_coord_t x = 10 + (i * 65) + (i == 3 ? 1 : 0);
    homeControlBars[i] = lv_obj_create(homeControlsGroup);
    lv_obj_remove_style_all(homeControlBars[i]);
    lv_obj_set_pos(homeControlBars[i], x, 207);
    lv_obj_set_size(homeControlBars[i], 56, 4);
    lv_obj_set_style_radius(homeControlBars[i], 999, 0);
    lv_obj_set_style_bg_color(homeControlBars[i], kAccent, 0);
    lv_obj_set_style_bg_opa(homeControlBars[i], LV_OPA_50, 0);
    createText(homeControlsGroup, x, 215, controlLabels[i], &homeControlTextStyle);
  }
}

void initTempsPage() {
  tempsPage = lv_obj_create(root);
  lv_obj_remove_style_all(tempsPage);
  lv_obj_add_style(tempsPage, &pageStyle, 0);
  lv_obj_set_pos(tempsPage, kContentX, 0);
  lv_obj_set_size(tempsPage, kContentW, kScreenH);
  lv_obj_clear_flag(tempsPage, LV_OBJ_FLAG_SCROLLABLE);

  createText(tempsPage, 10, 10, "Temperature", &homeTitleStyle);

  lv_obj_t *chartWrap = lv_obj_create(tempsPage);
  lv_obj_remove_style_all(chartWrap);
  lv_obj_set_pos(chartWrap, 10, 40);
  lv_obj_set_size(chartWrap, 252, 134);
  lv_obj_set_style_bg_opa(chartWrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chartWrap, 0, 0);
  lv_obj_set_style_radius(chartWrap, 0, 0);
  lv_obj_clear_flag(chartWrap, LV_OBJ_FLAG_SCROLLABLE);

  tempsChart = lv_chart_create(chartWrap);
  lv_obj_set_size(tempsChart, 252, 134);
  lv_obj_set_pos(tempsChart, 0, 0);
  lv_obj_set_style_bg_opa(tempsChart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tempsChart, 0, 0);
  lv_obj_set_style_pad_all(tempsChart, 0, 0);
  lv_obj_set_style_pad_gap(tempsChart, 0, 0);
  lv_obj_set_style_line_color(tempsChart, lv_color_hex(0x4B3A47), LV_PART_MAIN);
  lv_obj_set_style_line_width(tempsChart, 1, LV_PART_MAIN);
  lv_obj_set_style_size(tempsChart, 4, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(tempsChart, 2, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(tempsChart, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(tempsChart, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_border_color(tempsChart, lv_color_hex(0x4B3A47), 0);
  lv_obj_set_style_border_width(tempsChart, 1, 0);
  lv_obj_set_style_border_side(tempsChart, LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
  lv_chart_set_type(tempsChart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(tempsChart, LV_CHART_AXIS_PRIMARY_Y, 0, 260);
  lv_chart_set_point_count(tempsChart, kTempHistorySize);
  lv_chart_set_div_line_count(tempsChart, 4, 4);
  tempsNozzleSeries = lv_chart_add_series(tempsChart, kAccent, LV_CHART_AXIS_PRIMARY_Y);
  tempsBedSeries = lv_chart_add_series(tempsChart, kLineBed, LV_CHART_AXIS_PRIMARY_Y);

  createHomeStatCard(tempsPage, 10, 184, 124, 46, &homeCardBottomStyle, "Bed", IconId::Bed,
                     &tempsBedValue, "69 / 70 C");
  createHomeStatCard(tempsPage, 138, 184, 124, 46, &homeCardBottomStyle, "Nozzle", IconId::Nozzle,
                     &tempsNozzleValue, "129 / 130 C");
}

void initPrintPage() {
  printPage = lv_obj_create(root);
  lv_obj_remove_style_all(printPage);
  lv_obj_add_style(printPage, &pageStyle, 0);
  lv_obj_set_pos(printPage, kContentX, 0);
  lv_obj_set_size(printPage, kContentW, kScreenH);
  lv_obj_clear_flag(printPage, LV_OBJ_FLAG_SCROLLABLE);

  createText(printPage, 10, 10, "Print", &homeTitleStyle);

  lv_obj_t *jobCard = lv_obj_create(printPage);
  lv_obj_remove_style_all(jobCard);
  lv_obj_add_style(jobCard, &homeCardTopStyle, 0);
  lv_obj_set_pos(jobCard, 10, 40);
  lv_obj_set_size(jobCard, 252, 50);
  lv_obj_clear_flag(jobCard, LV_OBJ_FLAG_SCROLLABLE);

  createText(jobCard, 0, 0, "Task name", &labelStyle);
  lv_obj_t *taskIcon = createImage(jobCard, 208, 0, &ui_icon_file_list);
  lv_img_set_zoom(taskIcon, 149);

  printJobLabel = createText(jobCard, 0, 20, "-", &labelStyle);
  lv_obj_set_width(printJobLabel, 172);
  lv_label_set_long_mode(printJobLabel, LV_LABEL_LONG_DOT);

  printProgressValue = createText(jobCard, 190, 20, "0%", &labelStyle);

  createPrintStatCard(printPage, 10, 94, 124, "Remaining", IconId::Print, &printRemainValue, "--");
  createPrintStatCard(printPage, 138, 94, 124, "Layer", IconId::Layers, &printLayersValue, "0 / 0");
  createPrintStatCard(printPage, 10, 144, 124, "Speed", IconId::Speed, &printSpeedValue, "Normal");
  createPrintStatCard(printPage, 138, 144, 124, "State", IconId::System, &printStateValue, "Ready");
}

void initSystemPage() {
  systemPage = lv_obj_create(root);
  lv_obj_remove_style_all(systemPage);
  lv_obj_add_style(systemPage, &pageStyle, 0);
  lv_obj_set_pos(systemPage, kContentX, 0);
  lv_obj_set_size(systemPage, kContentW, kScreenH);
  lv_obj_clear_flag(systemPage, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *contentWrap = lv_obj_create(systemPage);
  lv_obj_remove_style_all(contentWrap);
  lv_obj_add_style(contentWrap, &pageStyle, 0);
  lv_obj_set_pos(contentWrap, 0, 0);
  lv_obj_set_size(contentWrap, 240, kScreenH);
  lv_obj_clear_flag(contentWrap, LV_OBJ_FLAG_SCROLLABLE);

  systemInfoPane = lv_obj_create(contentWrap);
  lv_obj_remove_style_all(systemInfoPane);
  lv_obj_add_style(systemInfoPane, &pageStyle, 0);
  lv_obj_set_size(systemInfoPane, 240, kScreenH);
  lv_obj_set_pos(systemInfoPane, 0, 0);
  lv_obj_clear_flag(systemInfoPane, LV_OBJ_FLAG_SCROLLABLE);

  createText(systemInfoPane, 10, 10, "System", &homeTitleStyle);
  createSystemInfoCard(systemInfoPane, 10, 40, 108, 46, "IP", &ui_icon_wifi_fill, &systemIpValue, "-");
  createSystemInfoCard(systemInfoPane, 122, 40, 108, 46, "Serial", &ui_icon_server, &systemSerialValue, "-");
  createSystemInfoCard(systemInfoPane, 10, 90, 108, 46, "Heap", &ui_icon_sdcard, &systemHeapValue, "-");
  createSystemInfoCard(systemInfoPane, 122, 90, 108, 46, "Firmware", &ui_icon_info, &systemFirmwareValue, "-");
  createSystemInfoCard(systemInfoPane, 10, 140, 108, 46, "MQTT", &ui_icon_wifi_fill, &systemMqttValue, "-");
  createSystemInfoCard(systemInfoPane, 122, 140, 108, 46, "Updated", &ui_icon_timer, &systemUpdatedValue, "-");

  systemSettingsPane = lv_obj_create(contentWrap);
  lv_obj_remove_style_all(systemSettingsPane);
  lv_obj_add_style(systemSettingsPane, &pageStyle, 0);
  lv_obj_set_size(systemSettingsPane, 240, kScreenH);
  lv_obj_set_pos(systemSettingsPane, 0, 0);
  lv_obj_clear_flag(systemSettingsPane, LV_OBJ_FLAG_SCROLLABLE);

  createText(systemSettingsPane, 10, 10, "System", &homeTitleStyle);
  createText(systemSettingsPane, 10, 40, "Brightness", &homeBodyStyle);
  const char *brightnessLabels[3] = {"Low", "Mid", "Max"};
  const lv_coord_t brightnessX[3] = {10, 85, 160};
  const lv_coord_t brightnessW[3] = {71, 71, 70};
  for (uint8_t i = 0; i < 3; ++i) {
    systemBrightnessButtons[i] = lv_obj_create(systemSettingsPane);
    lv_obj_remove_style_all(systemBrightnessButtons[i]);
    lv_obj_set_pos(systemBrightnessButtons[i], brightnessX[i], 58);
    lv_obj_set_size(systemBrightnessButtons[i], brightnessW[i], 28);
    lv_obj_set_style_radius(systemBrightnessButtons[i], 12, 0);
    lv_obj_set_style_border_width(systemBrightnessButtons[i], 0, 0);
    lv_obj_clear_flag(systemBrightnessButtons[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(systemBrightnessButtons[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = createText(systemBrightnessButtons[i], 0, 0, brightnessLabels[i], &homeBodyStyle);
    lv_obj_center(label);
  }
  lv_obj_add_event_cb(systemBrightnessButtons[0], brightnessLowEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(systemBrightnessButtons[1], brightnessMidEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(systemBrightnessButtons[2], brightnessMaxEvent, LV_EVENT_CLICKED, nullptr);

  createText(systemSettingsPane, 10, 98, "Touch", &homeBodyStyle);
  lv_obj_t *touchButton = lv_obj_create(systemSettingsPane);
  lv_obj_remove_style_all(touchButton);
  lv_obj_set_pos(touchButton, 10, 116);
  lv_obj_set_size(touchButton, 220, 28);
  lv_obj_set_style_radius(touchButton, 12, 0);
  lv_obj_set_style_border_width(touchButton, 0, 0);
  lv_obj_set_style_bg_color(touchButton, kAccent, 0);
  lv_obj_set_style_bg_opa(touchButton, LV_OPA_50, 0);
  lv_obj_clear_flag(touchButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(touchButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touchButton, calibrateTouchEvent, LV_EVENT_CLICKED, nullptr);
  systemTouchValue = createText(touchButton, 0, 0, "Calibrate Touch", &homeBodyStyle);
  lv_obj_center(systemTouchValue);

  lv_obj_t *rightRail = lv_obj_create(systemPage);
  lv_obj_remove_style_all(rightRail);
  lv_obj_add_style(rightRail, &railStyle, 0);
  lv_obj_set_pos(rightRail, 240, 0);
  lv_obj_set_size(rightRail, 32, kScreenH);
  lv_obj_set_style_radius(rightRail, 0, 0);
  lv_obj_set_style_bg_color(rightRail, kRail, 0);
  lv_obj_set_style_bg_opa(rightRail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rightRail, 0, 0);
  lv_obj_set_style_radius(rightRail, 12, 0);
  lv_obj_clear_flag(rightRail, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *topRightFill = lv_obj_create(rightRail);
  lv_obj_remove_style_all(topRightFill);
  lv_obj_set_pos(topRightFill, 20, 0);
  lv_obj_set_size(topRightFill, 12, 12);
  lv_obj_set_style_bg_color(topRightFill, kRail, 0);
  lv_obj_set_style_bg_opa(topRightFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(topRightFill, 0, 0);
  lv_obj_clear_flag(topRightFill, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bottomRightFill = lv_obj_create(rightRail);
  lv_obj_remove_style_all(bottomRightFill);
  lv_obj_set_pos(bottomRightFill, 20, kScreenH - 12);
  lv_obj_set_size(bottomRightFill, 12, 12);
  lv_obj_set_style_bg_color(bottomRightFill, kRail, 0);
  lv_obj_set_style_bg_opa(bottomRightFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bottomRightFill, 0, 0);
  lv_obj_clear_flag(bottomRightFill, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < 2; ++i) {
    systemSubButtons[i] = lv_obj_create(rightRail);
    lv_obj_remove_style_all(systemSubButtons[i]);
    lv_obj_add_style(systemSubButtons[i], &navButtonStyle, 0);
    lv_obj_set_pos(systemSubButtons[i], 0, i * 120);
    lv_obj_set_size(systemSubButtons[i], 32, 120);
    lv_obj_clear_flag(systemSubButtons[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(systemSubButtons[i], LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_add_event_cb(systemSubButtons[0], systemInfoPaneEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(systemSubButtons[1], systemSettingsPaneEvent, LV_EVENT_CLICKED, nullptr);

  systemSubIcons[0] = lv_img_create(systemSubButtons[0]);
  lv_obj_add_style(systemSubIcons[0], &navIconImageStyle, 0);
  lv_img_set_src(systemSubIcons[0], &ui_icon_info);
  lv_img_set_zoom(systemSubIcons[0], 171);
  lv_obj_center(systemSubIcons[0]);

  systemSubIcons[1] = lv_img_create(systemSubButtons[1]);
  lv_obj_add_style(systemSubIcons[1], &navIconImageStyle, 0);
  lv_img_set_src(systemSubIcons[1], &ui_icon_system);
  lv_img_set_zoom(systemSubIcons[1], 171);
  lv_obj_center(systemSubIcons[1]);

  showSystemPane(SystemPane::Info);
}

void initMenuPage() {
  menuPage = lv_obj_create(root);
  lv_obj_remove_style_all(menuPage);
  lv_obj_add_style(menuPage, &pageStyle, 0);
  lv_obj_set_pos(menuPage, kContentX, 0);
  lv_obj_set_size(menuPage, kContentW, kScreenH);
  lv_obj_clear_flag(menuPage, LV_OBJ_FLAG_SCROLLABLE);

  createText(menuPage, 10, 10, "Menu", &homeTitleStyle);

  createText(menuPage, 10, 40, "Print", &homeBodyStyle);
  menuPauseButton = lv_obj_create(menuPage);
  lv_obj_remove_style_all(menuPauseButton);
  lv_obj_set_pos(menuPauseButton, 10, 58);
  lv_obj_set_size(menuPauseButton, 123, 28);
  lv_obj_set_style_radius(menuPauseButton, 12, 0);
  lv_obj_set_style_border_width(menuPauseButton, 0, 0);
  lv_obj_set_style_bg_color(menuPauseButton, kAccent, 0);
  lv_obj_set_style_bg_opa(menuPauseButton, 38, 0);
  lv_obj_clear_flag(menuPauseButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(menuPauseButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(menuPauseButton, menuPauseEvent, LV_EVENT_CLICKED, nullptr);
  menuPauseLabel = createText(menuPauseButton, 0, 0, "Pause", &homeBodyStyle);
  lv_obj_center(menuPauseLabel);

  menuStopButton = lv_obj_create(menuPage);
  lv_obj_remove_style_all(menuStopButton);
  lv_obj_set_pos(menuStopButton, 139, 58);
  lv_obj_set_size(menuStopButton, 123, 28);
  lv_obj_set_style_radius(menuStopButton, 12, 0);
  lv_obj_set_style_border_width(menuStopButton, 0, 0);
  lv_obj_set_style_bg_color(menuStopButton, kAccent, 0);
  lv_obj_set_style_bg_opa(menuStopButton, 38, 0);
  lv_obj_clear_flag(menuStopButton, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(menuStopButton, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(menuStopButton, menuStopEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *stopLabel = createText(menuStopButton, 0, 0, "Stop", &homeBodyStyle);
  lv_obj_center(stopLabel);

  createText(menuPage, 10, 98, "Speed", &homeBodyStyle);
  const char *speedLabels[4] = {"Sil.", "Stan.", "Sport", "Ludi."};
  const lv_coord_t speedX[4] = {10, 74, 138, 202};
  const lv_coord_t speedW[4] = {59, 59, 59, 60};
  lv_event_cb_t speedEvents[4] = {menuSpeedSilentEvent, menuSpeedStandardEvent, menuSpeedSportEvent,
                                  menuSpeedLudicrousEvent};
  for (uint8_t i = 0; i < 4; ++i) {
    menuSpeedButtons[i] = lv_obj_create(menuPage);
    lv_obj_remove_style_all(menuSpeedButtons[i]);
    lv_obj_set_pos(menuSpeedButtons[i], speedX[i], 116);
    lv_obj_set_size(menuSpeedButtons[i], speedW[i], 28);
    lv_obj_set_style_radius(menuSpeedButtons[i], 12, 0);
    lv_obj_set_style_border_width(menuSpeedButtons[i], 0, 0);
    lv_obj_set_style_bg_color(menuSpeedButtons[i], kAccent, 0);
    lv_obj_set_style_bg_opa(menuSpeedButtons[i], 38, 0);
    lv_obj_clear_flag(menuSpeedButtons[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menuSpeedButtons[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(menuSpeedButtons[i], speedEvents[i], LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = createText(menuSpeedButtons[i], 0, 0, speedLabels[i], &homeBodyStyle);
    lv_obj_center(label);
  }
}

void initCalibrationConfirm() {
  calibrationConfirmOverlay = lv_obj_create(root);
  lv_obj_remove_style_all(calibrationConfirmOverlay);
  lv_obj_add_style(calibrationConfirmOverlay, &overlayStyle, 0);
  lv_obj_set_size(calibrationConfirmOverlay, kScreenW, kScreenH);
  lv_obj_clear_flag(calibrationConfirmOverlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *panel = createCardShell(calibrationConfirmOverlay, 54, 58, 212, 116, false);
  lv_obj_add_style(panel, &modalStyle, 0);
  createText(panel, 0, 0, "Start calibration?", &valueStyle);
  createText(panel, 0, 28, "Current touch mapping will be replaced.", &tinyStyle);
  createActionButton(panel, 0, 70, 84, 28, "Cancel", calibrationCancelEvent);
  createActionButton(panel, 96, 70, 92, 28, "Start", calibrationStartEvent, true);

  lv_obj_add_flag(calibrationConfirmOverlay, LV_OBJ_FLAG_HIDDEN);
}

void initCalibrationOverlay() {
  calibrationOverlay = lv_obj_create(root);
  lv_obj_remove_style_all(calibrationOverlay);
  lv_obj_add_style(calibrationOverlay, &overlayStyle, 0);
  lv_obj_set_size(calibrationOverlay, kScreenW, kScreenH);
  lv_obj_clear_flag(calibrationOverlay, LV_OBJ_FLAG_SCROLLABLE);

  createText(calibrationOverlay, 72, 24, "Tap highlighted point", &valueStyle);
  calibrationStepLabel = createText(calibrationOverlay, 72, 48, "1 / 4", &tinyStyle);

  calibrationCross = lv_obj_create(calibrationOverlay);
  lv_obj_remove_style_all(calibrationCross);
  lv_obj_set_size(calibrationCross, 18, 18);
  lv_obj_set_style_bg_opa(calibrationCross, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(calibrationCross, 0, 0);

  lv_obj_t *h = lv_obj_create(calibrationCross);
  lv_obj_remove_style_all(h);
  lv_obj_set_size(h, 18, 2);
  lv_obj_center(h);
  lv_obj_set_style_bg_color(h, kAccent, 0);
  lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);

  lv_obj_t *v = lv_obj_create(calibrationCross);
  lv_obj_remove_style_all(v);
  lv_obj_set_size(v, 2, 18);
  lv_obj_center(v);
  lv_obj_set_style_bg_color(v, kAccent, 0);
  lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);

  lv_obj_add_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN);
}

void refreshHome() {
  char buffer[24];
  const bool initializing = !currentState.hasData;
  const bool printerOffline = currentState.hasData && currentState.stale;

  if (initializing) {
    lv_label_set_text(homeTitleLabel, "");
    lv_obj_add_flag(homeProgressGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(homeStatsGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(homeControlsGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(homeInitLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(homeProgressGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(homeStatsGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(homeControlsGroup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(homeInitLabel, LV_OBJ_FLAG_HIDDEN);

    if (printerOffline) {
      lv_label_set_text(homeTitleLabel, "Printer Offline");
      lv_label_set_text(homePercentLabel, "??%");
      lv_bar_set_value(homeProgressBar, 0, LV_ANIM_OFF);
      lv_label_set_text(homeJobLabel, "...");
      lv_label_set_text(homeTimeValue, "...");
      lv_label_set_text(homeLayersValue, "... / ...");
      lv_label_set_text(homeBedValue, "... / ... °C");
      lv_label_set_text(homeNozzleValue, "... / ... °C");
    } else {
      lv_label_set_text(homeTitleLabel, "Home");
      lv_label_set_text(homeJobLabel, currentState.jobName);
      lv_label_set_text_fmt(homePercentLabel, "%d%%", currentState.percent);
      lv_bar_set_value(homeProgressBar, currentState.percent, LV_ANIM_OFF);
      formatDuration(currentState.remainingMinutes, buffer, sizeof(buffer));
      lv_label_set_text(homeTimeValue, buffer);
      formatLayers(currentState, buffer, sizeof(buffer));
      lv_label_set_text(homeLayersValue, buffer);
      formatTemp(currentState.bedTemp, currentState.bedTargetTemp, buffer, sizeof(buffer));
      lv_label_set_text(homeBedValue, buffer);
      formatTemp(currentState.nozzleTemp, currentState.nozzleTargetTemp, buffer, sizeof(buffer));
      lv_label_set_text(homeNozzleValue, buffer);
    }
  }

  const uint8_t activeIdx = initializing || printerOffline
                                ? 0
                                : strcmp(currentState.stageText, "PAUSE") == 0
                                      ? 0
                                      : isHeating(currentState) ? 1
                                                                : strcmp(currentState.stageText, "FINISH") == 0 ? 3 : 2;
  for (uint8_t i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_opa(homeControlBars[i], i == activeIdx ? LV_OPA_COVER : LV_OPA_50, 0);
  }

  const bool dimSecondaryNav = activeSection == Section::Home && (initializing || printerOffline);
  for (uint8_t i = 0; i < 5; ++i) {
    const lv_opa_t opa = (!dimSecondaryNav || i == 0) ? LV_OPA_COVER : LV_OPA_50;
    lv_obj_set_style_opa(navButtons[i], opa, 0);
  }
}

void refreshTemps() {
  char buffer[24];
  pushTempSample();
  for (uint8_t i = 0; i < kTempHistorySize; ++i) {
    tempsNozzleSeries->y_points[i] = nozzleHistory[i];
    tempsBedSeries->y_points[i] = bedHistory[i];
  }
  lv_chart_refresh(tempsChart);

  formatTemp(currentState.nozzleTemp, currentState.nozzleTargetTemp, buffer, sizeof(buffer));
  lv_label_set_text(tempsNozzleValue, buffer);
  formatTemp(currentState.bedTemp, currentState.bedTargetTemp, buffer, sizeof(buffer));
  lv_label_set_text(tempsBedValue, buffer);
}

void refreshPrint() {
  char buffer[24];
  lv_label_set_text(printJobLabel, currentState.jobName);
  lv_label_set_text_fmt(printProgressValue, "%d%%", currentState.percent);
  formatDuration(currentState.remainingMinutes, buffer, sizeof(buffer));
  lv_label_set_text(printRemainValue, buffer);
  formatLayers(currentState, buffer, sizeof(buffer));
  lv_label_set_text(printLayersValue, buffer);
  lv_label_set_text(printSpeedValue, speedText(currentState.speedLevel));
  lv_label_set_text(printStateValue, statusText(currentState));
}

void refreshSystem() {
  lv_label_set_text(systemIpValue, systemIpText);
  lv_label_set_text(systemSerialValue, systemSerialText);
  lv_label_set_text(systemHeapValue, systemHeapText);
  lv_label_set_text(systemFirmwareValue, systemFirmwareText);
  lv_label_set_text(systemMqttValue, systemMqttText);
  lv_label_set_text(systemUpdatedValue, systemUpdatedText);

  const uint8_t activeBrightnessIdx = brightnessPercent <= 33 ? 0 : brightnessPercent <= 66 ? 1 : 2;
  for (uint8_t i = 0; i < 3; ++i) {
    lv_obj_set_style_bg_color(systemBrightnessButtons[i], kAccent, 0);
    lv_obj_set_style_bg_opa(systemBrightnessButtons[i], i == activeBrightnessIdx ? LV_OPA_50 : 38, 0);
  }
  lv_label_set_text(systemTouchValue, "Calibrate Touch");

  const bool controlEnabled = canControlPrint(currentState);
  lv_label_set_text(menuPauseLabel, isPaused(currentState) ? "Resume" : "Pause");
  lv_obj_center(menuPauseLabel);
  lv_obj_set_style_bg_color(menuPauseButton, kAccent, 0);
  lv_obj_set_style_bg_opa(menuPauseButton, controlEnabled ? LV_OPA_50 : 24, 0);
  lv_obj_set_style_bg_color(menuStopButton, kAccent, 0);
  lv_obj_set_style_bg_opa(menuStopButton, controlEnabled ? LV_OPA_50 : 24, 0);
  if (controlEnabled) {
    lv_obj_add_flag(menuPauseButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(menuStopButton, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_flag(menuPauseButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(menuStopButton, LV_OBJ_FLAG_CLICKABLE);
  }

  for (uint8_t i = 0; i < 4; ++i) {
    lv_obj_set_style_bg_color(menuSpeedButtons[i], kAccent, 0);
    const lv_opa_t baseOpa = (i + 1) == currentState.speedLevel ? LV_OPA_50 : 38;
    lv_obj_set_style_bg_opa(menuSpeedButtons[i], controlEnabled ? baseOpa : 24, 0);
    if (controlEnabled) {
      lv_obj_add_flag(menuSpeedButtons[i], LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_clear_flag(menuSpeedButtons[i], LV_OBJ_FLAG_CLICKABLE);
    }
  }
}

}  // namespace

void setActionHandler(ActionHandler handler) { actionHandler = handler; }

void setBrightnessPercent(uint8_t percent) {
  brightnessPercent = percent;
  if (!root) return;
  refreshSystem();
}

void setSystemServiceInfo(const char *ip, const char *serial, const char *heap, const char *firmware,
                          const char *mqtt, const char *updated) {
  snprintf(systemIpText, sizeof(systemIpText), "%s", ip ? ip : "-");
  snprintf(systemSerialText, sizeof(systemSerialText), "%s", serial ? serial : "-");
  snprintf(systemHeapText, sizeof(systemHeapText), "%s", heap ? heap : "-");
  snprintf(systemFirmwareText, sizeof(systemFirmwareText), "%s", firmware ? firmware : "-");
  snprintf(systemMqttText, sizeof(systemMqttText), "%s", mqtt ? mqtt : "-");
  snprintf(systemUpdatedText, sizeof(systemUpdatedText), "%s", updated ? updated : "-");
  if (!root) return;
  refreshSystem();
}

void init() {
  initStyles();
  if (lv_disp_t *disp = lv_disp_get_default()) {
    lv_disp_set_theme(disp, nullptr);
  }

  root = lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(root);
  lv_obj_add_style(root, &screenStyle, 0);
  lv_obj_set_size(root, kScreenW, kScreenH);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  initRail();
  initHomePage();
  initTempsPage();
  initPrintPage();
  initSystemPage();
  initMenuPage();
  initCalibrationConfirm();
  initCalibrationOverlay();

  showPage(Section::Home);
  applyState(currentState);
}

void applyState(const PrinterState &state) {
  currentState = state;
  setLiveBadge(state.stale || !state.hasData);
  refreshHome();
  refreshTemps();
  refreshPrint();
  refreshSystem();
}

void showCalibrationStep(uint8_t step, uint8_t total, int x, int y) {
  closeCalibrationConfirm();
  lv_obj_clear_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(calibrationOverlay);
  lv_label_set_text_fmt(calibrationStepLabel, "%u / %u", step, total);
  lv_obj_set_pos(calibrationCross, x - 9, y - 9);
}

void hideCalibration() { lv_obj_add_flag(calibrationOverlay, LV_OBJ_FLAG_HIDDEN); }

}  // namespace ui

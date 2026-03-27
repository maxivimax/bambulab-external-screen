#pragma once

// Wi-Fi credentials for the same LAN where the printer is reachable.
static constexpr char WIFI_SSID[] = "";
static constexpr char WIFI_PASSWORD[] = "";

// Bambu printer local MQTT settings.
static constexpr char BAMBU_PRINTER_IP[] = "192.168.1.97";
static constexpr uint16_t BAMBU_MQTT_PORT = 8883;
static constexpr char BAMBU_PRINTER_SERIAL[] = "";
static constexpr char BAMBU_ACCESS_CODE[] = "";
static constexpr char BAMBU_MQTT_USERNAME[] = "bblp";

// ESP32-2432S028R / CYD (2.8" ILI9341 + XPT2046).
static constexpr int LCD_SCLK_PIN = 14;
static constexpr int LCD_MOSI_PIN = 13;
static constexpr int LCD_MISO_PIN = 12;
static constexpr int LCD_CS_PIN = 15;
static constexpr int LCD_DC_PIN = 2;
static constexpr int LCD_RST_PIN = -1;
static constexpr int LCD_BL_PIN = 21;
static constexpr bool LCD_BL_INVERT = true;
static constexpr int LCD_ROTATION = 1;
static constexpr int LCD_HOR_RES = 320;
static constexpr int LCD_VER_RES = 240;

// Touch controller pins for the CYD board.
static constexpr int TOUCH_IRQ_PIN = 36;
static constexpr int TOUCH_MOSI_PIN = 32;
static constexpr int TOUCH_MISO_PIN = 39;
static constexpr int TOUCH_CLK_PIN = 25;
static constexpr int TOUCH_CS_PIN = 33;
static constexpr int TOUCH_RAW_MIN_X = 200;
static constexpr int TOUCH_RAW_MAX_X = 3800;
static constexpr int TOUCH_RAW_MIN_Y = 240;
static constexpr int TOUCH_RAW_MAX_Y = 3800;
static constexpr bool TOUCH_SWAP_XY = true;
static constexpr bool TOUCH_INVERT_X = false;
static constexpr bool TOUCH_INVERT_Y = true;

// UI timings.
static constexpr uint32_t WIFI_RETRY_MS = 15000;
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr uint32_t UI_REFRESH_MS = 500;
static constexpr uint32_t PUSHALL_INTERVAL_MS = 30000;
static constexpr uint32_t STALE_AFTER_MS = 45000;

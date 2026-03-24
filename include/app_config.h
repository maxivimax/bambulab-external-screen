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

// Waveshare 2.4" ILI9341 wiring to BPI-Leaf-S3.
// Change these pins to match your actual wiring.
static constexpr int LCD_SCLK_PIN = 13;
static constexpr int LCD_MOSI_PIN = 14;
static constexpr int LCD_MISO_PIN = -1;
static constexpr int LCD_CS_PIN = 12;
static constexpr int LCD_DC_PIN = 11;
static constexpr int LCD_RST_PIN = 10;
static constexpr int LCD_BL_PIN = 9;
static constexpr bool LCD_BL_INVERT = false;
static constexpr int LCD_ROTATION = 0;

// UI timings.
static constexpr uint32_t WIFI_RETRY_MS = 15000;
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr uint32_t UI_REFRESH_MS = 500;
static constexpr uint32_t PUSHALL_INTERVAL_MS = 30000;
static constexpr uint32_t STALE_AFTER_MS = 45000;

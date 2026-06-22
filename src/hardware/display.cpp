#include "hardware/display.h"
#include "hardware/display_init.h"

#include "hardware/display_brightness.h"
#include "hardware/display_font.h"
#include "hardware/pin_config.h"

namespace {

Arduino_DataBus* s_bus = nullptr;
Arduino_ESP32RGBPanel* s_panel = nullptr;
Arduino_RGB_Display *gfx = nullptr;

}  // namespace

PlaneGfx tft;

void displayInit() {

  
  /*
  pinMode(LCD_VCI_EN, OUTPUT);
  digitalWrite(LCD_VCI_EN, HIGH);

  s_bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2,
                                  LCD_SDIO3);
  s_panel = new Arduino_SH8601(s_bus, LCD_RST, 0, false, LCD_WIDTH, LCD_HEIGHT);

  if (!s_panel->begin(40000000)) {
    Serial.println("Display init failed");
  }
  

  tft.attach(s_panel, true);
  tft.fillScreen(BLACK);

  for (uint8_t brightness = 0; brightness < 255; ++brightness) {
    s_panel->Display_Brightness(brightness);
    delay(2);
  }
  s_panel->SetContrast(SH8601_ContrastOff);
  */
  size_t beforeHeap =ESP.getFreeHeap();
  size_t beforePsram = ESP.getFreePsram();

  //s_bus = new Arduino_ESP32SPI(GFX_NOT_DEFINED /* DC */, 16 /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */, HSPI /* spi_num */);
  s_bus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, 16 /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */);
  s_panel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 7 /* VSYNC */, 15 /* HSYNC */, 41 /* PCLK */,
    46 /* R0 */, 3 /* R1 */, 8 /* R2 */, 18 /* R3 */, 17 /* R4 */,
    14 /* G0/P22 */, 13 /* G1/P23 */, 12 /* G2/P24 */, 11 /* G3/P25 */, 10 /* G4/P26 */, 9 /* G5 */,
    5 /* B0 */, 45 /* B1 */, 48 /* B2 */, 47 /* B3 */, 21 /* B4 */,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
    1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */,
    0 /* pclk_active_neg */, 12000000 /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */, 10*480 /* bounce_buffer_size_px */);

  gfx = new Arduino_RGB_Display(
    480 /* width */, 480 /* height */, s_panel , 0 /* rotation */, true /* auto_flush */,
    s_bus, GFX_NOT_DEFINED /* RST */, elecrow_init_operations, sizeof(elecrow_init_operations));

   // Init Display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  //gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(GFX_BL, 5000, 8);
  ledcWrite(GFX_BL, 204); 
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(GFX_BL, 0);
  ledcWrite(0, 204);
#endif
 
  tft.attach(gfx);
  tft.fillScreen(RGB565_BLACK);

  hardware::displayBrightnessBootLoad();
  hardware::displayApplyBrightness();

  tft.setTextWrap(false);

  size_t afterPsram = ESP.getFreePsram();
  Serial.printf("PSRAM consumed by canvas: %d bytes\n", beforePsram - afterPsram);
  size_t afterHeap = ESP.getFreeHeap();
  Serial.printf("HEAP consumed by canvas: %d bytes\n", beforeHeap - afterHeap);
}

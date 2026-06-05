#include "hardware/display.h"

#include "hardware/display_brightness.h"
#include "hardware/display_font.h"
#include "hardware/pin_config.h"

namespace {

Arduino_DataBus* s_bus = nullptr;
Arduino_GFX* s_panel = nullptr;

}  // namespace

PlaneGfx tft;

void displayInit() {
  pinMode(LCD_VCI_EN, OUTPUT);
  digitalWrite(LCD_VCI_EN, HIGH);

  s_bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2,
                                  LCD_SDIO3);
  s_panel = new Arduino_SH8601(s_bus, LCD_RST, 0, false, LCD_WIDTH, LCD_HEIGHT);

  if (!s_panel->begin(40000000)) {
    Serial.println("Display init failed");
  }

  tft.attach(s_panel);
  tft.fillScreen(BLACK);

  for (uint8_t brightness = 0; brightness < 255; ++brightness) {
    s_panel->Display_Brightness(brightness);
    delay(2);
  }
  s_panel->SetContrast(SH8601_ContrastOff);

  hardware::displayBrightnessBootLoad();
  hardware::displayApplyBrightness();

  tft.setTextWrap(false);
  displayFontInit();
}

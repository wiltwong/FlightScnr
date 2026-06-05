/**
 * FlightScnr — WiFi setup, then radar UI on the T-Encoder Pro AMOLED display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/input.h"
#include "services/adsb_client.h"
#include "services/clock_time.h"
#include "services/map_center.h"
#include "services/route_lookup.h"
#include "services/settings_web.h"
#include "services/wifi_setup.h"
#include "ui/clock_screen.h"
#include "ui/clock_settings_screen.h"
#include "ui/flight_detail_screen.h"
#include "ui/info_screen.h"
#include "ui/radar_display.h"
#include "ui/radar_scale.h"
#include "ui/radar_theme.h"

namespace {

enum class AppScreen : uint8_t { Radar, FlightDetail, Settings, Clock, ClockSettings };

AppScreen g_screen = AppScreen::Radar;
bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_radar_frame_ms = 0;
unsigned long g_secondary_activity_ms = 0;
uint32_t g_last_clock_minute_stamp = UINT32_MAX;

void noteSecondaryActivity() {
  if (g_screen != AppScreen::Radar) {
    g_secondary_activity_ms = millis();
  }
}

void showRadar() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
  g_last_radar_frame_ms = millis();
  g_last_adsb_fetch_ms = millis();
}

void showFlightDetail() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  ui::flightDetailDraw();
  g_radar_visible = false;
}

void showSettings() {
  ui::infoScreenDraw();
  g_radar_visible = false;
}

void showClock() {
  ui::clockScreenDraw();
  g_radar_visible = false;
  g_last_clock_minute_stamp = services::clock::localMinuteStamp();
}

void showClockSettings() {
  ui::clockSettingsScreenDraw();
  g_radar_visible = false;
}

void returnToRadar(bool from_idle_timeout = false) {
  if (from_idle_timeout) {
    ui::infoScreenResetToMain();
    ui::clockSettingsResetFocus();
    inputDiscardPendingInteractions();
  }
  g_screen = AppScreen::Radar;
  g_radar_visible = false;
  if (WiFi.status() == WL_CONNECTED) {
    showRadar();
  }
  Serial.println(from_idle_timeout ? "Screen: radar (timeout)"
                                   : "Screen: radar");
}

void returnToClockFromIdleTimeout() {
  ui::clockSettingsResetFocus();
  inputDiscardPendingInteractions();
  g_screen = AppScreen::Clock;
  showClock();
  Serial.println("Screen: clock (timeout)");
}

void openSettingsFromRadar() {
  ui::infoScreenResetToMain();
  g_screen = AppScreen::Settings;
  noteSecondaryActivity();
  showSettings();
  Serial.println("Screen: settings (1/2)");
}

void openClockFromRadar() {
  g_screen = AppScreen::Clock;
  showClock();
  Serial.println("Screen: clock");
}

void openClockSettingsFromClock() {
  ui::clockSettingsResetFocus();
  g_screen = AppScreen::ClockSettings;
  noteSecondaryActivity();
  showClockSettings();
  Serial.println("Screen: clock settings");
}

void openFlightDetailFromRadar(int16_t tap_x, int16_t tap_y, bool from_screen_tap) {
  if (from_screen_tap) {
    ui::flightDetailSelectAtScreen(tap_x, tap_y);
  } else {
    ui::flightDetailSelectClosest();
  }
  g_screen = AppScreen::FlightDetail;
  noteSecondaryActivity();
  showFlightDetail();
  Serial.println("Screen: flight detail");
}

void onFlightDetailStep(int8_t delta) {
  if (g_screen != AppScreen::FlightDetail || delta == 0) {
    return;
  }
  if (ui::flightDetailCycle(delta)) {
    showFlightDetail();
  }
}

void onRangeStep(int8_t delta) {
  if (g_screen != AppScreen::Radar || delta == 0) {
    return;
  }

  if (delta > 0) {
    ui::radar::scaleIncrease();
  } else {
    ui::radar::scaleDecrease();
  }
  char range_label[12];
  ui::radar::formatActiveScaleTag(range_label, sizeof(range_label));
  Serial.printf("Scale: %s (coverage ~%.0f km)\n", range_label,
                ui::radar::scaleActive().coverage_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleNavigation() {
  const SwipeGesture swipe = inputConsumeSwipe();
  if (swipe != SwipeNone && g_screen != AppScreen::Radar &&
      g_screen != AppScreen::Clock) {
    noteSecondaryActivity();
  }
  if (swipe == SwipeDown && g_screen == AppScreen::Radar) {
    openClockFromRadar();
  } else if (swipe == SwipeUp && g_screen == AppScreen::Clock) {
    returnToRadar(false);
  } else if (swipe == SwipeLeft && g_screen == AppScreen::Clock) {
    openClockSettingsFromClock();
  } else if (swipe == SwipeRight && g_screen == AppScreen::ClockSettings) {
    g_screen = AppScreen::Clock;
    showClock();
    Serial.println("Screen: clock");
  } else if (swipe == SwipeLeft && g_screen == AppScreen::Radar) {
    openSettingsFromRadar();
  } else if (swipe == SwipeLeft && g_screen == AppScreen::Settings &&
             ui::infoScreenPage() == ui::InfoSettingsPage::Main) {
    ui::infoScreenSetPage(ui::InfoSettingsPage::Display);
    ui::infoScreenResetDisplayFocus();
    showSettings();
    Serial.println("Screen: settings (2/2)");
  } else if (swipe == SwipeRight && g_screen == AppScreen::FlightDetail) {
    returnToRadar(false);
  } else if (swipe == SwipeRight && g_screen == AppScreen::Settings &&
             ui::infoScreenPage() == ui::InfoSettingsPage::Display) {
    ui::infoScreenSetPage(ui::InfoSettingsPage::Main);
    showSettings();
    Serial.println("Screen: settings (1/2)");
  } else if (swipe == SwipeRight && g_screen == AppScreen::Settings) {
    returnToRadar(false);
  }
}

void tickSecondaryScreenTimeout() {
  if (g_screen == AppScreen::Radar || g_screen == AppScreen::Clock) {
    return;
  }
  if (millis() - g_secondary_activity_ms >= config::kSecondaryScreenTimeoutMs) {
    if (g_screen == AppScreen::ClockSettings) {
      returnToClockFromIdleTimeout();
    } else {
      returnToRadar(true);
    }
  }
}

void tickClockDisplay() {
  if (g_screen != AppScreen::Clock) {
    return;
  }
  const uint32_t stamp = services::clock::localMinuteStamp();
  if (stamp == g_last_clock_minute_stamp) {
    return;
  }
  g_last_clock_minute_stamp = stamp;
  ui::clockScreenDraw();
}

void handleInput() {
  inputPoll();
  inputPollLongPress();
  handleNavigation();

  if (g_screen == AppScreen::Radar) {
    int16_t tx = 0;
    int16_t ty = 0;
    if (inputConsumeScreenTap(&tx, &ty)) {
      openFlightDetailFromRadar(tx, ty, true);
      return;
    }
    if (inputConsumeKnobTap()) {
      openFlightDetailFromRadar(0, 0, false);
      return;
    }
    const int8_t enc = inputConsumeEncoderDelta();
    if (enc != 0) {
      onRangeStep(enc);
    }
    return;
  }

  if (g_screen == AppScreen::FlightDetail) {
    const int8_t enc = inputConsumeEncoderDelta();
    if (enc != 0) {
      noteSecondaryActivity();
      onFlightDetailStep(enc);
    }
    return;
  }

  if (g_screen == AppScreen::Settings) {
    if (ui::infoScreenPage() == ui::InfoSettingsPage::Display &&
        inputConsumeKnobTap()) {
      noteSecondaryActivity();
      ui::infoScreenCycleDisplayFocus();
      showSettings();
      return;
    }
    const int8_t enc = inputConsumeEncoderDelta();
    if (enc != 0) {
      noteSecondaryActivity();
      ui::infoScreenHandleKnob(enc);
    }
    return;
  }

  if (g_screen == AppScreen::ClockSettings) {
    if (inputConsumeKnobTap()) {
      noteSecondaryActivity();
      ui::clockSettingsCycleFocus();
      showClockSettings();
      return;
    }
    const int8_t enc = inputConsumeEncoderDelta();
    if (enc != 0) {
      noteSecondaryActivity();
      ui::clockSettingsHandleKnob(enc);
    }
    return;
  }
}

void tickRadarAnimation() {
  if (g_screen != AppScreen::Radar || !g_radar_visible) {
    return;
  }

  const unsigned long now = millis();
  if (now - g_last_radar_frame_ms < ui::radar::kSweepFrameMs) {
    return;
  }
  g_last_radar_frame_ms = now;
  ui::radarDisplayRefreshSweep();
}

void tickAdsbFetch() {
  const bool on_radar = g_screen == AppScreen::Radar && g_radar_visible;
  const bool on_detail = g_screen == AppScreen::FlightDetail;
  if (!on_radar && !on_detail) {
    return;
  }

  if (services::adsb::fetchReady()) {
    services::adsb::fetchConsume();
    if (g_screen == AppScreen::Radar && g_radar_visible) {
      ui::radarDisplayRefreshAircraft();
    } else if (g_screen == AppScreen::FlightDetail) {
      ui::flightDetailDraw();
    }
  }

  const unsigned long now = millis();
  if (now - g_last_adsb_fetch_ms < config::kTrafficPollIntervalMs) {
    return;
  }
  if (services::adsb::fetchInProgress()) {
    return;
  }

  if (!on_radar && !on_detail) {
    return;
  }

  const float fetch_km = ui::radar::adsbQueryRadiusKm();
  if (services::adsb::fetchRequest(services::map_center::latitude(),
                                   services::map_center::longitude(), fetch_km)) {
    g_last_adsb_fetch_ms = now;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("FlightScnr (T-Encoder Pro)");

  inputInit();
  displayInit();
  services::map_center::bootLoad();
  ui::radar::scaleBootLoad();
  services::adsb::trafficFilterBootLoad();
  services::clock::bootLoad();

  if (wifiSetupConnect()) {
    services::clock::startNtp();
    services::route::init();
    services::adsb::fetchInit();
    showRadar();
  }
}

void loop() {
  tickSecondaryScreenTimeout();
  tickClockDisplay();
  handleInput();
  settingsWebPoll();
  services::route::tickCacheFlush(millis());

  if (WiFi.status() != WL_CONNECTED) {
    settingsWebStop();
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        services::clock::startNtp();
        if (g_screen == AppScreen::Radar) {
          showRadar();
        } else if (g_screen == AppScreen::FlightDetail) {
          showFlightDetail();
        } else if (g_screen == AppScreen::Clock) {
          showClock();
        } else if (g_screen == AppScreen::ClockSettings) {
          showClockSettings();
        } else {
          showSettings();
        }
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (g_screen == AppScreen::Radar) {
      if (!g_radar_visible) {
        showRadar();
      } else {
        tickRadarAnimation();
        tickAdsbFetch();
      }
    } else if (g_screen == AppScreen::FlightDetail) {
      tickAdsbFetch();
    }
  }

  delay(1);
}

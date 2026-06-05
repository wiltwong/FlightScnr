#include "ui/flight_detail_screen.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "geo/flat_earth.h"
#include "services/map_center.h"
#include "ui/radar_scale.h"
#include "ui/radar_theme.h"

namespace ui {
namespace {

constexpr int kBezelInsetPx = 10;
constexpr int kTextPadPx = 6;
constexpr int kTitleGap = 4;
constexpr int kLineGap = 3;
constexpr int kSectionGap = 6;
constexpr int kFooterGap = 6;
constexpr int kTapPickRadiusPx = 36;

const int kCenterX = config::kDisplayWidth / 2;
const int kCenterY = config::kDisplayHeight / 2;
const int kCircleRadius = kCenterX - kBezelInsetPx;

uint8_t s_order[services::adsb::kMaxAircraft];
size_t s_order_count = 0;
size_t s_sel = 0;

float aircraftDistKm(const services::adsb::Aircraft& ac) {
  float dx = 0.0f;
  float dy = 0.0f;
  float dist = 0.0f;
  geo::localOffsetKm(services::map_center::latitude(), services::map_center::longitude(),
                     ac.lat, ac.lon, &dx, &dy, &dist);
  return dist;
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::scaleActive().label_km;
  const float px_per_km =
      static_cast<float>(radar::kGridOuterRadius) / outer_km;
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  geo::localOffsetKm(services::map_center::latitude(), services::map_center::longitude(),
                     lat, lon, &dx_km, &dy_km, &dist_km);
  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

int circleHalfWidthAtRow(int row_y, int row_h) {
  if (kCircleRadius <= 0 || row_h <= 0) {
    return 0;
  }
  const int row_center_y = row_y + row_h / 2;
  const int dy = row_center_y - kCenterY;
  if (std::abs(dy) >= kCircleRadius) {
    return 0;
  }
  const float half =
      std::sqrt(static_cast<float>(kCircleRadius * kCircleRadius - dy * dy));
  const int usable = static_cast<int>(half) - kTextPadPx;
  return usable > 0 ? usable : 0;
}

void fitLineToWidth(char* text, size_t len, UiTextStyle style, int max_width_px) {
  if (max_width_px <= 0 || text[0] == '\0') {
    return;
  }
  displayFontApply(tft, style);
  if (tft.textWidth(text) <= max_width_px) {
    return;
  }
  const size_t raw_len = strlen(text);
  for (size_t n = raw_len; n > 0; --n) {
    snprintf(text, len, "%.*s…", static_cast<int>(n), text);
    if (tft.textWidth(text) <= max_width_px) {
      return;
    }
  }
  strncpy(text, "…", len);
  text[len - 1] = '\0';
}

void drawCenterLine(const char* text, int* y, UiTextStyle style, uint16_t fg,
                    uint16_t bg) {
  displayFontApply(tft, style);
  const int h = displayFontHeight(tft, style);
  const int max_half_w = circleHalfWidthAtRow(*y, h);
  const int max_w = max_half_w * 2;

  char line[48];
  strncpy(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  fitLineToWidth(line, sizeof(line), style, max_w);

  tft.setTextDatum(TextDatum::TopCenter);
  tft.setTextColor(fg, bg);
  tft.drawString(line, kCenterX, *y);
  *y += h + kLineGap;
}

void rebuildOrderByDistance() {
  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  s_order_count = n;
  for (size_t i = 0; i < n; ++i) {
    s_order[i] = static_cast<uint8_t>(i);
  }

  for (size_t i = 0; i + 1 < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (aircraftDistKm(planes[s_order[j]]) < aircraftDistKm(planes[s_order[i]])) {
        const uint8_t tmp = s_order[i];
        s_order[i] = s_order[j];
        s_order[j] = tmp;
      }
    }
  }
}

const services::adsb::Aircraft* selectedAircraft() {
  if (s_order_count == 0 || s_sel >= s_order_count) {
    return nullptr;
  }
  return &services::adsb::aircraftList()[s_order[s_sel]];
}

void formatRouteLine(const services::adsb::Aircraft& ac, char* out, size_t out_len) {
  if (ac.route_origin[0] != '\0' && ac.route_dest[0] != '\0') {
    snprintf(out, out_len, "%s > %s", ac.route_origin, ac.route_dest);
  } else if (ac.route_origin[0] != '\0') {
    snprintf(out, out_len, "%s > ?", ac.route_origin);
  } else if (ac.route_dest[0] != '\0') {
    snprintf(out, out_len, "? > %s", ac.route_dest);
  } else {
    strncpy(out, "Route unknown", out_len - 1);
    out[out_len - 1] = '\0';
  }
}

void formatAltLine(const services::adsb::Aircraft& ac, char* out, size_t out_len) {
  if (ac.alt[0] != '\0') {
    snprintf(out, out_len, "Alt: %s", ac.alt);
  } else {
    strncpy(out, "Alt: —", out_len - 1);
    out[out_len - 1] = '\0';
  }
}

void formatSpeedLine(const services::adsb::Aircraft& ac, char* out, size_t out_len) {
  if (ac.gs_knots > 0.5f) {
    snprintf(out, out_len, "Speed: %d kt", static_cast<int>(lroundf(ac.gs_knots)));
  } else {
    strncpy(out, "Speed: —", out_len - 1);
    out[out_len - 1] = '\0';
  }
}

void formatTypeLine(const services::adsb::Aircraft& ac, char* out, size_t out_len) {
  if (ac.type[0] != '\0') {
    strncpy(out, ac.type, out_len - 1);
    out[out_len - 1] = '\0';
  } else {
    strncpy(out, "—", out_len - 1);
    out[out_len - 1] = '\0';
  }
}

}  // namespace

void flightDetailSelectClosest() {
  rebuildOrderByDistance();
  s_sel = 0;
}

void flightDetailSelectAtScreen(int16_t x, int16_t y) {
  rebuildOrderByDistance();
  if (s_order_count == 0) {
    return;
  }

  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  int best_i = 0;
  int best_d2 = INT32_MAX;
  const int pick_r2 = kTapPickRadiusPx * kTapPickRadiusPx;

  for (size_t i = 0; i < s_order_count; ++i) {
    const services::adsb::Aircraft& ac = planes[s_order[i]];
    int sx = 0;
    int sy = 0;
    latLonToScreen(ac.lat, ac.lon, &sx, &sy);
    const int dx = sx - x;
    const int dy = sy - y;
    const int d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      best_i = static_cast<int>(i);
    }
  }

  if (best_d2 <= pick_r2) {
    s_sel = static_cast<size_t>(best_i);
  } else {
    s_sel = 0;
  }
}

bool flightDetailCycle(int delta) {
  if (s_order_count == 0) {
    return false;
  }
  if (delta == 0) {
    return true;
  }
  const int n = static_cast<int>(s_order_count);
  int idx = static_cast<int>(s_sel) + delta;
  while (idx < 0) {
    idx += n;
  }
  while (idx >= n) {
    idx -= n;
  }
  s_sel = static_cast<size_t>(idx);
  return true;
}

bool flightDetailHasSelection() { return s_order_count > 0; }

size_t flightDetailCount() { return s_order_count; }

void flightDetailDraw() {
  const uint16_t bg = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  const uint16_t fg = tft.color565(255, 255, 255);
  const uint16_t label_fg = tft.color565(180, 200, 220);
  const uint16_t route_fg = tft.color565(100, 220, 255);
  const uint16_t hint_fg = tft.color565(120, 140, 160);

  tft.fillScreen(bg);

  if (s_order_count == 0) {
    rebuildOrderByDistance();
  }

  const services::adsb::Aircraft* ac = selectedAircraft();
  if (ac == nullptr) {
    int y = kCenterY - 20;
    drawCenterLine("Flight", &y, displayFontTitle(), fg, bg);
    drawCenterLine("No aircraft", &y, displayFontBody(), label_fg, bg);
    drawCenterLine("Swipe right", &y, displayFontDetail(), hint_fg, bg);
    drawCenterLine("for radar", &y, displayFontDetail(), hint_fg, bg);
    tft.setTextDatum(TextDatum::TopLeft);
    return;
  }

  char callsign[16];
  char airline[32];
  char route[20];
  char type[16];
  char alt[20];
  char speed[20];
  char index_line[16];

  if (ac->callsign[0] != '\0') {
    strncpy(callsign, ac->callsign, sizeof(callsign) - 1);
    callsign[sizeof(callsign) - 1] = '\0';
  } else {
    strncpy(callsign, "—", sizeof(callsign) - 1);
  }

  if (ac->airline[0] != '\0') {
    strncpy(airline, ac->airline, sizeof(airline) - 1);
    airline[sizeof(airline) - 1] = '\0';
  } else {
    strncpy(airline, "Airline unknown", sizeof(airline) - 1);
  }

  formatRouteLine(*ac, route, sizeof(route));
  formatTypeLine(*ac, type, sizeof(type));
  formatAltLine(*ac, alt, sizeof(alt));
  formatSpeedLine(*ac, speed, sizeof(speed));
  snprintf(index_line, sizeof(index_line), "%u / %u",
           static_cast<unsigned>(s_sel + 1), static_cast<unsigned>(s_order_count));

  const int title_h = displayFontHeight(tft, displayFontTitle());
  const int callsign_h = displayFontHeight(tft, displayFontBody());
  const int body_h = displayFontHeight(tft, displayFontBody());
  const int detail_h = displayFontHeight(tft, displayFontDetail());
  const int block_h = title_h + kTitleGap + callsign_h + kLineGap + body_h + kSectionGap +
                      body_h + kLineGap + detail_h + kLineGap + detail_h + kLineGap +
                      detail_h + kFooterGap + detail_h + kLineGap + detail_h;

  int y = kCenterY - block_h / 2;
  if (y < kBezelInsetPx) {
    y = kBezelInsetPx;
  }

  displayFontApply(tft, displayFontTitle());
  tft.setTextDatum(TextDatum::TopCenter);
  tft.setTextColor(fg, bg);
  tft.drawString("Flight", kCenterX, y);
  y += title_h + kTitleGap;

  drawCenterLine(callsign, &y, displayFontBody(), fg, bg);
  drawCenterLine(airline, &y, displayFontBody(), label_fg, bg);
  y += kSectionGap - kLineGap;
  drawCenterLine(route, &y, displayFontBody(), route_fg, bg);
  drawCenterLine(type, &y, displayFontDetail(), label_fg, bg);
  drawCenterLine(alt, &y, displayFontDetail(), fg, bg);
  drawCenterLine(speed, &y, displayFontDetail(), fg, bg);

  y += kFooterGap;
  drawCenterLine(index_line, &y, displayFontDetail(), hint_fg, bg);
  drawCenterLine("Turn: next", &y, displayFontDetail(), hint_fg, bg);
  drawCenterLine("Swipe right", &y, displayFontDetail(), hint_fg, bg);

  tft.setTextDatum(TextDatum::TopLeft);
}

}  // namespace ui

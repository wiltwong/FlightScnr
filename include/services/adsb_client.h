#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  /** Airline + route (serial log only; from API waterfall). */
  char airline[28];
  char route_origin[5];
  char route_dest[5];
  char type[5];
  char alt[12];
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Start background fetch worker (call once after WiFi is available). */
void fetchInit();

/** Queue a non-blocking fetch; returns false if a fetch is already running. */
bool fetchRequest(double center_lat, double center_lon, float fetch_radius_km);

/** True when a queued fetch finished and data is ready to display. */
bool fetchReady();

/** Acknowledge fetchReady (call before reading aircraft for display). */
void fetchConsume();

bool fetchInProgress();

/** Blocking fetch (legacy); prefer fetchRequest on the main loop. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

void trafficFilterBootLoad();
int altitudeFloorFt();
void saveAltitudeFloorFromForm(const char* value);
void trafficFilterWipe();

}  // namespace services::adsb

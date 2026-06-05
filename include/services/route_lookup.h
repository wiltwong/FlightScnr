#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {
struct Aircraft;
}

namespace services::route {

/** Which API supplied the airline name (serial log tag). */
enum class ApiSource : uint8_t {
  kNone = 0,
  kCache,
  kAirLabs,
  kFlightAware,
  kFr24,
  kPrefix,  // callsign-prefix fallback when no API keys / all failed
};

void init();

/** Debounced persist of route cache to LittleFS (call from main loop). */
void tickCacheFlush(unsigned long now_ms);

/**
 * Waterfall lookup (AirLabs → FlightAware → FR24). Each callsign triggers live APIs at most once.
 * callsign must be ICAO radio format (e.g. UAL1234).
 */
bool lookupAirline(const char* callsign, char* out, size_t out_len, ApiSource* source_out);

/** Fill airline names for aircraft with flight callsigns (rate-limited API calls). */
void enrichAircraft(services::adsb::Aircraft* planes, size_t count, double center_lat,
                    double center_lon);

const char* sourceTag(ApiSource s);

}  // namespace services::route

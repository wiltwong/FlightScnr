#include "services/adsb_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "geo/flat_earth.h"
#include "services/route_lookup.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
void logAircraftToSerial(const Aircraft* planes, size_t count, double center_lat,
                         double center_lon) {
  Serial.printf("adsb: %u aircraft (center %.5f, %.5f)\n",
                static_cast<unsigned>(count), center_lat, center_lon);
  if (count == 0) {
    return;
  }

  uint8_t order[kMaxAircraft];
  float dist_km[kMaxAircraft];
  for (size_t i = 0; i < count; ++i) {
    order[i] = static_cast<uint8_t>(i);
    float dx = 0.0f;
    float dy = 0.0f;
    geo::localOffsetKm(center_lat, center_lon, planes[i].lat, planes[i].lon, &dx, &dy,
                       &dist_km[i]);
  }

  for (size_t i = 0; i + 1 < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      if (dist_km[order[j]] < dist_km[order[i]]) {
        const uint8_t tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  Serial.println(
      "  #  callsign airline              route    type alt       dist    brg  trk   gs");
  for (size_t row = 0; row < count; ++row) {
    const Aircraft& ac = planes[order[row]];
    float dx = 0.0f;
    float dy = 0.0f;
    float dist = 0.0f;
    geo::localOffsetKm(center_lat, center_lon, ac.lat, ac.lon, &dx, &dy, &dist);
    const int brg = geo::bearingFromOffset(dx, dy);
    const char* cs = ac.callsign[0] != '\0' ? ac.callsign : "—";
    const char* airline = ac.airline[0] != '\0' ? ac.airline : "—";
    const char* ty = ac.type[0] != '\0' ? ac.type : "—";
    const char* alt = ac.alt[0] != '\0' ? ac.alt : "—";
    char route[12];
    route[0] = '\0';
    if (ac.route_origin[0] != '\0' && ac.route_dest[0] != '\0') {
      snprintf(route, sizeof(route), "%s→%s", ac.route_origin, ac.route_dest);
    } else if (ac.route_origin[0] != '\0') {
      snprintf(route, sizeof(route), "%s→?", ac.route_origin);
    } else if (ac.route_dest[0] != '\0') {
      snprintf(route, sizeof(route), "?→%s", ac.route_dest);
    } else {
      strncpy(route, "—", sizeof(route) - 1);
      route[sizeof(route) - 1] = '\0';
    }
    Serial.printf(" %2u  %-7s %-22s %-8s %-4s %-9s %5.1f km %03d° %4.0f° %4.0f kt\n",
                  static_cast<unsigned>(row + 1), cs, airline, route, ty, alt, dist, brg,
                  ac.track_deg, ac.gs_knots);
  }
}

constexpr char kPrefsNamespace[] = "flightscnr";
constexpr char kPrefsAltFloorKey[] = "alt_floor_ft";
constexpr char kLegacyAltFloorKey[] = "minAltFt";

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
int s_altitude_floor_ft = config::kFactoryAltitudeFloorFt;

Aircraft s_aircraft_staging[kMaxAircraft];
size_t s_aircraft_staging_count = 0;

SemaphoreHandle_t s_aircraft_mutex = nullptr;
TaskHandle_t s_fetch_task = nullptr;

volatile bool s_fetch_requested = false;
volatile bool s_fetch_ready = false;
volatile bool s_fetch_busy = false;

double s_fetch_lat = 0.0;
double s_fetch_lon = 0.0;
float s_fetch_radius_km = 0.0f;

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

bool readAltitudeFt(const JsonObject& plane, float* out_ft) {
  if (isOnGround(plane)) {
    return false;
  }
  if (readJsonFloat(plane, "alt_baro", out_ft)) {
    return true;
  }
  return readJsonFloat(plane, "alt_geom", out_ft);
}

bool passesAltitudeFloor(const JsonObject& plane) {
  if (s_altitude_floor_ft <= 0) {
    return true;
  }
  float alt_ft = 0.0f;
  if (!readAltitudeFt(plane, &alt_ft)) {
    return false;
  }
  return alt_ft >= static_cast<float>(s_altitude_floor_ft);
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  while (*s == ' ') {
    ++s;
  }
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  char flight_id[sizeof(ac->callsign)];
  flight_id[0] = '\0';
  copyJsonStringTrimmed(plane, "flight", flight_id, sizeof(flight_id));

  const bool has_flight = flight_id[0] != '\0';
  if (has_flight) {
    strncpy(ac->callsign, flight_id, sizeof(ac->callsign) - 1);
    ac->callsign[sizeof(ac->callsign) - 1] = '\0';
  } else {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
  ac->airline[0] = '\0';
  ac->route_origin[0] = '\0';
  ac->route_dest[0] = '\0';
}

}  // namespace

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

void trafficFilterBootLoad() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    s_altitude_floor_ft = config::kFactoryAltitudeFloorFt;
    return;
  }
  if (prefs.isKey(kPrefsAltFloorKey)) {
    s_altitude_floor_ft = prefs.getInt(kPrefsAltFloorKey, config::kFactoryAltitudeFloorFt);
  } else {
    s_altitude_floor_ft =
        prefs.getInt(kLegacyAltFloorKey, config::kFactoryAltitudeFloorFt);
  }
  if (s_altitude_floor_ft < 0) {
    s_altitude_floor_ft = 0;
  }
  prefs.end();

  if (s_altitude_floor_ft > 0) {
    Serial.printf("Traffic altitude floor: %d ft\n", s_altitude_floor_ft);
  }
}

int altitudeFloorFt() { return s_altitude_floor_ft; }

void saveAltitudeFloorFromForm(const char* value) {
  int min_ft = 0;
  if (value != nullptr && value[0] != '\0') {
    min_ft = static_cast<int>(lroundf(strtof(value, nullptr)));
  }
  if (min_ft < 0) {
    min_ft = 0;
  }
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putInt(kPrefsAltFloorKey, min_ft);
    prefs.end();
  }
  s_altitude_floor_ft = min_ft;
  if (min_ft > 0) {
    Serial.printf("Traffic altitude floor: %d ft\n", min_ft);
  } else {
    Serial.println("Traffic altitude floor off");
  }
}

void trafficFilterWipe() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.remove(kPrefsAltFloorKey);
    prefs.remove(kLegacyAltFloorKey);
    prefs.end();
  }
  s_altitude_floor_ft = config::kFactoryAltitudeFloorFt;
}

bool parseAircraftPayload(const String& payload, Aircraft* out, size_t* out_count) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    *out_count = 0;
    return true;
  }

  size_t n = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kTrafficIncludeGround) {
      continue;
    }
    if (!passesAltitudeFloor(plane)) {
      continue;
    }

    out[n].lat = plane["lat"].as<float>();
    out[n].lon = plane["lon"].as<float>();
    out[n].nose_deg = pickNoseHeading(plane);
    out[n].track_deg = pickTrackHeading(plane);
    out[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&out[n], plane);
    ++n;
  }

  *out_count = n;
  return true;
}

bool fetchUpdateBlocking(double center_lat, double center_lon, float fetch_radius_km,
                       Aircraft* out, size_t* out_count) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.setTimeout(5000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  if (!parseAircraftPayload(payload, out, out_count)) {
    return false;
  }

  services::route::enrichAircraft(out, *out_count, center_lat, center_lon);
  logAircraftToSerial(out, *out_count, center_lat, center_lon);
  return true;
}

void fetchWorkerTask(void* /*arg*/) {
  for (;;) {
    if (s_fetch_requested) {
      s_fetch_busy = true;
      size_t n = 0;
      const bool ok = fetchUpdateBlocking(s_fetch_lat, s_fetch_lon, s_fetch_radius_km,
                                        s_aircraft_staging, &n);
      if (ok) {
        s_aircraft_staging_count = n;
        if (s_aircraft_mutex != nullptr) {
          xSemaphoreTake(s_aircraft_mutex, portMAX_DELAY);
        }
        memcpy(s_aircraft, s_aircraft_staging,
               n * sizeof(Aircraft));
        s_aircraft_count = n;
        if (s_aircraft_mutex != nullptr) {
          xSemaphoreGive(s_aircraft_mutex);
        }
        s_fetch_ready = true;
      }
      s_fetch_requested = false;
      s_fetch_busy = false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void fetchInit() {
  if (s_fetch_task != nullptr) {
    return;
  }
  if (s_aircraft_mutex == nullptr) {
    s_aircraft_mutex = xSemaphoreCreateMutex();
  }
  xTaskCreatePinnedToCore(fetchWorkerTask, "adsb_fetch", 16384, nullptr, 1, &s_fetch_task,
                          0);
}

bool fetchRequest(double center_lat, double center_lon, float fetch_radius_km) {
  if (s_fetch_task == nullptr) {
    fetchInit();
  }
  if (s_fetch_requested || s_fetch_busy) {
    return false;
  }
  s_fetch_lat = center_lat;
  s_fetch_lon = center_lon;
  s_fetch_radius_km = fetch_radius_km;
  s_fetch_requested = true;
  return true;
}

bool fetchReady() { return s_fetch_ready; }

void fetchConsume() { s_fetch_ready = false; }

bool fetchInProgress() { return s_fetch_requested || s_fetch_busy; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  size_t n = 0;
  if (!fetchUpdateBlocking(center_lat, center_lon, fetch_radius_km, s_aircraft, &n)) {
    return false;
  }
  s_aircraft_count = n;
  return true;
}

}  // namespace services::adsb

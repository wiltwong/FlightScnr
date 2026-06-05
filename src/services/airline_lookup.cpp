#include "services/airline_lookup.h"

#include <cctype>
#include <cstring>

namespace services::airline {

namespace {

struct OverrideEntry {
  const char* code;
  const char* name;
};

/** Regional / brand overrides (plane-tracker utilities/airlines.py). */
constexpr OverrideEntry kOverrides[] = {
    {"ENY", "American Eagle"},
    {"JIA", "American Eagle"},
    {"PDT", "American Eagle"},
    {"PSA", "American Eagle"},
    {"GJS", "United Express"},
    {"CPZ", "United Express"},
    {"ASH", "United Express"},
    {"G7", "United Express"},
    {"UCA", "United Express"},
    {"EDV", "Delta Connection"},
    {"ASQ", "Delta Connection"},
    {"RPA", "Republic Airways"},
    {"SKW", "SkyWest Airlines"},
    {"QXE", "Horizon Air"},
    {"TIV", "Thrive Aviation"},
    {"AAL", "American Airlines"},
    {"UAL", "United Airlines"},
    {"DAL", "Delta Air Lines"},
    {"SWA", "Southwest Airlines"},
    {"BAW", "British Airways"},
    {"DLH", "Lufthansa"},
    {"KLM", "KLM"},
    {"AFR", "Air France"},
    {"UAE", "Emirates"},
    {"QTR", "Qatar Airways"},
    {"ACA", "Air Canada"},
    {"RYR", "Ryanair"},
    {"EZS", "easyJet"},
    {"VOZ", "Virgin Australia"},
};

const char* overrideName(const char* code) {
  if (code == nullptr) {
    return nullptr;
  }
  for (const OverrideEntry& e : kOverrides) {
    if (strcmp(e.code, code) == 0) {
      return e.name;
    }
  }
  return nullptr;
}

void normalizeCallsign(const char* in, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return;
  }
  while (*in == ' ') {
    ++in;
  }
  size_t n = 0;
  while (in[n] != '\0' && n + 1 < out_len) {
    const unsigned char c = static_cast<unsigned char>(in[n]);
    out[n] = static_cast<char>(islower(c) ? toupper(c) : in[n]);
    ++n;
  }
  while (n > 0 && out[n - 1] == ' ') {
    --n;
  }
  out[n] = '\0';
}

bool isIcaoRadioCallsign(const char* cs) {
  if (cs == nullptr || strlen(cs) < 4) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (!isupper(static_cast<unsigned char>(cs[i]))) {
      return false;
    }
  }
  for (size_t i = 3; cs[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(cs[i]);
    if (!isupper(c) && !isdigit(c)) {
      return false;
    }
  }
  return true;
}

bool isIataRadioCallsign(const char* cs) {
  if (cs == nullptr || strlen(cs) < 3) {
    return false;
  }
  for (int i = 0; i < 2; ++i) {
    if (!isupper(static_cast<unsigned char>(cs[i]))) {
      return false;
    }
  }
  for (size_t i = 2; cs[i] != '\0'; ++i) {
    if (!isdigit(static_cast<unsigned char>(cs[i]))) {
      return false;
    }
  }
  return true;
}

bool isNNumber(const char* cs) {
  if (cs == nullptr || cs[0] != 'N') {
    return false;
  }
  for (size_t i = 1; cs[i] != '\0'; ++i) {
    if (!isdigit(static_cast<unsigned char>(cs[i])) &&
        !isupper(static_cast<unsigned char>(cs[i]))) {
      return false;
    }
  }
  return cs[1] != '\0';
}

}  // namespace

bool lookupByCode(const char* code, char* out, size_t out_len) {
  if (out_len > 0) {
    out[0] = '\0';
  }
  if (code == nullptr || code[0] == '\0') {
    return false;
  }
  const char* name = overrideName(code);
  if (name == nullptr) {
    return false;
  }
  strncpy(out, name, out_len - 1);
  out[out_len - 1] = '\0';
  return true;
}

void resolveFromCallsign(const char* callsign, bool has_flight_field, char* out,
                         size_t out_len) {
  if (out_len > 0) {
    out[0] = '\0';
  }
  if (!has_flight_field || callsign == nullptr || callsign[0] == '\0') {
    return;
  }

  char normalized[16];
  normalizeCallsign(callsign, normalized, sizeof(normalized));
  if (normalized[0] == '\0') {
    return;
  }

  if (isNNumber(normalized)) {
    strncpy(out, "Private", out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }

  char prefix[4];
  if (isIcaoRadioCallsign(normalized)) {
    memcpy(prefix, normalized, 3);
    prefix[3] = '\0';
    if (lookupByCode(prefix, out, out_len)) {
      return;
    }
  }

  if (isIataRadioCallsign(normalized)) {
    memcpy(prefix, normalized, 2);
    prefix[2] = '\0';
    lookupByCode(prefix, out, out_len);
  }
}

}  // namespace services::airline

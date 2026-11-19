#pragma once

// Runtime-settable SNTP server (the "NTP Server" text entity in the EHVClock YAMLs).
//
// ESPHome's sntp component sets its servers once, in setup(). On ESP8266 that is the SDK's
// lwIP SNTP client, so re-running sntp_stop() / sntp_setservername() / sntp_init() from a
// lambda swaps the server at runtime. The name goes into EVERY lwIP slot: after a timeout
// lwIP moves to the next slot and stays there while it answers, so a different server left
// in another slot could quietly take over. An empty name restores ESPHome's defaults.
//
// From YAML lambdas:  vfd::set_ntp_server(x);
// ESP8266 only (both EHVClock clocks); elsewhere it logs and returns false.

#include <cstring>
#include <string>

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#ifdef USE_ESP8266
#include "sntp.h"
#endif

namespace esphome {
namespace vfd {

static const char *const SNTP_TAG = "vfd.sntp";

/// Hostname or IPv4 address: [A-Za-z0-9.-], 1..63 chars, no leading/trailing '.' or '-'.
inline bool valid_ntp_server(const std::string &s) {
  if (s.empty() || s.size() > 63)
    return false;
  for (char c : s) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok)
      return false;
  }
  return s.front() != '.' && s.front() != '-' && s.back() != '.' && s.back() != '-';
}

/// Use `name` as the SNTP server ("" = ESPHome's defaults, 0-2.pool.ntp.org).
/// Returns false (and changes nothing) if the name is not a hostname or IPv4 address.
inline bool set_ntp_server(const std::string &name) {
#ifdef USE_ESP8266
  static const char *const DEFAULTS[] = {"0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"};
  static char server[64];  // lwIP keeps the pointer, not a copy
  if (!name.empty() && !valid_ntp_server(name)) {
    ESP_LOGW(SNTP_TAG, "Not a hostname or IPv4 address: '%s'", name.c_str());
    return false;
  }
  sntp_stop();  // before changing the buffer lwIP points at
  if (name.empty()) {
    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; i++)
      sntp_setservername(i, (char *) DEFAULTS[i % 3]);
    ESP_LOGI(SNTP_TAG, "NTP server: ESPHome defaults");
  } else {
    strncpy(server, name.c_str(), sizeof(server) - 1);
    server[sizeof(server) - 1] = '\0';
    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; i++)
      sntp_setservername(i, server);
    ESP_LOGI(SNTP_TAG, "NTP server: %s", server);
  }
  sntp_init();
  return true;
#else
  ESP_LOGW(SNTP_TAG, "Setting the NTP server is only supported on ESP8266");
  return false;
#endif
}

}  // namespace vfd
}  // namespace esphome

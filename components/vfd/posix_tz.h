#pragma once

// Runtime time zones from POSIX TZ strings (the time zone text entities in the EHVClock YAMLs).
//
// ESPHome 2026.9 removed RealTimeClock::set_timezone(const char *). Since 2026.4 time zones
// are parsed into a time::ParsedTimezone (at codegen, or by Home Assistant) and ONE global
// zone is used by every time component (time::set_global_tz). Nothing parses a string on the
// device any more, so parse_posix_tz() below is a C++ port of aioesphomeapi's
// posix_tz.parse_posix_tz(): same fields, same inputs rejected.
//
// From YAML lambdas:  vfd::set_timezone(id(main_timezone).state, id(my_time));
// ESPHome 2026.4 and newer: parses the string and sets the global zone (the clock argument is
// not needed, all clocks share it). Older ESPHome: calls the clock's own set_timezone().
// Lives in the vfd component so that the YAMLs get it with the component, no extra files.

#include <string>

#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/macros.h"
#include "esphome/core/version.h"

#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 4, 0) && defined(USE_TIME_TIMEZONE)
#include <cctype>
#include "esphome/components/time/posix_tz.h"
#define VFD_PARSE_POSIX_TZ
#endif

namespace esphome {
namespace vfd {

static const char *const TZ_TAG = "vfd.timezone";

#ifdef VFD_PARSE_POSIX_TZ
namespace tz_detail {

using time::DSTRule;
using time::DSTRuleType;

// Each returns false on a parse error, leaving pos where it stopped.
inline bool parse_uint(const std::string &s, size_t &pos, int32_t &out) {
  size_t start = pos;
  out = 0;
  while (pos < s.size() && isdigit((unsigned char) s[pos]))
    out = out * 10 + (s[pos++] - '0');
  return pos != start;
}

inline bool skip_name(const std::string &s, size_t &pos) {
  if (pos < s.size() && s[pos] == '<') {  // <+07>, <-03>
    while (++pos < s.size() && s[pos] != '>') {
    }
    if (pos >= s.size())
      return false;
    pos++;
    return true;
  }
  size_t start = pos;  // 3+ letters
  while (pos < s.size() && isalpha((unsigned char) s[pos]))
    pos++;
  return pos - start >= 3;
}

// [+-]hh[:mm[:ss]] in seconds
inline bool parse_offset(const std::string &s, size_t &pos, int32_t &out) {
  int32_t sign = 1, h, m = 0, sec = 0;
  if (pos < s.size() && (s[pos] == '-' || s[pos] == '+'))
    sign = s[pos++] == '-' ? -1 : 1;
  if (!parse_uint(s, pos, h))
    return false;
  if (pos < s.size() && s[pos] == ':') {
    pos++;
    if (!parse_uint(s, pos, m))
      return false;
    if (pos < s.size() && s[pos] == ':') {
      pos++;
      if (!parse_uint(s, pos, sec))
        return false;
    }
  }
  out = sign * (h * 3600 + m * 60 + sec);
  return true;
}

// Mm.w.d[/time], Jn[/time] or n[/time]
inline bool parse_rule(const std::string &s, size_t &pos, DSTRule &rule) {
  int32_t v;
  rule = DSTRule{};
  if (pos >= s.size())
    return false;
  char c = s[pos];
  if (c == 'M' || c == 'm') {
    rule.type = DSTRuleType::MONTH_WEEK_DAY;
    pos++;
    if (!parse_uint(s, pos, v) || v < 1 || v > 12)
      return false;
    rule.month = v;
    if (pos >= s.size() || s[pos++] != '.' || !parse_uint(s, pos, v) || v < 1 || v > 5)
      return false;
    rule.week = v;
    if (pos >= s.size() || s[pos++] != '.' || !parse_uint(s, pos, v) || v > 6)
      return false;
    rule.day_of_week = v;
  } else if (c == 'J' || c == 'j') {
    rule.type = DSTRuleType::JULIAN_NO_LEAP;
    pos++;
    if (!parse_uint(s, pos, v) || v < 1 || v > 365)
      return false;
    rule.day = v;
  } else if (isdigit((unsigned char) c)) {
    rule.type = DSTRuleType::DAY_OF_YEAR;
    if (!parse_uint(s, pos, v) || v > 365)
      return false;
    rule.day = v;
  } else {
    return false;
  }
  rule.time_seconds = 2 * 3600;  // default 02:00
  if (pos < s.size() && s[pos] == '/') {
    pos++;
    if (!parse_offset(s, pos, rule.time_seconds))
      return false;
  }
  return true;
}

}  // namespace tz_detail

/// Parse a POSIX TZ string. Same results as aioesphomeapi.posix_tz.parse_posix_tz();
/// returns false where that raises.
inline bool parse_posix_tz(const std::string &s, time::ParsedTimezone &tz) {
  using namespace tz_detail;
  tz = time::ParsedTimezone{};
  size_t pos = 0;
  if (s.empty() || !skip_name(s, pos))
    return false;
  if (pos >= s.size() || !(isdigit((unsigned char) s[pos]) || s[pos] == '+' || s[pos] == '-'))
    return false;
  if (!parse_offset(s, pos, tz.std_offset_seconds))
    return false;
  if (pos >= s.size())
    return true;  // no DST
  if (s[pos] == ',')
    return false;  // rules without a DST name
  if (!isalpha((unsigned char) s[pos]) && s[pos] != '<')
    return true;  // no DST, trailing characters ignored
  if (!skip_name(s, pos))
    return false;
  if (pos < s.size() && s[pos] != ',' && (isdigit((unsigned char) s[pos]) || s[pos] == '+' || s[pos] == '-')) {
    if (!parse_offset(s, pos, tz.dst_offset_seconds))
      return false;
  } else {
    tz.dst_offset_seconds = tz.std_offset_seconds - 3600;
  }
  if (pos >= s.size() || s[pos] != ',')
    return true;  // DST name without rules: no DST
  pos++;
  if (!parse_rule(s, pos, tz.dst_start))
    return false;
  if (pos >= s.size() || s[pos] != ',')
    return false;  // the second rule is required
  pos++;
  return parse_rule(s, pos, tz.dst_end);
}
#endif  // VFD_PARSE_POSIX_TZ

/// Set the time zone from a POSIX TZ string (e.g. "KST-9", "EST5EDT,M3.2.0,M11.1.0").
/// Returns false (and changes nothing) if the string is not a valid POSIX TZ.
template<typename Clock> bool set_timezone(const std::string &tz, Clock *clock) {
#ifdef VFD_PARSE_POSIX_TZ
  (void) clock;  // one global zone for every clock
  time::ParsedTimezone parsed;
  if (!parse_posix_tz(tz, parsed)) {
    ESP_LOGW(TZ_TAG, "Not a POSIX time zone: '%s'", tz.c_str());
    return false;
  }
  time::set_global_tz(parsed);
  return true;
#elif ESPHOME_VERSION_CODE < VERSION_CODE(2026, 4, 0)
  clock->set_timezone(tz.c_str());
  return true;
#else
  (void) clock;
  ESP_LOGW(TZ_TAG, "Time zone support (USE_TIME_TIMEZONE) is not enabled");
  return false;
#endif
}

}  // namespace vfd
}  // namespace esphome

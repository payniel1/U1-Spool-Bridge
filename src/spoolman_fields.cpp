#include "spoolman_fields.h"

#include <ArduinoJson.h>

#include <cctype>

std::string smJsonUnquote(const std::string &encoded) {
  if (encoded.empty()) return std::string();
  JsonDocument d;
  if (deserializeJson(d, encoded) == DeserializationError::Ok && d.is<const char *>()) {
    return std::string(d.as<const char *>());
  }
  return encoded;  // wasn't actually encoded — take it at face value
}

std::string smJsonQuote(const std::string &plain) {
  JsonDocument d;
  d.set(plain);
  std::string out;
  serializeJson(d, out);
  return out;
}

// ---------------------------------------------------------------------------
// card_uids list
// ---------------------------------------------------------------------------

static std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

static bool iequals(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return false;
  }
  return true;
}

template <typename F>
static void forEachToken(const std::string &list, F fn) {
  size_t start = 0;
  while (start <= list.size()) {
    size_t comma = list.find(',', start);
    if (comma == std::string::npos) comma = list.size();
    std::string tok = trim(list.substr(start, comma - start));
    if (!tok.empty()) fn(tok);
    if (comma == list.size()) break;
    start = comma + 1;
  }
}

bool smUidListContains(const std::string &list, const std::string &uid) {
  bool hit = false;
  forEachToken(list, [&](const std::string &tok) {
    if (iequals(tok, uid)) hit = true;
  });
  return hit;
}

std::string smUidListRemove(const std::string &list, const std::string &uid) {
  std::string out;
  forEachToken(list, [&](const std::string &tok) {
    if (iequals(tok, uid)) return;
    if (!out.empty()) out += ",";
    out += tok;
  });
  return out;
}

std::string smUidListAdd(const std::string &list, const std::string &uid) {
  if (smUidListContains(list, uid)) return list;
  // Rebuild rather than concatenate, so a list with stray whitespace or a
  // trailing comma comes back normalised.
  std::string out;
  forEachToken(list, [&](const std::string &tok) {
    if (!out.empty()) out += ",";
    out += tok;
  });
  if (!out.empty()) out += ",";
  out += uid;
  return out;
}

// ---------------------------------------------------------------------------
// location + comment
// ---------------------------------------------------------------------------

static void subst(std::string &f, const std::string &token, const std::string &val) {
  size_t pos = 0;
  while ((pos = f.find(token, pos)) != std::string::npos) {
    f.replace(pos, token.size(), val);
    pos += val.size();   // don't rescan what we just inserted
  }
}

// Spoolman groups spools by location, so putting the fleet's group name in the
// location is what makes the two views agree: the shelf you see in the box's
// web UI and the heading you see in Spoolman become the same string.
//
// {group} falls back to the box name when a box has no group set, rather than
// collapsing every ungrouped box into one blank location.
std::string smFormatLocation(const std::string &fmt, int slot, const std::string &group,
                             const std::string &box) {
  // Same fallback as a fresh box's default. Group only, no slot: Spoolman
  // groups its list by location, so putting the slot in the string gives every
  // box a location of its own and you get eight headings instead of two.
  std::string f = fmt.empty() ? std::string("{group}") : fmt;
  subst(f, "{slot}", std::to_string(slot));
  subst(f, "{group}", group.empty() ? box : group);
  subst(f, "{box}", box);

  // A format that was all tokens and no text can end up blank or padded.
  size_t a = f.find_first_not_of(" \t");
  if (a == std::string::npos) return std::string();
  size_t b = f.find_last_not_of(" \t");
  return f.substr(a, b - a + 1);
}

std::string smAppendComment(const std::string &existing, const std::string &line,
                            size_t maxLen) {
  std::string out = existing;
  if (!out.empty()) out += "\n";
  out += line;

  // Trim from the front on line boundaries so the trail stays readable.
  while (out.size() > maxLen) {
    size_t nl = out.find('\n');
    if (nl == std::string::npos) {
      out = out.substr(out.size() - maxLen);
      break;
    }
    out = out.substr(nl + 1);
  }
  return out;
}

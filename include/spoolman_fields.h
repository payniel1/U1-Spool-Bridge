// ---------------------------------------------------------------------------
// spoolman_fields.h — the fiddly string handling behind the Spoolman client,
// kept free of Arduino types so it can be unit-tested on a host.
//
// Two things here are easy to get subtly wrong and expensive to debug on a
// device: Spoolman JSON-encodes every extra-field value regardless of the
// field's declared type, and `card_uids` is a comma-separated list that has to
// stay compatible with what the U1's SpoolLink writes.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>

#include <string>

// "\"AABBCCDD\""  ->  AABBCCDD
std::string smJsonUnquote(const std::string &encoded);
// AABBCCDD  ->  "\"AABBCCDD\""
std::string smJsonQuote(const std::string &plain);

bool        smUidListContains(const std::string &list, const std::string &uid);
std::string smUidListAdd(const std::string &list, const std::string &uid);
std::string smUidListRemove(const std::string &list, const std::string &uid);

// "U1 slot {slot}" + slot 2 -> "U1 slot 2"
// fmt understands {slot}, {group} and {box}. {group} falls back to the box name
// when the box has no group of its own.
std::string smFormatLocation(const std::string &fmt, int slot, const std::string &group,
                             const std::string &box);

// Append one line to a spool comment, dropping whole lines off the top so the
// result stays under Spoolman's limit.
std::string smAppendComment(const std::string &existing, const std::string &line,
                            size_t maxLen);

#pragma once

#include <ArduinoJson.h>
#include "reading_view.h"

namespace reading_ui {
struct ParsedReadings {
  History history;
  bool available = false;
  int value = 0;
  int64_t date = 0;
  char direction[25]{};
};
enum class ParseResult { Ok, Empty, Invalid, NoMemory };
struct ParseDiagnostics {
  const char* stage = "not_started";
  const char* jsonError = "Ok";
  size_t entries = 0;
};

template <typename Input> class ObjectReader {
 public:
  explicit ObjectReader(Input& input) : input_(input) {}
  int read() {
    if (prefix_) { prefix_ = false; return '{'; }
    char value;
    return input_.readBytes(&value, 1) == 1 ? static_cast<unsigned char>(value) : -1;
  }
  size_t readBytes(char* buffer, size_t length) {
    if (!length) return 0;
    size_t used = 0;
    if (prefix_) { buffer[used++] = '{'; prefix_ = false; }
    return used + (used < length ? input_.readBytes(buffer + used, length - used) : 0);
  }
 private:
  Input& input_;
  bool prefix_ = true;
};

template <typename Input> int token(Input& input) {
  char value;
  // Reject unbounded padding while allowing formatted JSON.
  for (unsigned spaces = 0; spaces < 128; ++spaces) {
    if (input.readBytes(&value, 1) != 1) return -1;
    if (value != ' ' && value != '\r' && value != '\n' && value != '\t') return static_cast<unsigned char>(value);
  }
  return -1;
}

// Decode one filtered object at a time instead of keeping 145 JSON objects in
// RAM alongside BearSSL. Only the small numeric history survives each object.
template <typename Input> ParseResult parseEntries(Input& input, int64_t now, ParsedReadings& output,
                                                 ParseDiagnostics& diagnostic) {
  diagnostic = ParseDiagnostics{};
  diagnostic.stage = "allocation";
  DynamicJsonDocument document(384);
  if (!document.capacity()) return ParseResult::NoMemory;
  StaticJsonDocument<192> filter;
  filter["sgv"] = true;
  filter["date"] = true;
  filter["direction"] = true;
  diagnostic.stage = "array_start";
  if (token(input) != '[') return ParseResult::Invalid;
  int next = token(input);
  if (next == ']') { diagnostic.stage = "empty"; return ParseResult::Empty; }
  for (size_t count = 0; count < SOURCE_READINGS; ++count) {
    diagnostic.stage = "object_start";
    if (next != '{') return ParseResult::Invalid;
    ObjectReader<Input> object(input);
    const auto error = deserializeJson(document, object, DeserializationOption::Filter(filter));
    diagnostic.stage = "object_json";
    diagnostic.jsonError = error.c_str();
    if (error) return error == DeserializationError::NoMemory ? ParseResult::NoMemory : ParseResult::Invalid;
    if (!document.is<JsonObject>()) return ParseResult::Invalid;
    ++diagnostic.entries;
    const int value = document["sgv"] | 0;
    const int64_t date = document["date"].as<int64_t>();
    const bool valid = value >= 1 && value <= 1000 && date >= device_clock::EARLIEST_UNIX * 1000 &&
                       (!device_clock::plausible(now) || date <= (now + 300) * 1000);
    // Do not conceal an invalid newest reading. Older sensor error records
    // should leave gaps, rather than preventing all future updates.
    if (!valid && count == 0) { diagnostic.stage = "invalid_reading"; return ParseResult::Invalid; }
    if (valid) output.history.add(date, static_cast<uint16_t>(value));
    if (valid && (!output.available || date > output.date)) {
      output.available = true;
      output.value = value;
      output.date = date;
      const char* direction = document["direction"] | "NONE";
      strncpy(output.direction, direction, sizeof(output.direction) - 1);
      output.direction[sizeof(output.direction) - 1] = '\0';
    }
    diagnostic.stage = "object_separator";
    next = token(input);
    if (next == ']') { diagnostic.stage = "ok"; return ParseResult::Ok; }
    if (next != ',') return ParseResult::Invalid;
    next = token(input);
#ifdef ARDUINO
    yield();
#endif
  }
  diagnostic.stage = "entry_limit";
  return ParseResult::Invalid;
}
template <typename Input> ParseResult parseEntries(Input& input, int64_t now, ParsedReadings& output) {
  ParseDiagnostics diagnostic;
  return parseEntries(input, now, output, diagnostic);
}
}  // namespace reading_ui

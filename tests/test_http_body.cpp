#include "http_body.h"
#include "reading_json.h"
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <string>
using namespace nightscout_http;
struct Wire {
  std::string value;
  size_t at = 0, fragment, maxRead;
  explicit Wire(std::string body, size_t pieces = 512, size_t readLimit = 512)
      : value(body), fragment(pieces), maxRead(readLimit) {}
  size_t available() { return std::min(fragment, value.size() - at); }
  size_t readBytes(char* buffer, size_t length) {
    size_t size = std::min(length, std::min(available(), maxRead));
    value.copy(buffer, size, at); at += size; return size;
  }
};
const std::string JSON = "[{\"sgv\":120,\"date\":1790000000000,\"direction\":\"Flat\"}]";
std::string chunked(const std::string& json, size_t size) {
  std::string output;
  for (size_t i = 0; i < json.size(); i += size) {
    const auto length = std::min(size, json.size() - i);
    char hex[20]; snprintf(hex, sizeof(hex), "%zx", length);
    output += std::string(hex) + ";extension=yes\r\n" + json.substr(i, length) + "\r\n";
  }
  return output + "0\r\nX-Ignored: yes\r\n\r\n";
}
void testIdentityAndChunks() {
  for (size_t fragment : {size_t(1), size_t(3), size_t(512)}) {
    for (size_t readLimit : {size_t(1), size_t(512)}) {
    for (bool chunks : {false, true}) {
      for (int length : {-1, int(JSON.size())}) {
        Wire wire(chunks ? chunked(JSON, 7) : JSON, fragment, readLimit);
        BodyReader<Wire> body(wire, chunks, length);
        reading_ui::ParsedReadings parsed;
        reading_ui::ParseDiagnostics diagnostic;
        assert(reading_ui::parseEntries(body, 1790000000, parsed, diagnostic) == reading_ui::ParseResult::Ok);
        assert(body.finish());
        assert(body.error() == BodyError::None && body.firstByte() == '[' && body.bytes() == JSON.size());
        assert(parsed.available && parsed.value == 120 && diagnostic.entries == 1);
      }
    }
    }
  }
}
void testRejectIncompleteAndTrailing() {
  for (const auto& wireText : {std::string("4\r\n[]"), std::string("2\r\n[]\r\n"),
                              std::string("2\r\n[]\r\n0\r\n"), std::string("2\r\n[]\r\n0\r\nX: y\r\n")}) {
    Wire wire(wireText, 1); BodyReader<Wire> body(wire, true, -1);
    reading_ui::ParsedReadings parsed;
    assert(reading_ui::parseEntries(body, 1790000000, parsed) == reading_ui::ParseResult::Empty);
    assert(!body.finish() && body.error() == BodyError::Truncated);
  }
  Wire truncated(JSON); BodyReader<Wire> incomplete(truncated, false, JSON.size() + 3);
  reading_ui::ParsedReadings parsed;
  assert(reading_ui::parseEntries(incomplete, 1790000000, parsed) == reading_ui::ParseResult::Ok);
  assert(!incomplete.finish() && incomplete.error() == BodyError::Truncated);
  Wire trailing("[]{}"); BodyReader<Wire> extra(trailing, false, 4);
  assert(reading_ui::parseEntries(extra, 1790000000, parsed) == reading_ui::ParseResult::Empty);
  assert(!extra.finish() && extra.error() == BodyError::Trailing);
}
void testRejectBadFramingAndLimits() {
  for (const auto& text : {"g\r\n[]\r\n0\r\n\r\n", "2\n[]\r\n0\r\n\r\n", "2\r\n[]XX0\r\n\r\n"}) {
    Wire wire(text); BodyReader<Wire> body(wire, true, -1);
    char buffer[100]; body.readBytes(buffer, sizeof(buffer));
    assert(body.error() == BodyError::Chunk);
  }
  Wire large("FFFFFFFFFFFFFFFF\r\n"); BodyReader<Wire> huge(large, true, -1);
  assert(huge.read() == -1 && huge.error() == BodyError::Limit);
}
void testRegressionRawChunkedIsNotJson() {
  // This reproduces the old getStream() path feeding chunk sizes to ArduinoJson.
  Wire raw(chunked(JSON, 7)); reading_ui::ParsedReadings parsed;
  assert(reading_ui::parseEntries(raw, 1790000000, parsed) == reading_ui::ParseResult::Invalid);
  Wire wire(chunked(JSON, 7)); BodyReader<Wire> decoded(wire, true, -1);
  assert(reading_ui::parseEntries(decoded, 1790000000, parsed) == reading_ui::ParseResult::Ok);
  assert(decoded.finish());
}
void testParserDiagnostics() {
  for (const auto& text : {"[{}]", "[{", "null"}) {
    Wire wire(text); BodyReader<Wire> body(wire, false, -1);
    reading_ui::ParsedReadings parsed; reading_ui::ParseDiagnostics diagnostic;
    assert(reading_ui::parseEntries(body, 1790000000, parsed, diagnostic) == reading_ui::ParseResult::Invalid);
    if (std::string(text) == "[{}]") assert(std::string(diagnostic.stage) == "invalid_reading");
    if (std::string(text) == "[{") assert(std::string(diagnostic.jsonError) == "IncompleteInput");
    if (std::string(text) == "null") assert(std::string(diagnostic.stage) == "array_start");
  }
}
int main() {
  testIdentityAndChunks(); testRejectIncompleteAndTrailing(); testRejectBadFramingAndLimits();
  testRegressionRawChunkedIsNotJson(); testParserDiagnostics();
  puts("5 HTTP body test groups passed");
}

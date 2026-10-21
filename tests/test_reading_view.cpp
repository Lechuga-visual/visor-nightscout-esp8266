#include "reading_json.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <algorithm>

using namespace reading_ui;
constexpr int64_t NOW = 1790000000;
struct Input {
  std::string text;
  size_t at = 0;
  explicit Input(std::string value) : text(value) {}
  size_t readBytes(char* destination, size_t count) {
    const size_t size = std::min(count, text.size() - at);
    text.copy(destination, size, at); at += size; return size;
  }
};
std::string entry(int value, int64_t date, const std::string& extra = "") {
  return "{\"sgv\":" + std::to_string(value) + ",\"date\":" + std::to_string(date) +
         ",\"direction\":\"FortyFiveUp\"" + extra + "}";
}
void testAgeAndStaleness() {
  assert(ageSeconds(false, NOW * 1000, NOW) == -1);
  assert(ageSeconds(true, NOW * 1000, 0) == -1);
  assert(ageSeconds(true, (NOW + 30) * 1000, NOW) == 0);
  assert(tone(true, 120, 599, true) == Tone::Normal);
  assert(tone(true, 120, 600, true) == Tone::Old);
  assert(tone(true, 60, 300, true) == Tone::Low);
  assert(tone(true, 200, 300, true) == Tone::High);
  assert(tone(true, 120, 300, false) == Tone::Error);
  assert(tone(true, 120, -1, true) == Tone::Clock);
}
void testBoundedSortedDeduplicatedHistory() {
  History history;
  for (int i = 40; i >= 0; --i) history.add((NOW - i * 300) * 1000, 100 + i);
  assert(history.count == 25);
  assert(history.points[0].date == (NOW - 24 * 300) * 1000);
  history.add(NOW * 1000, 200);
  assert(history.count == 25 && history.points[24].value == 200);
  history.add((NOW - 40 * 300) * 1000, 55);
  assert(history.count == 25 && history.points[0].value == 124);
}
void testGraphWindowGapsAndScale() {
  History history;
  history.add((NOW - 8000) * 1000, 900); // Outside the two-hour window.
  history.add((NOW - 7200) * 1000, 40);
  history.add((NOW - 6900) * 1000, 70);
  history.add(NOW * 1000, 300);
  const auto graph = plot(history, NOW);
  assert(graph.count == 3 && graph.points[0].x == 0 && graph.points[2].x == 1000);
  assert(graph.points[0].start && !graph.points[1].start && graph.points[2].start);
  assert(graph.low <= 30 && graph.high >= 310 && graph.high < 900);
  for (size_t i = 0; i < graph.count; ++i) assert(graph.points[i].x <= 1000 && graph.points[i].y <= 1000);
  assert(plot(history, 0).count == 0);
}
void testStreamsFullHistoryWithoutRetainingLargeFields() {
  std::string json = "[\n";
  for (int i = 0; i < 145; ++i) {
    if (i) json += ",\n";
    json += entry(120 + i, (NOW - i * 60) * 1000,
                  ",\"unused\":{\"large\":\"" + std::string(800, 'x') + "\",\"nested\":[1,2,3]}");
  }
  json += "\n]";
  Input input(json); ParsedReadings result;
  assert(parseEntries(input, NOW, result) == ParseResult::Ok);
  assert(result.available && result.history.count == 25 && result.value == 120);
  assert(std::string(result.direction) == "FortyFiveUp");
  assert(result.history.points[0].date <= (NOW - 115 * 60) * 1000);
}
void testEmptyAndInvalidResponses() {
  for (const char* json : {"[]", "[ \r\n ]"}) {
    Input input(json); ParsedReadings result;
    assert(parseEntries(input, NOW, result) == ParseResult::Empty);
  }
  for (const char* json : {"null", "{}", "[null]", "[{}]", "[", "[{\"sgv\":120}]", "[1]"}) {
    Input input(json); ParsedReadings result;
    assert(parseEntries(input, NOW, result) == ParseResult::Invalid);
  }
  Input future("[" + entry(120, (NOW + 301) * 1000) + "]"); ParsedReadings result;
  assert(parseEntries(future, NOW, result) == ParseResult::Invalid);
  Input olderError("[" + entry(120,NOW*1000) + "," + entry(0,(NOW-300)*1000) + "," + entry(110,(NOW-600)*1000) + "]");
  ParsedReadings withGap;
  assert(parseEntries(olderError,NOW,withGap) == ParseResult::Ok && withGap.value == 120 && withGap.history.count == 2);
}
void testNewestEntryAndUnknownTrend() {
  Input input("[" + entry(80, (NOW - 300) * 1000) + "," + entry(90, NOW * 1000) + "]");
  ParsedReadings result;
  assert(parseEntries(input, NOW, result) == ParseResult::Ok && result.value == 90);
  assert(trend("DoubleDown").doubled && trend("DoubleDown").dy == 1);
  assert(trend("unknown").dx == 0 && trend("unknown").dy == 0);
}
int main() {
  testAgeAndStaleness(); testBoundedSortedDeduplicatedHistory(); testGraphWindowGapsAndScale();
  testStreamsFullHistoryWithoutRetainingLargeFields(); testEmptyAndInvalidResponses(); testNewestEntryAndUnknownTrend();
  puts("6 reading view test groups passed");
}

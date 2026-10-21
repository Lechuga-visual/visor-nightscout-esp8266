#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "device_clock.h"

namespace reading_ui {
constexpr size_t HISTORY_SIZE = 25;
constexpr size_t SOURCE_READINGS = 145; // Covers two hours even with one-minute uploads.
constexpr int64_t WINDOW_MS = 2 * 60 * 60 * 1000;
constexpr int64_t GAP_MS = 15 * 60 * 1000;
struct Point { int64_t date; uint16_t value; };
struct History {
  Point points[HISTORY_SIZE]{};
  size_t count = 0;
  void add(int64_t date, uint16_t value) {
    // Keep the most recent sample in each five-minute bucket. This bounds
    // graph memory for sensors that publish more often than every five minutes.
    for (size_t i = 0; i < count; ++i) {
      if (points[i].date / 300000 == date / 300000) {
        if (date >= points[i].date) points[i] = {date, value};
        return;
      }
    }
    size_t at = 0;
    while (at < count && points[at].date < date) ++at;
    if (at < count && points[at].date == date) { points[at].value = value; return; }
    if (count == HISTORY_SIZE) {
      if (!at) return;
      for (size_t i = 1; i < count; ++i) points[i - 1] = points[i];
      --count; --at;
    }
    for (size_t i = count; i > at; --i) points[i] = points[i - 1];
    points[at] = {date, value};
    ++count;
  }
};
static_assert(sizeof(History) <= 512, "History must remain bounded on ESP8266");

enum class Tone { Empty, Clock, Old, Error, Low, High, Normal };
inline int64_t ageSeconds(bool available, int64_t dateMs, int64_t now) {
  if (!available || !device_clock::plausible(now)) return -1;
  const int64_t age = now - dateMs / 1000;
  return age < 0 ? 0 : age;
}
inline Tone tone(bool available, int value, int64_t age, bool connected) {
  if (!available) return Tone::Empty;
  if (age < 0) return Tone::Clock;
  if (age >= 600) return Tone::Old;
  if (!connected) return Tone::Error;
  return value < 70 ? Tone::Low : value > 180 ? Tone::High : Tone::Normal;
}
inline const char* toneName(Tone value) {
  switch (value) {
    case Tone::Low: return "low";
    case Tone::High: return "high";
    case Tone::Normal: return "normal";
    case Tone::Old: return "old";
    case Tone::Clock: return "clock";
    case Tone::Error: return "error";
    default: return "empty";
  }
}
struct Trend { int dx; int dy; bool doubled; const char* symbol; const char* label; };
inline Trend trend(const char* direction) {
  if (!strcmp(direction, "Flat")) return {1, 0, false, "→", "Estable"};
  if (!strcmp(direction, "SingleUp")) return {0, -1, false, "↑", "Subiendo"};
  if (!strcmp(direction, "DoubleUp")) return {0, -1, true, "⇈", "Subiendo rápido"};
  if (!strcmp(direction, "SingleDown")) return {0, 1, false, "↓", "Bajando"};
  if (!strcmp(direction, "DoubleDown")) return {0, 1, true, "⇊", "Bajando rápido"};
  if (!strcmp(direction, "FortyFiveUp")) return {1, -1, false, "↗", "Subiendo"};
  if (!strcmp(direction, "FortyFiveDown")) return {1, 1, false, "↘", "Bajando"};
  return {0, 0, false, "—", "Sin tendencia"};
}

struct PlotPoint { uint16_t x; uint16_t y; bool start; };
struct Plot { PlotPoint points[HISTORY_SIZE]{}; size_t count = 0; int low = 60; int high = 200; };
inline Plot plot(const History& history, int64_t now) {
  Plot result;
  if (!device_clock::plausible(now)) return result;
  const int64_t end = now * 1000;
  const int64_t begin = end - WINDOW_MS;
  for (size_t i = 0; i < history.count; ++i) {
    const auto& point = history.points[i];
    if (point.date < begin || point.date > end + 300000) continue;
    if (point.value < result.low + 10) result.low = point.value > 10 ? (point.value - 10) / 10 * 10 : 0;
    if (point.value > result.high - 10) result.high = (point.value + 19) / 10 * 10;
  }
  int64_t previous = 0;
  for (size_t i = 0; i < history.count; ++i) {
    const auto& point = history.points[i];
    if (point.date < begin || point.date > end + 300000) continue;
    const auto x = point.date > end ? 1000 : (point.date - begin) * 1000 / WINDOW_MS;
    const auto y = static_cast<int64_t>(result.high - point.value) * 1000 / (result.high - result.low);
    result.points[result.count++] = {static_cast<uint16_t>(x), static_cast<uint16_t>(y), !previous || point.date - previous > GAP_MS};
    previous = point.date;
  }
  return result;
}
}  // namespace reading_ui

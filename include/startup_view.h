#pragma once

namespace startup_view {
enum class Stage { Pending, Active, Done, Error };
struct Progress {
  bool finished = false;
  int responseCode = 0;
  bool parseFailed = false;

  void beginRequest() { responseCode = 0; parseFailed = false; }
  void response(int code) { responseCode = code; }
  void parsed(bool ok, bool available) {
    parseFailed = !ok;
    if (responseCode == 200 && ok && available) finished = true;
  }
  Stage wifi(bool connected) const { return connected ? Stage::Done : Stage::Active; }
  Stage server(bool connected) const {
    if (!connected) return Stage::Pending;
    return responseCode == 200 ? Stage::Done : responseCode ? Stage::Error : Stage::Active;
  }
  Stage reading(bool connected) const {
    if (!connected || responseCode != 200) return Stage::Pending;
    return parseFailed ? Stage::Error : Stage::Active;
  }
};
}  // namespace startup_view

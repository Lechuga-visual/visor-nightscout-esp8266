#include <cassert>
#include <initializer_list>
#include "startup_view.h"
int main() {
  using namespace startup_view;
  Progress progress;
  assert(!progress.finished);
  assert(progress.wifi(false) == Stage::Active);
  assert(progress.server(false) == Stage::Pending);
  assert(progress.reading(false) == Stage::Pending);
  assert(progress.wifi(true) == Stage::Done);
  assert(progress.server(true) == Stage::Active);
  for (int status : {-1, 401, 403, 500}) {
    progress.beginRequest(); progress.response(status);
    assert(progress.server(true) == Stage::Error);
    assert(progress.reading(true) == Stage::Pending);
    assert(!progress.finished);
  }
  progress.parsed(true, true);
  assert(!progress.finished); // A non-200 response cannot finish startup.
  progress.beginRequest(); progress.response(200);
  assert(progress.server(true) == Stage::Done);
  assert(progress.reading(true) == Stage::Active);
  progress.parsed(false, false);
  assert(progress.reading(true) == Stage::Error);
  assert(!progress.finished);
  progress.beginRequest(); progress.response(200); progress.parsed(true, false);
  assert(!progress.finished); // A valid empty response is not a glucose reading.
  assert(progress.reading(true) == Stage::Active);
  progress.parsed(true, true);
  assert(progress.finished);
  progress.beginRequest(); progress.response(500); progress.parsed(false, false);
  assert(progress.finished); // Later outages keep the live view and its old reading.
  assert(progress.server(false) == Stage::Pending);
}

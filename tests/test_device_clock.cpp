#include "device_clock.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

using namespace device_clock;

void testWaitsForWifiAndStartsOnConnection() {
  SyncSchedule schedule;
  assert(!schedule.poll(false, false, 0));
  assert(!schedule.poll(false, false, 60000));
  assert(schedule.poll(true, false, 60001));
  assert(!schedule.poll(true, false, 60002));
}

void testRetriesWithoutBusyLoopAndStopsWhenClockReady() {
  SyncSchedule schedule;
  assert(schedule.poll(true, false, 100));
  assert(!schedule.poll(true, false, 100 + RETRY_MS - 1));
  assert(schedule.poll(true, false, 100 + RETRY_MS));
  assert(!schedule.poll(true, true, 100 + 2 * RETRY_MS));
  assert(!schedule.poll(true, true, 100 + 10 * RETRY_MS));
}

void testReconnectAndManualRetryRestartSchedule() {
  SyncSchedule schedule;
  assert(schedule.poll(true, false, 0));
  schedule.restarted(25000);
  assert(!schedule.poll(true, false, 30000));
  assert(schedule.poll(true, false, 55000));
  assert(!schedule.poll(false, true, 60000));
  assert(schedule.poll(true, true, 60001));
}

void testMillisWraparound() {
  SyncSchedule schedule;
  const uint32_t start = UINT32_MAX - 1000;
  assert(schedule.poll(true, false, start));
  assert(!schedule.poll(true, false, 1000));
  assert(schedule.poll(true, false, start + RETRY_MS));
}

void testBrowserEpochValidation() {
  int64_t epoch = -1;
  assert(parseBrowserTime("1790000000", epoch) && epoch == 1790000000);
  const char* invalid[] = {nullptr, "", "0", "1690000000", "1790000000000", "-1790000000", "+1790000000",
                           "1790000000junk", " 1790000000", "1790000000\n", "9999999999"};
  for (const char* value : invalid) {
    epoch = -1;
    assert(!parseBrowserTime(value, epoch));
    assert(epoch == -1);
  }
  assert(!plausible(EARLIEST_UNIX - 1));
  assert(!plausible(LATEST_UNIX + 1));
}

int main() {
  testWaitsForWifiAndStartsOnConnection();
  testRetriesWithoutBusyLoopAndStopsWhenClockReady();
  testReconnectAndManualRetryRestartSchedule();
  testMillisWraparound();
  testBrowserEpochValidation();
  puts("5 clock test groups passed");
}

// Suite peer registry test: publish, discover, remote write, staleness.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "peer.hpp"

#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL  %s\n", what);
    ++failures;
  } else {
    std::printf("PASS  %s\n", what);
  }
}
} // namespace

int main() {
  {
    suite::peer::Client a;
    check(a.attach(), "first client attaches");
    const int slot = a.registerSlot(3, "test-channel", 8);
    check(slot >= 0, "registers a slot");

    a.publish(0, 0.25);
    a.publish(1, 0.75);
    a.beat();

    suite::peer::Client b;
    suite::peer::Segment* seg = b.segment();
    check(seg != nullptr, "second client attaches to the same segment");
    int found = -1;
    for (int i = 0; i < suite::peer::kSlots; ++i) {
      if (seg->slot[i].used.load(std::memory_order_acquire) &&
          std::strcmp(seg->slot[i].name, "test-channel") == 0) {
        found = i;
      }
    }
    check(found >= 0, "consumer can find the slot by name");
    check(found == slot, "slot index matches the registration");
    if (found < 0) {
      std::printf("%d failure(s)\n", failures);
      return 1;
    }
    check(seg->slot[found].bus.load() == 3u, "bus is published");
    check(suite::peer::Client::alive(seg->slot[found]), "slot reads as alive");
    check(seg->slot[found].param[0].value.load() == 0.25, "param 0 mirrored");
    check(seg->slot[found].param[1].value.load() == 0.75, "param 1 mirrored");

    // Remote write from the consumer side.
    suite::peer::Slot& s = seg->slot[found];
    suite::peer::Client::writeRemote(s, 2, 1.0);
    double v = 0.0;
    check(a.takeRemote(2, &v), "publisher sees the remote write");
    check(v == 1.0, "remote value is correct");

    // Our own publish is visible to the consumer side unchanged.  The caller
    // ignores it when the value already matches its own state.
    a.publish(3, 0.5);
    double echo = 0.0;
    check(a.takeRemote(3, &echo), "own publish is readable back");
    check(echo == 0.5, "own publish keeps its value");

    // Capture-style bus reassignment.
    suite::peer::Client::writeRemote(s, 7, 5.0);
    check(a.takeRemote(7, &v) && v == 5.0, "bus can be reassigned remotely");

    a.unregisterSlot();
    check(!suite::peer::Client::alive(s), "slot reads as dead after unregister");
  }
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

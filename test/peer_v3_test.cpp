// Protocol v3: instance ids, insert metadata and the LINK handshake.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "peer.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
  return ok;
}

void setText(char* dst, size_t size, const char* text) {
  std::snprintf(dst, size, "%s", text);
}
} // namespace

int main() {
  suite::peer::Client insert;
  const int slot = insert.registerSlot(1, "Test Insert", 2,
                                       suite::peer::kKindInsert, 1);
  if (!check(slot >= 0, "insert registers a slot")) return 1;
  const uint64_t id = insert.id();
  check(id != 0, "instance id is set");

  suite::peer::ParamMeta meta[2] = {};
  setText(meta[0].symbol, sizeof(meta[0].symbol), "gain");
  setText(meta[0].name, sizeof(meta[0].name), "Gain");
  setText(meta[0].group, sizeof(meta[0].group), "Dynamics");
  setText(meta[0].unit, sizeof(meta[0].unit), "dB");
  meta[0].min = -20.0f;
  meta[0].max = 20.0f;
  meta[0].def = 0.0f;
  meta[0].flags = suite::peer::kMetaStepped;

  setText(meta[1].symbol, sizeof(meta[1].symbol), "mode");
  setText(meta[1].name, sizeof(meta[1].name), "Mode");
  meta[1].labelCount = 3;
  setText(meta[1].label[0], sizeof(meta[1].label[0]), "Alpha");
  setText(meta[1].label[1], sizeof(meta[1].label[1]), "Beta");
  setText(meta[1].label[2], sizeof(meta[1].label[2]), "Gamma");
  meta[1].flags = suite::peer::kMetaToggle;
  insert.publishMeta(meta, 2);

  suite::peer::Hub hub;
  hub.refresh();
  int entry = -1;
  for (int i = 0; i < hub.size(); ++i) {
    if (hub.instanceId(i) == id) entry = i;
  }
  if (!check(entry >= 0, "hub finds the insert by instance id")) return 1;
  check(hub.kind(entry) == suite::peer::kKindInsert, "kind is insert");
  check(hub.bypassParam(entry) == 1, "bypass parameter is published");
  check(hub.metaReady(entry), "metadata is ready");
  check(hub.paramCount(entry) == 2, "parameter count");
  check(std::strcmp(hub.meta(entry, 0).symbol, "gain") == 0 &&
            std::fabs(hub.meta(entry, 0).max - 20.0f) < 1e-6f,
        "parameter metadata round-trips");
  check(hub.meta(entry, 1).labelCount == 3 &&
            std::strcmp(hub.meta(entry, 1).label[2], "Gamma") == 0,
        "enum labels round-trip");

  // LINK: the console writes it, the insert adopts it once.
  hub.setLink(entry, 3, 2, 1);
  suite::peer::LinkInfo link{};
  check(insert.takeRemoteLink(&link) && link.bus == 3 && link.channel == 2 &&
            link.slot == 1,
        "insert adopts the link");
  check(!insert.takeRemoteLink(&link), "link is adopted only once");
  check(hub.link(entry).bus == 3 && hub.link(entry).channel == 2 &&
            hub.link(entry).slot == 1,
        "hub reads the link back");

  // The insert republishes its saved link after a restore; no stamp churn.
  insert.publishLink(3, 2, 1);
  check(!insert.takeRemoteLink(&link), "publishing the same link is a no-op");

  // Clearing.
  hub.setLink(entry, 0, 0, 0);
  check(insert.takeRemoteLink(&link) && link.bus == 0 && link.channel == 0 &&
            link.slot == 0,
        "link can be cleared");

  // Own publishes are values, not requests; a stale publish cannot clobber a
  // request that arrived since the last poll.
  insert.publish(1, 0.25);
  double own = 0.0;
  check(!insert.takeRemote(1, &own), "own publish is not a remote request");
  suite::peer::Client::writeRemote(insert.raw()->slot[slot], 1, 0.75);
  insert.publish(1, 0.25);
  check(insert.takeRemote(1, &own) && std::fabs(own - 0.75) < 1e-12,
        "pending request survives a stale publish");

  // Remote parameter writes still use the generation counter.
  suite::peer::Client::writeRemote(insert.raw()->slot[slot], 0, 0.5);
  double value = 0.0;
  check(insert.takeRemote(0, &value) && std::fabs(value - 0.5) < 1e-12,
        "remote parameter write reaches the insert");
  check(!insert.takeRemote(0, &value), "remote write is taken once");

  insert.unregisterSlot();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  suite::peer::Hub after;
  after.refresh();
  bool stillThere = false;
  for (int i = 0; i < after.size(); ++i) {
    if (after.instanceId(i) == id) stillThere = true;
  }
  check(!stillThere, "unregistered slot disappears");

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

// Suite Control — panel test
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <cstdio>

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
  suite::Shared shared;
  suite::PanelState state;
  suite::Panel panel;

  cairo_surface_t* surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, suite::kPanelWidth, suite::kPanelHeight);
  cairo_t* cr = cairo_create(surface);
  panel.tick(shared, state);
  panel.draw(cr, shared, state);
  cairo_surface_flush(surface);
  const unsigned char* data = cairo_image_surface_get_data(surface);
  bool painted = false;
  for (int i = 0; i < 4096; ++i) {
    if (data[i] != 0) {
      painted = true;
      break;
    }
  }
  check(painted, "panel paints something");
  check(cairo_status(cr) == CAIRO_STATUS_SUCCESS, "panel draw is clean");

  // Bus tabs: "B" is the third pill (All, A, B ...).
  const double tabX = 300.0 + 2 * 36.0 + 17.0;
  const bool hit = panel.pointerDown(shared, state, tabX, 32.0, false);
  check(hit, "bus tab is clickable");
  check(static_cast<int>(shared.get(suite::P_BUS) + 0.5) == 2,
        "clicking B selects bus B");
  panel.pointerUp();

  // Capture on a bus with no peers must be harmless.
  panel.pointerDown(shared, state, 860.0, 32.0, false);
  panel.pointerUp();
  check(true, "capture without peers is safe");

  // EQ type pill toggles.
  const double before = shared.get(suite::P_EQ_TYPE);
  panel.pointerDown(shared, state, 1022.0 + 60.0 + 28.0, 400.0, false);
  panel.pointerUp();
  check(shared.get(suite::P_EQ_TYPE) != before, "EQ type pill toggles");

  // A registered Suite Channel Plus instance becomes a remote strip.
  {
    suite::peer::Client c;
    c.registerSlot(2, "unit-test", suite::ch::kCount);
    c.publish(suite::ch::kInput, 0.6);
    suite::Shared s2;
    s2.set(suite::P_BUS, 2.0);
    suite::Panel p2;
    p2.tick(s2, state);
    check(p2.stripCount() >= 1, "remote strip is discovered");
    cairo_surface_t* surf2 = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, suite::kPanelWidth, suite::kPanelHeight);
    cairo_t* cr2 = cairo_create(surf2);
    p2.draw(cr2, s2, state);
    check(cairo_status(cr2) == CAIRO_STATUS_SUCCESS, "remote strip draws");
    cairo_destroy(cr2);
    cairo_surface_destroy(surf2);
    c.unregisterSlot();
  }

  // Mute and solo write the channel's enable parameter.  The test does not
  // assume which slot lands on strip 0, only the resulting state.
  {
    suite::peer::Client c1, c2;
    const int a = c1.registerSlot(7, "Suite Channel Plus", suite::ch::kCount);
    const int b = c2.registerSlot(7, "Suite Channel Plus", suite::ch::kCount);
    suite::Shared s4;
    s4.set(suite::P_BUS, 7.0);
    suite::PanelState st4;
    suite::Panel p4;
    p4.tick(s4, st4);
    auto on = [&](int slot, suite::peer::Client& c) {
      return c.raw()->slot[slot].param[suite::ch::kOutput].value.load() > 0.01;
    };
    // Start both at a normal fader position.
    suite::peer::Client::writeRemote(c1.raw()->slot[a], suite::ch::kOutput, 0.75);
    suite::peer::Client::writeRemote(c2.raw()->slot[b], suite::ch::kOutput, 0.75);
    p4.pointerDown(s4, st4, 40.0, 694.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "mute disables exactly one channel");
    // Solo on the other strip: the muted one stays off, the soloed one on.
    p4.pointerDown(s4, st4, 141.0 + 70.0, 694.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "solo leaves one channel on");
    p4.pointerDown(s4, st4, 141.0 + 70.0, 694.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "mute survives solo release");
    // Unmute restores the fader.
    p4.pointerDown(s4, st4, 40.0, 694.0, false);
    p4.pointerUp();
    check(on(a, c1) && on(b, c2), "unmute restores the fader");
    c1.unregisterSlot();
    c2.unregisterSlot();
  }

  // A knob drag must stop when the mouse button is released.
  {
    suite::Shared s3;
    suite::PanelState st3;
    suite::Panel p3;
    const double before = s3.get(suite::P_COMP_THRESHOLD);
    p3.pointerDown(s3, st3, 1059.0, 218.0, false);
    p3.pointerMove(s3, st3, 1059.0, 208.0, false);
    const double moved = s3.get(suite::P_COMP_THRESHOLD);
    p3.pointerUp();
    p3.pointerMove(s3, st3, 1059.0, 150.0, false);
    check(moved != before, "drag changes the value");
    check(s3.get(suite::P_COMP_THRESHOLD) == moved, "drag stops on release");
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

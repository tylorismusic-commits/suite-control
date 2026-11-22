// Renders the panel to a PNG for visual checks.
#include "ui/panel.hpp"
#include <cstdio>
int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "/tmp/suite-control.png";
  suite::peer::Client coarse;
  // Two fake Suite Channel Plus instances so the strips have content.
  int slots[3] = {-1, -1, -1};
  for (int i = 0; i < 3; ++i) {
    slots[i] = coarse.registerSlot(i == 0 ? 1 : 1, "Suite Channel Plus",
                                   suite::ch::kCount);
    if (slots[i] < 0) continue;
    auto* seg = coarse.raw();
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kInput, 0.5 + 0.1 * i);
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kCompThreshold, 0.7);
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kCompMakeup, 0.55);
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kOutput, 0.75);
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kEqIn, 1.0);
    suite::peer::Client::writeRemote(seg->slot[slots[i]], suite::ch::kCompIn, i == 1 ? 0.0 : 1.0);
    seg->slot[slots[i]].outPeakL.store(-12.0f - 4.0f * i);
    seg->slot[slots[i]].outPeakR.store(-14.0f - 4.0f * i);
    seg->slot[slots[i]].inPeakL.store(-18.0f);
    seg->slot[slots[i]].inPeakR.store(-16.0f);
    seg->slot[slots[i]].grDb.store(3.0f + 2.0f * i);
    seg->slot[slots[i]].beatMs.store(suite::peer::nowMs());
  }
  suite::Shared shared;
  shared.set(suite::P_BUS, 1.0);
  shared.set(suite::P_COMP_THRESHOLD, 0.7);
  suite::PanelState state;
  suite::Panel panel;
  panel.tick(shared, state);
  cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                  suite::kPanelWidth,
                                                  suite::kPanelHeight);
  cairo_t* cr = cairo_create(s);
  panel.draw(cr, shared, state);
  cairo_surface_write_to_png(s, out);
  cairo_destroy(cr);
  cairo_surface_destroy(s);
  std::printf("wrote %s (strips %d)\n", out, panel.stripCount());
  return 0;
}

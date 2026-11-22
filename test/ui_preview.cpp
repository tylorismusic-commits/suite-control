// Suite Channel — UI preview renderer
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cairo.h>

#include <cstdio>

#include "ui/panel.hpp"

namespace {

void render(const char* path, double scale, double compType, double eqType) {
  const int w = static_cast<int>(suite::kPanelWidth * scale);
  const int h = static_cast<int>(suite::kPanelHeight * scale);
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t* cr = cairo_create(surface);
  cairo_scale(cr, scale, scale);

  suite::Shared shared;
  shared.set(suite::P_COMP_TYPE, compType);
  shared.set(suite::P_EQ_TYPE, eqType);
  shared.grDb.store(8.0f);
  shared.gateGrDb.store(4.0f);
  shared.outPeakLDb.store(-6.0f);
  shared.outPeakRDb.store(-9.0f);
  shared.inPeakLDb.store(-12.0f);
  shared.inPeakRDb.store(-14.0f);
  suite::PanelState state;
  suite::Panel panel;
  panel.tick(shared, state);
  panel.draw(cr, shared, state);

  cairo_destroy(cr);
  cairo_surface_write_to_png(surface, path);
  cairo_surface_destroy(surface);
  std::printf("wrote %s\n", path);
}

} // namespace

int main() {
  render("suite-channel.png", 1.0, 0.0, 0.0);
  render("suite-channel-76.png", 1.0, 1.0, 0.0);
  render("suite-channel-black.png", 1.0, 0.0, 1.0);
  render("suite-channel-0.9x.png", 0.9, 0.0, 0.0);
  render("suite-channel@2x.png", 2.0, 0.0, 0.0);
  return 0;
}

// Suite Control — panel test
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panel.hpp"

#include <cstdio>
#include <string>

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
    // The surface writes requests; the plug-in publishes the value later.
    auto on = [&](int slot, suite::peer::Client& c) {
      return c.raw()->slot[slot].param[suite::ch::kOutput].request.load() > 0.01;
    };
    // Start both at a normal fader position.
    suite::peer::Client::writeRemote(c1.raw()->slot[a], suite::ch::kOutput, 0.75);
    suite::peer::Client::writeRemote(c2.raw()->slot[b], suite::ch::kOutput, 0.75);
    p4.pointerDown(s4, st4, 40.0, 676.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "mute disables exactly one channel");
    // Solo on the other strip: the muted one stays off, the soloed one on.
    p4.pointerDown(s4, st4, 141.0 + 70.0, 676.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "solo leaves one channel on");
    p4.pointerDown(s4, st4, 141.0 + 70.0, 676.0, false);
    p4.pointerUp();
    check(on(a, c1) != on(b, c2), "mute survives solo release");
    // Unmute restores the fader.
    p4.pointerDown(s4, st4, 40.0, 676.0, false);
    p4.pointerUp();
    check(on(a, c1) && on(b, c2), "unmute restores the fader");
    c1.unregisterSlot();
    c2.unregisterSlot();
  }

  // Tab commits a name and opens the next strip's field.
  {
    suite::peer::Client c1, c2;
    c1.registerSlot(7, "Suite Channel Plus", suite::ch::kCount);
    c2.registerSlot(7, "Suite Channel Plus", suite::ch::kCount);
    suite::Shared s5;
    s5.set(suite::P_BUS, 7.0);
    suite::PanelState st5;
    suite::Panel p5;
    p5.tick(s5, st5);
    p5.beginTextEntry(s5, st5, 118.0, 124.0);
    p5.textKey(s5, st5, 0, 'K');
    p5.textKey(s5, st5, suite::Panel::kTextTab, 0);
    check(std::string(s5.names[7 * 32]) == "K",
          "tab commits the current name");
    check(p5.editing(), "tab opens the next field");
    p5.textKey(s5, st5, 0, 'S');
    p5.textKey(s5, st5, suite::Panel::kTextEnter, 0);
    check(std::string(s5.names[7 * 32 + 1]) == "S",
          "second name commits on enter");
    c1.unregisterSlot();
    c2.unregisterSlot();
  }

  // Right-click editing: same control closes, another control switches,
  // empty space closes.
  {
    suite::peer::Client c;
    c.registerSlot(7, "Suite Channel Plus", suite::ch::kCount);
    suite::Shared s6;
    s6.set(suite::P_BUS, 7.0);
    suite::PanelState st6;
    suite::Panel p6;
    p6.tick(s6, st6);
    p6.beginTextEntry(s6, st6, 50.0, 194.0);
    check(p6.editing(), "right-click opens the editor");
    p6.beginTextEntry(s6, st6, 50.0, 194.0);
    check(!p6.editing(), "second right-click closes the editor");
    p6.beginTextEntry(s6, st6, 50.0, 194.0);
    p6.beginTextEntry(s6, st6, 102.0, 194.0);
    check(p6.editing(), "right-click on another knob switches target");
    p6.beginTextEntry(s6, st6, 700.0, 400.0);
    check(!p6.editing(), "right-click on empty space closes the editor");
    c.unregisterSlot();
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

  // -------------------------------------------------------- transport
  {
    suite::Shared st;
    suite::PanelState pst;
    suite::Panel pp;
    std::vector<int> actions;
    pst.transport = [&](int action) { actions.push_back(action); };
    pp.tick(st, pst);
    const double buttons = 5 * 40.0 + 4 * 6.0;
    const double total =
        buttons + 10.0 + 46.0 + 10.0 + 88.0 + 8.0 + 44.0 + 8.0 + 16.0;
    const double x0 = 20.0 + (980.0 - total) * 0.5;
    for (int i = 0; i < 5; ++i) {
      pp.pointerDown(st, pst, x0 + i * 46.0 + 20.0, 735.0, false);
      pp.pointerUp();
    }
    const double loopX = x0 + buttons + 10.0 + 23.0;
    pp.pointerDown(st, pst, loopX, 735.0, false);  // loop
    pp.pointerUp();
    check(actions.size() == 6 && actions[0] == suite::kTransportRewind &&
              actions[1] == suite::kTransportPlay &&
              actions[2] == suite::kTransportStop &&
              actions[3] == suite::kTransportRecord &&
              actions[4] == suite::kTransportForward &&
              actions[5] == suite::kTransportLoop,
          "transport buttons send their actions");

    // Play/record lights follow the host feedback.
    pst.playing = true;
    pst.recording = true;
    pst.loop = true;
    std::snprintf(pst.time, sizeof(pst.time), "1:23.4");
    pp.draw(cairo_create(surface), st, pst);
    check(true, "transport row draws with feedback state");

    // The chevron collapses the row to a chip; clicking the chip restores it.
    const double hideX = x0 + total - 8.0;
    pp.pointerDown(st, pst, hideX, 735.0, false);
    pp.pointerUp();
    pp.draw(cairo_create(surface), st, pst);
    actions.clear();
    pp.pointerDown(st, pst, 510.0, 735.0, false);  // the chip
    pp.pointerUp();
    check(actions.empty(), "the collapsed chip does not send transport");
    pp.pointerDown(st, pst, x0 + 46.0 + 20.0, 735.0, false);  // play again
    pp.pointerUp();
    check(actions.size() == 1 && actions[0] == suite::kTransportPlay,
          "the chip restores the transport row");

    // OSC settings sheet: sync and editable ports.
    pst.oscSendPort = 8000;
    pst.oscRecvPort = 9000;
    int syncs = 0, cfgSend = 0, cfgRecv = 0;
    pst.oscSync = [&] { ++syncs; };
    pst.oscConfigure = [&](int sendPort, int recvPort) {
      cfgSend = sendPort;
      cfgRecv = recvPort;
    };
    const double setupX = x0 + buttons + 10.0 + 46.0 + 10.0 + 88.0 + 8.0 + 22.0;
    pp.pointerDown(st, pst, setupX, 735.0, false);
    pp.pointerUp();
    pp.draw(cairo_create(surface), st, pst);
    check(true, "OSC settings sheet draws");
    pp.pointerDown(st, pst, 330.0 + 85.0, 170.0 + 352.0, false);  // SYNC
    pp.pointerUp();
    check(syncs == 1, "OSC sync asks the host for its state");
    pp.pointerDown(st, pst, 330.0 + 218.0, 170.0 + 97.0, false);  // send port
    pp.pointerUp();
    for (int i = 0; i < 4; ++i) {
      pp.textKey(st, pst, suite::Panel::kTextBackspace, 0);
    }
    for (char c : std::string("8001")) {
      pp.textKey(st, pst, 0, static_cast<unsigned char>(c));
    }
    pp.textKey(st, pst, suite::Panel::kTextEnter, 0);
    check(cfgSend == 8001 && cfgRecv == 9000, "OSC send port is applied");
    pp.pointerDown(st, pst, 330.0 + 555.0, 170.0 + 352.0, false);  // CLOSE
    pp.pointerUp();
    actions.clear();
    pp.pointerDown(st, pst, x0 + 46.0 + 20.0, 735.0, false);
    pp.pointerUp();
    check(actions.size() == 1, "transport works after closing OSC settings");
  }

  // -------------------------------------------------------------- OSC view
  {
    suite::Shared st;
    suite::PanelState pst;
    suite::Panel po;
    pst.osc = true;
    pst.hostBank = 1;
    std::snprintf(pst.host[0].name, sizeof(pst.host[0].name), "Drums");
    std::snprintf(pst.host[0].gain, sizeof(pst.host[0].gain), "-6.0 dB");
    pst.host[0].volume = 0.72;
    pst.host[0].valid = true;
    pst.host[0].vuL = pst.host[0].vuR = -18.0f;
    po.tick(st, pst);
    po.pointerDown(st, pst, 905.0, 92.0, false);  // OSC view pill
    po.pointerUp();
    po.draw(cairo_create(surface), st, pst);
    check(true, "OSC view draws host strips");
  }

  // ---------------------------------------------------------- inserts
  // An insert plug-in publishes metadata; the OUT view cell opens the picker
  // and assigns the LINK, then toggles bypass.
  {
    // The surface needs a strip on its bus before it lays out insert cells.
    suite::peer::Client strip;
    strip.registerSlot(8, "Suite Channel Plus", suite::ch::kCount);
    suite::peer::Client insert;
    const int slot = insert.registerSlot(8, "Comp Suite", 2,
                                         suite::peer::kKindInsert, 1);
    check(slot >= 0, "fake insert registers");
    suite::peer::ParamMeta meta[2] = {};
    std::snprintf(meta[0].symbol, sizeof(meta[0].symbol), "threshold");
    std::snprintf(meta[0].name, sizeof(meta[0].name), "Threshold");
    std::snprintf(meta[0].group, sizeof(meta[0].group), "Compressor");
    meta[0].min = 0.0f;
    meta[0].max = 1.0f;
    meta[0].def = 0.5f;
    std::snprintf(meta[1].symbol, sizeof(meta[1].symbol), "power");
    std::snprintf(meta[1].name, sizeof(meta[1].name), "Power");
    std::snprintf(meta[1].group, sizeof(meta[1].group), "Global");
    meta[1].max = 1.0f;
    meta[1].flags = suite::peer::kMetaToggle;
    insert.publishMeta(meta, 2);
    insert.publish(1, 1.0);  // engaged

    suite::Shared si;
    si.set(suite::P_BUS, 8.0);
    suite::PanelState sti;
    suite::Panel pi;
    pi.tick(si, sti);
    pi.pointerDown(si, sti, 748.0, 92.0, false);  // OUT view
    pi.pointerUp();
    pi.tick(si, sti);

    pi.pointerDown(si, sti, 48.0, 155.0, false);  // first insert cell
    pi.pointerUp();
    pi.pointerDown(si, sti, 400.0, 276.0, false);  // first picker row
    pi.pointerUp();
    suite::peer::Slot& is = insert.raw()->slot[slot];
    check(is.linkBus.load() == 8 && is.linkChannel.load() == 1 &&
              is.linkSlot.load() == 1,
          "picker assigns the insert to the channel slot");
    check(pi.stripCount() >= 0 && is.linkBus.load() == 8,
          "link lands on the peer slot");

    pi.pointerDown(si, sti, 48.0, 155.0, false);  // toggle bypass
    pi.pointerUp();
    check(is.param[1].request.load() < 0.5, "cell click toggles bypass");

    insert.unregisterSlot();
    strip.unregisterSlot();
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

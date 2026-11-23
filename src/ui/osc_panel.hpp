// Suite Control — glue between the OSC link and the panel state.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>

#include "ui/osc.hpp"
#include "ui/panel.hpp"

namespace suite {

// Copies one consistent OSC snapshot into the panel's host-track view.
inline void applyOscState(const OscLink& link, PanelState& state) {
  const OscSnapshot snap = link.snapshot();
  state.playing = snap.playing;
  state.recording = snap.recording;
  state.loop = snap.loop;
  std::snprintf(state.time, sizeof(state.time), "%s", snap.time);
  state.osc = link.valid() && link.online();
  state.oscProtocol = link.protocol() == OscProtocol::Ardour
                          ? 1
                          : (link.protocol() == OscProtocol::Auto ? 2 : 0);
  state.oscSendPort = link.sendPort();
  state.oscRecvPort = link.recvPort();
  state.hostBank = snap.bank;
  for (int t = 0; t < kHostTracks; ++t) {
    const HostTrackState& src = snap.track[t];
    HostTrack& dst = state.host[t];
    std::snprintf(dst.name, sizeof(dst.name), "%s", src.name);
    std::snprintf(dst.gain, sizeof(dst.gain), "%s", src.gain);
    dst.volume = src.volume;
    dst.pan = src.pan;
    dst.mute = src.mute;
    dst.solo = src.solo;
    dst.rec = src.rec;
    dst.valid = src.valid;
    dst.vuL = src.vuL;
    dst.vuR = src.vuR;
  }
}

// Routes the panel's OSC controls to the link.  The callbacks hold a pointer,
// so rebinding after the link is constructed is safe.
inline void bindOsc(OscLink& link, PanelState& state) {
  OscLink* l = &link;
  state.transport = [l](int action) { l->transport(action); };
  state.hostControl = [l](int index, int control, double value) {
    switch (control) {
      case kHostVolume: l->setTrackVolume(index, value); break;
      case kHostPan: l->setTrackPan(index, value); break;
      case kHostMute: l->setTrackMute(index, value > 0.5); break;
      case kHostSolo: l->setTrackSolo(index, value > 0.5); break;
      case kHostRec: l->setTrackRec(index, value > 0.5); break;
      default: break;
    }
  };
  state.hostSelect = [l](int index) { l->selectTrack(index); };
  state.hostBankChange = [l](int delta) { l->changeBank(delta); };
  state.oscConfigure = [l](int send, int recv) {
    l->configure(static_cast<uint16_t>(send), static_cast<uint16_t>(recv));
  };
  state.oscSync = [l] { l->sync(); };
}

} // namespace suite

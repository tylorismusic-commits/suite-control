// OSC host link — localhost send/receive tests (no host needed).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

#include "ui/osc.hpp"
#include "ui/transport.hpp"

namespace {
int failures = 0;
bool check(bool ok, const char* what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
  return ok;
}

int makeReceiver(uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;
  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

size_t pad4(size_t n) { return (n + 3u) & ~size_t(3u); }

struct Message {
  char address[64] = {};
  char types[16] = {};
  double number = 0.0;
  char text[64] = {};
};

// Returns false on timeout.
bool receiveMessage(int fd, Message* out, int timeoutMs = 1000) {
  pollfd p{fd, POLLIN, 0};
  if (poll(&p, 1, timeoutMs) <= 0) return false;
  uint8_t buffer[1024] = {};
  const ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
  if (got <= 0) return false;
  const char* address = reinterpret_cast<const char*>(buffer);
  const size_t addressLen = strnlen(address, sizeof(buffer));
  std::snprintf(out->address, sizeof(out->address), "%s", address);
  size_t offset = pad4(addressLen + 1);
  if (offset >= static_cast<size_t>(got) || buffer[offset] != ',') return true;
  const char* tags = reinterpret_cast<const char*>(buffer + offset);
  const size_t tagLen = strnlen(tags, static_cast<size_t>(got) - offset);
  std::snprintf(out->types, sizeof(out->types), "%s", tags + 1);
  size_t data = pad4(offset + tagLen + 1);
  if (tagLen > 1 && data + 4 <= static_cast<size_t>(got)) {
    uint32_t raw = 0;
    std::memcpy(&raw, buffer + data, 4);
    if (tags[1] == 'i') {
      out->number = static_cast<double>(static_cast<int32_t>(ntohl(raw)));
      data += 4;
    } else if (tags[1] == 'f') {
      raw = ntohl(raw);
      float f = 0.0f;
      std::memcpy(&f, &raw, 4);
      out->number = f;
      data += 4;
    } else if (tags[1] == 's') {
      std::snprintf(out->text, sizeof(out->text), "%s",
                    reinterpret_cast<const char*>(buffer + data));
    }
  }
  return true;
}

// The link greets the host on open; drop those datagrams.
void drain(int fd) {
  Message m;
  while (receiveMessage(fd, &m, 120)) {
  }
}

void sendMessage(uint16_t port, const std::string& bytes) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sendto(fd, bytes.data(), bytes.size(), 0,
         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  ::close(fd);
}

std::string packHeader(const char* address, const char* tags) {
  std::string b(address);
  b.push_back('\0');
  while (b.size() % 4) b.push_back('\0');
  b.push_back(',');
  for (const char* t = tags; *t; ++t) b.push_back(*t);
  b.push_back('\0');
  while (b.size() % 4) b.push_back('\0');
  return b;
}

std::string packInt(const char* address, int value) {
  std::string b = packHeader(address, "i");
  const uint32_t raw = htonl(static_cast<uint32_t>(value));
  b.append(reinterpret_cast<const char*>(&raw), 4);
  return b;
}

std::string packFloat(const char* address, float value) {
  std::string b = packHeader(address, "f");
  uint32_t raw = 0;
  std::memcpy(&raw, &value, 4);
  raw = htonl(raw);
  b.append(reinterpret_cast<const char*>(&raw), 4);
  return b;
}

std::string packIntPair(const char* address, int first, int second) {
  std::string b = packHeader(address, "ii");
  for (int v : {first, second}) {
    const uint32_t raw = htonl(static_cast<uint32_t>(v));
    b.append(reinterpret_cast<const char*>(&raw), 4);
  }
  return b;
}

std::string packFloatPair(const char* address, int first, float second) {
  std::string b = packHeader(address, "if");
  const uint32_t rawIndex = htonl(static_cast<uint32_t>(first));
  b.append(reinterpret_cast<const char*>(&rawIndex), 4);
  uint32_t raw = 0;
  std::memcpy(&raw, &second, 4);
  raw = htonl(raw);
  b.append(reinterpret_cast<const char*>(&raw), 4);
  return b;
}

std::string packIntString(const char* address, int first, const char* text) {
  std::string b = packHeader(address, "is");
  const uint32_t raw = htonl(static_cast<uint32_t>(first));
  b.append(reinterpret_cast<const char*>(&raw), 4);
  b.append(text);
  b.push_back('\0');
  while (b.size() % 4) b.push_back('\0');
  return b;
}

std::string packString(const char* address, const char* text) {
  std::string b = packHeader(address, "s");
  b.append(text);
  b.push_back('\0');
  while (b.size() % 4) b.push_back('\0');
  return b;
}

void sendBundle(uint16_t port, const std::vector<std::string>& messages) {
  std::string b("#bundle\0");
  b.resize(16, '\0');
  for (const std::string& m : messages) {
    const uint32_t size = htonl(static_cast<uint32_t>(m.size()));
    b.append(reinterpret_cast<const char*>(&size), 4);
    b.append(m);
  }
  sendMessage(port, b);
}
} // namespace

int main() {
  // ------------------------------------------------------------- REAPER
  constexpr uint16_t kReaperSend = 19001;
  constexpr uint16_t kReaperRecv = 19002;
  setenv("SUITE_OSC_SEND_PORT", "19001", 1);
  setenv("SUITE_OSC_RECV_PORT", "19002", 1);

  const int reaperPort = makeReceiver(kReaperSend);
  if (!check(reaperPort >= 0, "test receiver binds")) return 1;

  suite::OscLink reaper(suite::OscProtocol::Reaper);
  if (!check(reaper.open(), "OSC link opens")) return 1;
  drain(reaperPort);

  Message m;
  reaper.transport(suite::kTransportPlay);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/play") == 0,
        "play trigger is sent to REAPER's port");
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/1/play") == 0,
        "play is also sent in the LogicTouch spelling");

  reaper.transport(suite::kTransportRewind);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/action/40042") == 0,
        "go-to-start action is sent");

  reaper.transport(suite::kTransportStop);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/stop") == 0,
        "stop trigger is sent");
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/1/stop") == 0,
        "stop is also sent in the LogicTouch spelling");

  reaper.transport(suite::kTransportRecord);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/record") == 0,
        "record trigger is sent");
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/1/record") == 0,
        "record is also sent in the LogicTouch spelling");

  reaper.transport(suite::kTransportLoop);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/repeat") == 0,
        "loop trigger is sent");
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/1/repeat") == 0,
        "loop is also sent in the LogicTouch spelling");

  reaper.setTrackVolume(2, 0.55);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/track/3/volume") == 0 &&
            std::fabs(m.number - 0.55) < 0.01,
        "track volume is sent with the normalised value");
  reaper.setTrackPan(2, 0.25);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/track/3/pan") == 0 &&
            std::fabs(m.number - 0.25) < 0.01,
        "track pan is sent");
  reaper.setTrackMute(0, true);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/track/1/mute/toggle") == 0,
        "track mute is sent as a toggle");
  reaper.changeBank(1);
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/device/track/bank/select/2") == 0,
        "bank select is sent one-based");

  // Feedback: REAPER bundles track state.
  sendBundle(kReaperRecv,
             {packString("/track/1/name", "Drums"),
              packString("/track/2/name", "Bass"),
              packFloat("/track/1/volume", 0.8f),
              packString("/track/1/volume/str", "-6.0 dB"),
              packInt("/track/1/mute", 1),
              packFloat("/track/1/pan", 0.25f),
              packFloat("/track/1/vu/L", 0.5f),
              packFloat("/repeat", 1.0f),
              packFloat("/time", 65.4f),
              packInt("/play", 1)});
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  suite::OscSnapshot snap = reaper.snapshot();
  check(std::strcmp(snap.track[0].name, "Drums") == 0, "track name is decoded");
  check(std::strcmp(snap.track[1].name, "Bass") == 0,
        "second track name is decoded");
  check(std::fabs(snap.track[0].volume - 0.8) < 0.01,
        "track volume is decoded");
  check(std::strcmp(snap.track[0].gain, "-6.0 dB") == 0,
        "track level text is decoded");
  check(snap.track[0].mute, "track mute is decoded");
  check(std::fabs(snap.track[0].pan - 0.25) < 0.01, "track pan is decoded");
  check(snap.track[0].vuL > -20.0f, "track meter is converted to dB");
  check(snap.loop, "loop state is decoded");
  check(std::strcmp(snap.time, "1:05.4") == 0, "time is formatted");
  check(snap.playing, "play feedback is decoded");

  sendBundle(kReaperRecv, {packInt("/track/1/mute", 0),
                           packFloat("/time", 0.0f)});
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  snap = reaper.snapshot();
  check(!snap.track[0].mute, "mute release is decoded");
  check(std::strcmp(snap.time, "0:00.0") == 0, "time follows the host");

  // The selection dump sends the name before the track number; the number
  // decides which strip gets it (and drops names from another bank).
  sendBundle(kReaperRecv,
             {packString("/track/name", "Lead"),
              packString("/track/number/str", "1")});
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  snap = reaper.snapshot();
  check(std::strcmp(snap.track[0].name, "Drums") == 0,
        "a name from another bank is ignored");
  sendBundle(kReaperRecv,
             {packString("/track/name", "Kick"),
              packString("/track/number/str", "9")});
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  snap = reaper.snapshot();
  check(std::strcmp(snap.track[0].name, "Kick") == 0,
        "the selection dump names the track by number");
  check(reaper.online(), "feedback marks the link online");

  // sync() re-sends the handshake; poll() walks the track selection, which
  // is how REAPER publishes each track's name on demand.
  reaper.sync();
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/device/track/count/8") == 0,
        "sync re-sends the handshake");
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/device/track/bank/select/2") == 0,
        "sync re-sends the bank");
  std::this_thread::sleep_for(std::chrono::milliseconds(230));
  reaper.poll();
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/device/track/select/1") == 0,
        "the name sweep starts at track 1");
  std::this_thread::sleep_for(std::chrono::milliseconds(140));
  reaper.poll();
  check(receiveMessage(reaperPort, &m) &&
            std::strcmp(m.address, "/device/track/select/2") == 0,
        "the name sweep advances");
  reaper.close();
  ::close(reaperPort);

  // ------------------------------------------------------------- Ardour
  constexpr uint16_t kArdourSend = 19003;
  constexpr uint16_t kArdourRecv = 19004;
  setenv("SUITE_OSC_SEND_PORT", "19003", 1);
  setenv("SUITE_OSC_RECV_PORT", "19004", 1);
  const int ardourPort = makeReceiver(kArdourSend);
  if (!check(ardourPort >= 0, "Ardour test receiver binds")) return 1;

  suite::OscLink ardour(suite::OscProtocol::Ardour);
  if (!check(ardour.open(), "Ardour link opens")) return 1;

  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/set_surface") == 0 &&
            std::strcmp(m.types, "iiiiiiiii") == 0,
        "Ardour is told the surface capabilities");
  drain(ardourPort);

  ardour.transport(suite::kTransportPlay);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/transport_play") == 0,
        "Ardour play is sent");
  ardour.transport(suite::kTransportStop);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/transport_stop") == 0,
        "Ardour stop is sent");
  ardour.transport(suite::kTransportRewind);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/goto_start") == 0,
        "Ardour go-to-start is sent");
  ardour.transport(suite::kTransportLoop);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/loop_toggle") == 0,
        "Ardour loop is sent");
  ardour.setTrackVolume(1, 0.3);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/strip/fader/2") == 0 &&
            std::fabs(m.number - 0.3) < 0.01,
        "Ardour fader is sent");
  ardour.setTrackMute(0, true);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/strip/mute/1") == 0 &&
            m.number > 0.5,
        "Ardour mute is sent");
  ardour.changeBank(1);
  check(receiveMessage(ardourPort, &m) &&
            std::strcmp(m.address, "/bank_up") == 0,
        "Ardour bank up is sent");

  sendBundle(kArdourRecv,
             {packString("/strip/name/1", "Kick"),
              packFloatPair("/strip/fader", 1, 0.75f),
              packFloatPair("/strip/gain", 1, -3.5f),
              packIntPair("/strip/mute", 1, 1),
              packString("/position/time", "00:01:02.345"),
              packInt("/transport_play", 1), packInt("/loop_toggle", 1),
              packFloatPair("/strip/meter", 1, -12.0f)});
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  snap = ardour.snapshot();
  check(std::strcmp(snap.track[0].name, "Kick") == 0,
        "Ardour name feedback is decoded");
  check(std::fabs(snap.track[0].volume - 0.75) < 0.01,
        "Ardour fader feedback is decoded");
  check(std::strcmp(snap.track[0].gain, "-3.5 dB") == 0,
        "Ardour gain feedback is decoded");
  check(snap.track[0].mute, "Ardour mute feedback is decoded");
  check(snap.track[0].vuL < -11.0f && snap.track[0].vuL > -13.0f,
        "Ardour meter feedback is decoded");
  check(std::strcmp(snap.time, "1:02.3") == 0,
        "Ardour position is formatted");
  check(snap.playing && snap.loop, "Ardour transport feedback is decoded");
  check(ardour.online(), "Ardour feedback marks the link online");
  ardour.close();
  ::close(ardourPort);

  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

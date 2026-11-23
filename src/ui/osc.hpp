// Suite Control — OSC host link.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Talks to the host's OSC surface over localhost UDP, off the audio thread.
// Two address spaces are supported:
//
//   REAPER  send 8000, receive 9000  (an OSC device in "Local port" mode)
//   Ardour  send 3819, receive 9000  (/set_surface tells Ardour where to reply)
//
// Ports can be overridden with SUITE_OSC_SEND_PORT / SUITE_OSC_RECV_PORT.
#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "ui/transport.hpp"

namespace suite {

constexpr int kOscTrackCount = 8;

enum class OscProtocol { Auto, Reaper, Ardour };

// One host track as shown by the OSC strip view.
struct HostTrackState {
  char name[40] = {};
  char gain[16] = {};  // formatted level, e.g. "-6.0 dB"
  double volume = 0.0;
  double pan = 0.5;
  bool mute = false;
  bool solo = false;
  bool rec = false;
  bool valid = false;
  float vuL = -120.0f;
  float vuR = -120.0f;
};

struct OscSnapshot {
  HostTrackState track[kOscTrackCount];
  int bank = 0;
  bool playing = false;
  bool recording = false;
  bool loop = false;
  char time[16] = {};
};

class OscLink {
 public:
  explicit OscLink(OscProtocol protocol = OscProtocol::Reaper)
      : protocol_(protocol) {}

  bool wantsReaper() const { return protocol_ != OscProtocol::Ardour; }
  bool wantsArdour() const { return protocol_ != OscProtocol::Reaper; }
  ~OscLink() { close(); }
  OscLink(const OscLink&) = delete;
  OscLink& operator=(const OscLink&) = delete;

  OscProtocol protocol() const { return protocol_; }

  static uint16_t envPort(const char* name, uint16_t fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    const int port = std::atoi(value);
    return port > 0 && port < 65536 ? static_cast<uint16_t>(port) : fallback;
  }

  bool open() {
    if (fd_ >= 0) return true;
    if (!hasPorts_) {
      sendPort_ = envPort("SUITE_OSC_SEND_PORT",
                          protocol_ == OscProtocol::Ardour ? 3819 : 8000);
      recvPort_ = envPort("SUITE_OSC_RECV_PORT", 9000);
      hasPorts_ = true;
    }
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    int reuse = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(recvPort_);
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    ardourPort_ = envPort("SUITE_OSC_ARDOUR_PORT", 3819);
    toCount_ = 0;
    auto addTarget = [&](uint16_t port) {
      sockaddr_in& to = to_[toCount_++];
      std::memset(&to, 0, sizeof(to));
      to.sin_family = AF_INET;
      to.sin_port = htons(port);
      to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    };
    addTarget(sendPort_);
    if (wantsArdour()) addTarget(ardourPort_);
    running_.store(true, std::memory_order_relaxed);
    rx_ = std::thread([this] { rxLoop(); });
    hello();
    return true;
  }

  void close() {
    running_.store(false, std::memory_order_relaxed);
    if (rx_.joinable()) rx_.join();
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool valid() const { return fd_ >= 0; }
  uint16_t sendPort() const { return sendPort_; }
  uint16_t recvPort() const { return recvPort_; }

  // Re-bind on new ports, keeping the protocol.
  void configure(uint16_t sendPort, uint16_t recvPort) {
    if (sendPort == sendPort_ && recvPort == recvPort_) return;
    const bool wasOpen = fd_ >= 0;
    close();
    sendPort_ = sendPort;
    recvPort_ = recvPort;
    hasPorts_ = true;
    if (wasOpen) open();
  }

  // Ask the host for its state again (Ardour refreshes everything;
  // REAPER walks its track selection, which carries each track's name).
  void sync() {
    if (fd_ < 0) return;
    hello();
  }
  bool playing() const { return playing_.load(std::memory_order_relaxed); }
  bool recording() const { return recording_.load(std::memory_order_relaxed); }
  bool loopEnabled() const { return loop_.load(std::memory_order_relaxed); }
  int bank() const { return bank_.load(std::memory_order_relaxed); }

  bool online() const {
    const int64_t last = lastRx_.load(std::memory_order_relaxed);
    return last != 0 && nowMs() - last < 2500;
  }

  // Consistent copy for the UI thread.
  OscSnapshot snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    OscSnapshot s = state_;
    s.bank = bank_.load(std::memory_order_relaxed);
    s.playing = playing_.load(std::memory_order_relaxed);
    s.recording = recording_.load(std::memory_order_relaxed);
    s.loop = loop_.load(std::memory_order_relaxed);
    return s;
  }

  // Transport: kTransport* values from panel.hpp.
  void transport(int action);
  void setLoop(bool on);

  // Track controls, zero-based index into the current bank.
  void setTrackVolume(int index, double normalised);
  void setTrackPan(int index, double normalised);
  void setTrackMute(int index, bool on);
  void setTrackSolo(int index, bool on);
  void setTrackRec(int index, bool on);
  void selectTrack(int index);
  void changeBank(int delta);

  // Re-send the handshake when the host has not answered yet, and walk the
  // device's track selection so REAPER publishes each track's name.
  void poll() {
    const int64_t now = nowMs();
    if (!online() && now - lastHello_ > 2000) hello();
    if (wantsReaper() && fd_ >= 0 && sweepIndex_ < kOscTrackCount &&
        now >= sweepAt_) {
      char path[48];
      std::snprintf(path, sizeof(path), "/device/track/select/%d",
                    sweepIndex_ + 1);
      selected_ = sweepIndex_;
      sendNoArgs(path);
      ++sweepIndex_;
      sweepAt_ = now + 120;
    }
  }

  // A trigger message (no arguments): /play, /stop, /record.
  void trigger(const char* path);
  // /action/<id> triggers a REAPER action by command id.
  void action(int id);

 private:
  static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
  }

  static size_t pad4(size_t n) { return (n + 3u) & ~size_t(3u); }

  static void formatSeconds(double seconds, char* out, size_t size) {
    if (seconds < 0.0) seconds = 0.0;
    const int total = static_cast<int>(seconds);
    const int h = total / 3600;
    const int m = (total / 60) % 60;
    const int s = total % 60;
    const int tenths = static_cast<int>((seconds - total) * 10.0);
    if (h > 0) {
      std::snprintf(out, size, "%d:%02d:%02d", h, m, s);
    } else {
      std::snprintf(out, size, "%d:%02d.%d", m, s, tenths);
    }
  }

  // Ardour reports "HH:MM:SS.mmm"; drop the hour when it is zero.
  static void formatPositionString(const char* text, char* out, size_t size) {
    int h = 0, m = 0;
    double s = 0.0;
    if (std::sscanf(text, "%d:%d:%lf", &h, &m, &s) != 3) {
      std::snprintf(out, size, "%s", text);
      return;
    }
    if (h > 0) {
      std::snprintf(out, size, "%d:%02d:%02d", h, m,
                    static_cast<int>(s));
    } else {
      std::snprintf(out, size, "%d:%02d.%d", m, static_cast<int>(s),
                    static_cast<int>((s - static_cast<int>(s)) * 10.0));
    }
  }

  void hello() {
    lastHello_ = nowMs();
    if (wantsArdour()) {
      // /set_surface bank stripTypes feedback faderMode sendPage pluginPage
      //              port linkset linkid
      // 8 strips, all track types (159), strip buttons/values/meters/time
      // (1+2+8+128+2048), fader positions 0..1 (2), no paging, auto reply
      // port (0) because this socket is both sender and receiver.
      const int args[] = {kOscTrackCount, 159, 2187, 2, 0, 0, 0, 0, 0};
      sendInts("/set_surface", args, 9);
    }
    if (wantsReaper()) {
      char path[48];
      std::snprintf(path, sizeof(path), "/device/track/count/%d",
                    kOscTrackCount);
      sendNoArgs(path);
      std::snprintf(path, sizeof(path), "/device/track/bank/select/%d",
                    bank_.load(std::memory_order_relaxed) + 1);
      sendNoArgs(path);
      sweepIndex_ = 0;
      sweepAt_ = nowMs() + 200;
      selected_ = -1;
    }
  }

  void sendMessage(const char* path, const char* tags, const void* data,
                   size_t bytes) {
    if (fd_ < 0 || !path) return;
    uint8_t buffer[512] = {};
    const size_t pathLen = std::strlen(path);
    if (pathLen + 8 >= sizeof(buffer)) return;
    std::memcpy(buffer, path, pathLen);
    size_t offset = pad4(pathLen + 1);
    buffer[offset++] = ',';
    const size_t tagLen = tags ? std::strlen(tags) : 0;
    if (tagLen) std::memcpy(buffer + offset, tags, tagLen);
    offset = pad4(offset + tagLen + 1);
    if (bytes) {
      if (offset + bytes > sizeof(buffer)) return;
      std::memcpy(buffer + offset, data, bytes);
      offset += pad4(bytes);
    }
    for (int i = 0; i < toCount_; ++i) {
      ::sendto(fd_, buffer, offset, 0,
               reinterpret_cast<const sockaddr*>(&to_[i]), sizeof(to_[i]));
    }
  }

  void sendNoArgs(const char* path) { sendMessage(path, nullptr, nullptr, 0); }

  void actionGo(int id) {
    char path[32];
    std::snprintf(path, sizeof(path), "/action/%d", id);
    sendNoArgs(path);
  }

  void sendInt(const char* path, int value) {
    const uint32_t raw = htonl(static_cast<uint32_t>(value));
    sendMessage(path, "i", &raw, 4);
  }

  void sendFloat(const char* path, float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, 4);
    raw = htonl(raw);
    sendMessage(path, "f", &raw, 4);
  }

  void sendInts(const char* path, const int* values, int count) {
    char tags[32] = {};
    for (int i = 0; i < count && i < 31; ++i) tags[i] = 'i';
    uint32_t packed[32];
    for (int i = 0; i < count; ++i) {
      packed[i] = htonl(static_cast<uint32_t>(values[i]));
    }
    sendMessage(path, tags, packed, static_cast<size_t>(count) * 4);
  }

  void rxLoop() {
    uint8_t buffer[2048];
    while (running_.load(std::memory_order_relaxed)) {
      fd_set read;
      FD_ZERO(&read);
      FD_SET(fd_, &read);
      timeval timeout{};
      timeout.tv_usec = 100 * 1000;
      const int ready = ::select(fd_ + 1, &read, nullptr, nullptr, &timeout);
      if (ready <= 0) continue;
      const ssize_t got =
          ::recvfrom(fd_, buffer, sizeof(buffer), 0, nullptr, nullptr);
      if (got <= 0) continue;
      lastRx_.store(nowMs(), std::memory_order_relaxed);
      parse(buffer, static_cast<size_t>(got));
    }
  }

  // REAPER sends state changes inside an OSC bundle, so unwrap those first.
  void parse(const uint8_t* data, size_t size) {
    if (size >= 16 && !std::memcmp(data, "#bundle", 7)) {
      size_t offset = 16;  // "#bundle\0" + 8-byte time tag
      while (offset + 4 <= size) {
        uint32_t raw = 0;
        std::memcpy(&raw, data + offset, 4);
        const size_t length = ntohl(raw);
        offset += 4;
        if (length == 0 || offset + length > size) break;
        parseMessage(data + offset, length);
        offset += length;
      }
      return;
    }
    parseMessage(data, size);
  }

  struct Arg {
    char tag = 0;
    double number = 0.0;
    char text[48] = {};
  };

  // Reads up to four arguments.  Only the types the two hosts send are
  // handled: i, f and s.
  static int readArgs(const uint8_t* data, size_t size, size_t tagsOffset,
                      const char* tags, size_t tagLen, Arg* args, int maxArgs) {
    int count = 0;
    size_t offset = pad4(tagsOffset + tagLen + 1);
    for (size_t i = 1; i < tagLen && count < maxArgs; ++i) {
      if (tags[i] == 'i') {
        if (offset + 4 > size) break;
        uint32_t raw = 0;
        std::memcpy(&raw, data + offset, 4);
        args[count].tag = 'i';
        args[count].number = static_cast<double>(static_cast<int32_t>(ntohl(raw)));
        offset += 4;
      } else if (tags[i] == 'f') {
        if (offset + 4 > size) break;
        uint32_t raw = 0;
        std::memcpy(&raw, data + offset, 4);
        raw = ntohl(raw);
        float f = 0.0f;
        std::memcpy(&f, &raw, 4);
        args[count].tag = 'f';
        args[count].number = f;
        offset += 4;
      } else if (tags[i] == 's') {
        const char* s = reinterpret_cast<const char*>(data + offset);
        const size_t len = strnlen(s, size - offset);
        std::snprintf(args[count].text, sizeof(args[count].text), "%s", s);
        args[count].tag = 's';
        offset += pad4(len + 1);
      } else {
        break;
      }
      ++count;
    }
    return count;
  }

  void parseMessage(const uint8_t* data, size_t size) {
    const char* address = reinterpret_cast<const char*>(data);
    const size_t addressLen = strnlen(address, size);
    if (addressLen == 0 || addressLen >= size) return;
    size_t tagsOffset = pad4(addressLen + 1);
    if (tagsOffset >= size || data[tagsOffset] != ',') return;
    const char* tags = reinterpret_cast<const char*>(data + tagsOffset);
    const size_t tagLen = strnlen(tags, size - tagsOffset);
    Arg args[4];
    const int argc = readArgs(data, size, tagsOffset, tags, tagLen, args, 4);
    const double value = argc > 0 ? args[0].number : 0.0;
    const bool haveNumber = argc > 0 && args[0].tag != 's';

    if (wantsArdour()) parseArdour(address, args, argc, value, haveNumber);
    if (wantsReaper()) parseReaper(address, args, argc, value, haveNumber);
  }

  void parseReaper(const char* address, const Arg* args, int argc,
                   double value, bool haveNumber) {
    // LogicTouch spellings.
    const char* name = address;
    if (name[0] == '/' && name[1] == '1' && name[2] == '/') name += 2;

    int track = 0;
    char leaf[24] = {};
    if (std::sscanf(name, "/track/%d/%23s", &track, leaf) == 2 && track >= 1 &&
        track <= kOscTrackCount) {
      updateTrack(track - 1, leaf, args, argc);
      return;
    }
    // The device-selection dump uses the selected track's unindexed paths.
    // REAPER sends the name before the track number, so hold the name until
    // the number says which track it belongs to; this also drops stale names
    // when an empty bank is selected.
    if (std::strncmp(name, "/track/", 7) == 0 &&
        (name[7] < '0' || name[7] > '9')) {
      const char* leaf = name + 7;
      if (!std::strcmp(leaf, "name") && argc >= 1 && args[0].tag == 's') {
        std::snprintf(pendingName_, sizeof(pendingName_), "%s", args[0].text);
        pendingNameValid_ = true;
        return;
      }
      if (!std::strcmp(leaf, "number/str") && argc >= 1 &&
          args[0].tag == 's') {
        const int number = std::atoi(args[0].text);
        const int slot =
            number - 1 - bank_.load(std::memory_order_relaxed) * kOscTrackCount;
        if (pendingNameValid_ && slot >= 0 && slot < kOscTrackCount) {
          std::lock_guard<std::mutex> lock(mutex_);
          HostTrackState& t = state_.track[slot];
          std::snprintf(t.name, sizeof(t.name), "%s", pendingName_);
          t.valid = t.name[0] != '\0';
        }
        pendingNameValid_ = false;
        return;
      }
      if (selected_ >= 0) updateTrack(selected_, leaf, args, argc);
      return;
    }
    int selected = 0;
    if (std::sscanf(name, "/device/track/select/%d", &selected) == 1 &&
        selected >= 1 && selected <= kOscTrackCount) {
      selected_ = selected - 1;
      return;
    }

    if (!std::strcmp(name, "/play")) {
      if (haveNumber) setPlaying(value > 0.5);
    } else if (!std::strcmp(name, "/record")) {
      if (haveNumber) recording_.store(value > 0.5, std::memory_order_relaxed);
    } else if (!std::strcmp(name, "/stop")) {
      if (haveNumber && value > 0.5) setPlaying(false);
    } else if (!std::strcmp(name, "/repeat")) {
      if (haveNumber) loop_.store(value > 0.5, std::memory_order_relaxed);
    } else if (!std::strcmp(name, "/time")) {
      if (haveNumber) {
        std::lock_guard<std::mutex> lock(mutex_);
        formatSeconds(value, state_.time, sizeof(state_.time));
      }
    } else if (!std::strcmp(name, "/device/track/bank/select")) {
      if (haveNumber && value >= 1.0) {
        bank_.store(static_cast<int>(value) - 1);
      }
    }
  }

  void parseArdour(const char* address, const Arg* args, int argc,
                   double value, bool haveNumber) {
    if (!std::strcmp(address, "/position/time") && argc > 0 &&
        args[0].tag == 's') {
      std::lock_guard<std::mutex> lock(mutex_);
      formatPositionString(args[0].text, state_.time, sizeof(state_.time));
      return;
    }
    if (!std::strcmp(address, "/transport_play")) {
      if (haveNumber) setPlaying(value > 0.5);
      return;
    }
    if (!std::strcmp(address, "/transport_stop")) {
      if (haveNumber && value > 0.5) setPlaying(false);
      return;
    }
    if (!std::strcmp(address, "/loop_toggle")) {
      if (haveNumber) loop_.store(value > 0.5, std::memory_order_relaxed);
      return;
    }
    if (!std::strcmp(address, "/rec_enable_toggle")) {
      if (haveNumber) recording_.store(value > 0.5, std::memory_order_relaxed);
      return;
    }
    if (!std::strcmp(address, "/transport_frame")) {
      return;  // frame counts are not used; /position/time is the display
    }
    char leaf[40] = {};
    if (std::sscanf(address, "/strip/%39s", leaf) != 1) return;
    char* slash = std::strrchr(leaf, '/');
    int ssid = 0;
    int firstArg = 0;
    if (slash && std::sscanf(slash + 1, "%d", &ssid) == 1) {
      // Inline form: /strip/fader/2 0.5
      *slash = '\0';
      firstArg = 0;
    } else if (argc >= 2) {
      // Pair form: /strip/fader 2 0.5
      ssid = static_cast<int>(args[0].number + 0.5);
      firstArg = 1;
    }
    if (ssid >= 1 && ssid <= kOscTrackCount && argc > firstArg) {
      updateTrack(ssid - 1, leaf, args + firstArg, argc - firstArg);
    }
  }

  void updateTrack(int index, const char* leaf, const Arg* args, int argc) {
    if (index < 0 || index >= kOscTrackCount) return;
    std::lock_guard<std::mutex> lock(mutex_);
    HostTrackState& t = state_.track[index];
    if (!std::strcmp(leaf, "name") && argc >= 1 && args[0].tag == 's') {
      std::snprintf(t.name, sizeof(t.name), "%s", args[0].text);
      t.valid = t.name[0] != '\0';
    } else if (!std::strcmp(leaf, "fader") && argc >= 1) {
      t.volume = std::clamp(args[0].number, 0.0, 1.0);
      t.valid = true;
    } else if (!std::strcmp(leaf, "volume") && argc >= 1) {
      // /track/<n>/volume is normalised too.
      t.volume = std::clamp(args[0].number, 0.0, 1.0);
      t.valid = true;
    } else if (!std::strcmp(leaf, "volume/str") && argc >= 1 &&
               args[0].tag == 's') {
      std::snprintf(t.gain, sizeof(t.gain), "%s", args[0].text);
    } else if (!std::strcmp(leaf, "gain") && argc >= 1) {
      std::snprintf(t.gain, sizeof(t.gain), "%.1f dB", args[0].number);
    } else if (!std::strcmp(leaf, "pan") && argc >= 1) {
      t.pan = std::clamp(args[0].number, 0.0, 1.0);
    } else if (!std::strcmp(leaf, "pan_stereo_position") && argc >= 1) {
      t.pan = std::clamp(args[0].number, 0.0, 1.0);
    } else if (!std::strcmp(leaf, "mute") && argc >= 1) {
      t.mute = args[0].number > 0.5;
    } else if (!std::strcmp(leaf, "solo") && argc >= 1) {
      t.solo = args[0].number > 0.5;
    } else if (!std::strcmp(leaf, "recarm") && argc >= 1) {
      t.rec = args[0].number > 0.5;
    } else if (!std::strcmp(leaf, "recenable") && argc >= 1) {
      t.rec = args[0].number > 0.5;
    } else if (!std::strcmp(leaf, "vu") && argc >= 1) {
      const float db = normToDb(args[0].number);
      t.vuL = t.vuR = db;
    } else if (!std::strcmp(leaf, "vu/L") && argc >= 1) {
      t.vuL = normToDb(args[0].number);
    } else if (!std::strcmp(leaf, "vu/R") && argc >= 1) {
      t.vuR = normToDb(args[0].number);
    } else if (!std::strcmp(leaf, "meter") && argc >= 1) {
      t.vuL = t.vuR = static_cast<float>(args[0].number);
    }
  }

  static float normToDb(double value) {
    if (value <= 0.00001) return -120.0f;
    return std::max(-120.0f, static_cast<float>(20.0 * std::log10(value)));
  }

  void setPlaying(bool on) {
    playing_.store(on, std::memory_order_relaxed);
  }

  OscProtocol protocol_;
  int fd_ = -1;
  uint16_t sendPort_ = 8000;
  uint16_t recvPort_ = 9000;
  sockaddr_in to_[2];
  int toCount_ = 1;
  uint16_t ardourPort_ = 3819;
  std::thread rx_;
  std::atomic<bool> running_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> recording_{false};
  std::atomic<bool> loop_{false};
  std::atomic<int> bank_{0};
  std::atomic<int64_t> lastRx_{0};
  int64_t lastHello_ = 0;
  bool hasPorts_ = false;
  int sweepIndex_ = 0;
  int64_t sweepAt_ = 0;
  int selected_ = -1;
  char pendingName_[40] = {};
  bool pendingNameValid_ = false;
  mutable std::mutex mutex_;
  OscSnapshot state_;
};

inline void OscLink::transport(int action) {
  // Mirrors the kTransport* values in panel.hpp; the includes stay decoupled.
  switch (action) {
    case 0:  // rewind / go to start
      if (wantsReaper()) actionGo(40042);
      if (wantsArdour()) sendNoArgs("/goto_start");
      break;
    case 1:  // play
      if (wantsReaper()) trigger("/play");
      if (wantsArdour()) sendNoArgs("/transport_play");
      break;
    case 2:  // stop
      if (wantsReaper()) trigger("/stop");
      if (wantsArdour()) sendNoArgs("/transport_stop");
      break;
    case 3:  // record
      if (wantsReaper()) trigger("/record");
      if (wantsArdour()) {
        const bool armed = recording_.load(std::memory_order_relaxed);
        // Record in one press: arm, then roll if we are stopped.
        sendInt("/rec_enable_toggle", 1);
        if (!armed && !playing_.load(std::memory_order_relaxed)) {
          sendNoArgs("/transport_play");
        }
      }
      break;
    case 4:  // forward / go to end
      if (wantsReaper()) actionGo(40043);
      if (wantsArdour()) sendNoArgs("/goto_end");
      break;
    case 5:  // loop
      if (wantsReaper()) trigger("/repeat");
      if (wantsArdour()) sendNoArgs("/loop_toggle");
      break;
    default:
      break;
  }
}

inline void OscLink::setLoop(bool on) {
  if (wantsReaper()) sendNoArgs("/repeat");
  if (wantsArdour() && on != loop_.load(std::memory_order_relaxed)) {
    sendNoArgs("/loop_toggle");
  }
}

inline void OscLink::setTrackVolume(int index, double normalised) {
  if (index < 0 || index >= kOscTrackCount) return;
  if (wantsReaper()) {
    char path[48];
    std::snprintf(path, sizeof(path), "/track/%d/volume", index + 1);
    sendFloat(path, static_cast<float>(normalised));
  }
  if (wantsArdour()) {
    char path[48];
    std::snprintf(path, sizeof(path), "/strip/fader/%d", index + 1);
    sendFloat(path, static_cast<float>(normalised));
  }
}

inline void OscLink::setTrackPan(int index, double normalised) {
  if (index < 0 || index >= kOscTrackCount) return;
  char path[64];
  if (wantsReaper()) {
    std::snprintf(path, sizeof(path), "/track/%d/pan", index + 1);
    sendFloat(path, static_cast<float>(normalised));
  }
  if (wantsArdour()) {
    std::snprintf(path, sizeof(path), "/strip/pan_stereo_position/%d",
                  index + 1);
    sendFloat(path, static_cast<float>(normalised));
  }
}

inline void OscLink::setTrackMute(int index, bool on) {
  if (index < 0 || index >= kOscTrackCount) return;
  char path[64];
  if (wantsReaper()) {
    std::snprintf(path, sizeof(path), "/track/%d/mute/toggle", index + 1);
    sendNoArgs(path);
  }
  if (wantsArdour()) {
    std::snprintf(path, sizeof(path), "/strip/mute/%d", index + 1);
    sendFloat(path, on ? 1.0f : 0.0f);
  }
}

inline void OscLink::setTrackSolo(int index, bool on) {
  if (index < 0 || index >= kOscTrackCount) return;
  char path[64];
  if (wantsReaper()) {
    std::snprintf(path, sizeof(path), "/track/%d/solo/toggle", index + 1);
    sendNoArgs(path);
  }
  if (wantsArdour()) {
    std::snprintf(path, sizeof(path), "/strip/solo/%d", index + 1);
    sendFloat(path, on ? 1.0f : 0.0f);
  }
}

inline void OscLink::setTrackRec(int index, bool on) {
  if (index < 0 || index >= kOscTrackCount) return;
  char path[64];
  if (wantsReaper()) {
    std::snprintf(path, sizeof(path), "/track/%d/recarm/toggle", index + 1);
    sendNoArgs(path);
  }
  if (wantsArdour()) {
    std::snprintf(path, sizeof(path), "/strip/recenable/%d", index + 1);
    sendFloat(path, on ? 1.0f : 0.0f);
  }
}

inline void OscLink::selectTrack(int index) {
  if (index < 0 || index >= kOscTrackCount) return;
  if (wantsReaper()) {
    char path[32];
    std::snprintf(path, sizeof(path), "/track/%d/select", index + 1);
    sendInt(path, 1);
  }
  if (wantsArdour()) {
    char path[32];
    std::snprintf(path, sizeof(path), "/strip/select/%d", index + 1);
    sendInt(path, 1);
  }
}

inline void OscLink::changeBank(int delta) {
  if (delta == 0) return;
  const int next = wantsReaper()
                       ? std::clamp(bank_.load(std::memory_order_relaxed) + delta,
                                    0, 31)
                       : std::max(0, bank_.load(std::memory_order_relaxed) + delta);
  bank_.store(next, std::memory_order_relaxed);
  if (wantsArdour()) {
    for (int i = 0; i < std::abs(delta); ++i) {
      sendNoArgs(delta > 0 ? "/bank_up" : "/bank_down");
    }
  }
  if (wantsReaper()) {
    char path[48];
    std::snprintf(path, sizeof(path), "/device/track/bank/select/%d", next + 1);
    sendNoArgs(path);
    sweepIndex_ = 0;
    sweepAt_ = nowMs() + 250;
    selected_ = -1;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (int i = 0; i < kOscTrackCount; ++i) state_.track[i] = HostTrackState{};
}

inline void OscLink::trigger(const char* path) {
  // REAPER's shipped pattern files disagree on the address (Default uses
  // /play, LogicTouch /1/play), and an unknown address is ignored, so both
  // are sent.
  sendNoArgs(path);
  if (path[0] == '/' && path[1] != '1') {
    char alias[64];
    std::snprintf(alias, sizeof(alias), "/1%s", path);
    sendNoArgs(alias);
  }
}

inline void OscLink::action(int id) {
  char path[32];
  std::snprintf(path, sizeof(path), "/action/%d", id);
  sendNoArgs(path);
}

} // namespace suite

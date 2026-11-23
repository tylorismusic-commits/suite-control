// Suite Control — OSC transport link.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sends transport triggers to REAPER's OSC surface (defaults: REAPER listens
// on 8000, the device/plug-in receives on 9000) and reads play/record
// feedback.  Ports can be overridden with SUITE_OSC_SEND_PORT /
// SUITE_OSC_RECV_PORT.  Everything is localhost UDP, off the audio thread.
#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace suite {

class OscLink {
 public:
  OscLink() = default;
  ~OscLink() { close(); }
  OscLink(const OscLink&) = delete;
  OscLink& operator=(const OscLink&) = delete;

  static uint16_t envPort(const char* name, uint16_t fallback) {
    const char* value = std::getenv(name);
    if (!value) return fallback;
    const int port = std::atoi(value);
    return port > 0 && port < 65536 ? static_cast<uint16_t>(port) : fallback;
  }

  bool open() {
    if (fd_ >= 0) return true;
    sendPort_ = envPort("SUITE_OSC_SEND_PORT", 8000);
    recvPort_ = envPort("SUITE_OSC_RECV_PORT", 9000);
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
    std::memset(&to_, 0, sizeof(to_));
    to_.sin_family = AF_INET;
    to_.sin_port = htons(sendPort_);
    to_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    running_.store(true, std::memory_order_relaxed);
    rx_ = std::thread([this] { rxLoop(); });
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
  bool playing() const { return playing_.load(std::memory_order_relaxed); }
  bool recording() const { return recording_.load(std::memory_order_relaxed); }
  bool online() const {
    const int64_t last = lastRx_.load(std::memory_order_relaxed);
    return last != 0 && nowMs() - last < 2500;
  }

  // Transport triggers.  REAPER's shipped pattern files disagree on the
  // address (Default uses /play, LogicTouch /1/play), and an unknown address
  // is ignored, so both are sent.  Commands are never toggles, so the extra
  // message is harmless even if a config maps both.
  void trigger(const char* path) {
    sendMessage(path, false, 0);
    if (path[0] == '/' && path[1] != '1') {
      char alias[64];
      std::snprintf(alias, sizeof(alias), "/1%s", path);
      sendMessage(alias, false, 0);
    }
  }

  // /action/<id> triggers a REAPER action by command id.
  void action(int id) {
    char path[32];
    std::snprintf(path, sizeof(path), "/action/%d", id);
    sendMessage(path, false, 0);
  }

 private:
  static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
  }

  static size_t pad4(size_t n) { return (n + 3u) & ~size_t(3u); }

  void sendMessage(const char* path, bool withInt, int value) {
    if (fd_ < 0 || !path) return;
    uint8_t buffer[256] = {};
    const size_t pathLen = std::strlen(path);
    if (pathLen + 8 >= sizeof(buffer)) return;
    std::memcpy(buffer, path, pathLen);
    size_t offset = pad4(pathLen + 1);
    buffer[offset++] = ',';
    if (withInt) {
      buffer[offset++] = 'i';
      offset = pad4(offset + 1);
      const uint32_t v = htonl(static_cast<uint32_t>(value));
      std::memcpy(buffer + offset, &v, 4);
      offset += 4;
    } else {
      offset = pad4(offset + 1);
    }
    ::sendto(fd_, buffer, offset, 0, reinterpret_cast<const sockaddr*>(&to_),
             sizeof(to_));
  }

  void rxLoop() {
    uint8_t buffer[1024];
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

  void parseMessage(const uint8_t* data, size_t size) {
    const char* address = reinterpret_cast<const char*>(data);
    const size_t addressLen = strnlen(address, size);
    if (addressLen == 0 || addressLen >= size) return;
    size_t offset = pad4(addressLen + 1);
    if (offset >= size || data[offset] != ',') return;
    const char* tags = reinterpret_cast<const char*>(data + offset);
    const size_t tagLen = strnlen(tags, size - offset);
    const size_t dataOffset = pad4(offset + tagLen + 1);
    double value = 0.0;
    bool haveValue = false;
    if (dataOffset + 4 <= size) {
      uint32_t raw = 0;
      std::memcpy(&raw, data + dataOffset, 4);
      if (tagLen > 1) {
        if (tags[1] == 'i') {
          value = static_cast<double>(static_cast<int32_t>(ntohl(raw)));
          haveValue = true;
        } else if (tags[1] == 'f') {
          raw = ntohl(raw);
          float f = 0.0f;
          std::memcpy(&f, &raw, 4);
          value = f;
          haveValue = true;
        }
      }
    }
    const char* name = address;
    if (name[0] == '/' && name[1] == '1' && name[2] == '/') name += 2;
    if (!std::strcmp(name, "/play")) {
      if (haveValue) playing_.store(value > 0.5, std::memory_order_relaxed);
    } else if (!std::strcmp(name, "/record")) {
      if (haveValue) recording_.store(value > 0.5, std::memory_order_relaxed);
    } else if (!std::strcmp(name, "/stop")) {
      if (haveValue && value > 0.5) {
        playing_.store(false, std::memory_order_relaxed);
      }
    }
  }

  int fd_ = -1;
  uint16_t sendPort_ = 8000;
  uint16_t recvPort_ = 9000;
  sockaddr_in to_{};
  std::thread rx_;
  std::atomic<bool> running_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> recording_{false};
  std::atomic<int64_t> lastRx_{0};
};

} // namespace suite

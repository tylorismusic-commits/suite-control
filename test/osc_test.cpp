// OSC transport link — localhost send/receive test (no host needed).
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
#include <thread>

#include "ui/osc.hpp"

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

// Waits for one datagram and copies the OSC address.
bool receiveAddress(int fd, char* out, size_t size) {
  pollfd p{fd, POLLIN, 0};
  if (poll(&p, 1, 1000) <= 0) return false;
  uint8_t buffer[512] = {};
  const ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
  if (got <= 0) return false;
  const char* address = reinterpret_cast<const char*>(buffer);
  std::snprintf(out, size, "%s", address);
  return true;
}

void sendBundle(uint16_t port, const char* address, int value) {
  uint8_t message[128] = {};
  const size_t len = std::strlen(address);
  std::memcpy(message, address, len);
  size_t offset = pad4(len + 1);
  message[offset++] = ',';
  message[offset++] = 'i';
  offset = pad4(offset + 1);
  const uint32_t raw = htonl(static_cast<uint32_t>(value));
  std::memcpy(message + offset, &raw, 4);
  offset += 4;

  uint8_t bundle[256] = {};
  std::memcpy(bundle, "#bundle", 7);
  const uint32_t size = htonl(static_cast<uint32_t>(offset));
  std::memcpy(bundle + 16, &size, 4);
  std::memcpy(bundle + 20, message, offset);
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sendto(fd, bundle, 20 + offset, 0, reinterpret_cast<const sockaddr*>(&to),
         sizeof(to));
  ::close(fd);
}

void sendFeedback(uint16_t port, const char* address, int value) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  uint8_t buffer[128] = {};
  const size_t len = std::strlen(address);
  std::memcpy(buffer, address, len);
  size_t offset = pad4(len + 1);
  buffer[offset++] = ',';
  buffer[offset++] = 'i';
  offset = pad4(offset + 1);
  const uint32_t raw = htonl(static_cast<uint32_t>(value));
  std::memcpy(buffer + offset, &raw, 4);
  offset += 4;
  sendto(fd, buffer, offset, 0, reinterpret_cast<const sockaddr*>(&to),
         sizeof(to));
  ::close(fd);
}
} // namespace

int main() {
  constexpr uint16_t kSend = 19001;  // plays "REAPER" in this test
  constexpr uint16_t kRecv = 19002;  // the plug-in's feedback port
  setenv("SUITE_OSC_SEND_PORT", "19001", 1);
  setenv("SUITE_OSC_RECV_PORT", "19002", 1);

  const int receiver = makeReceiver(kSend);
  if (!check(receiver >= 0, "test receiver binds")) return 1;

  suite::OscLink link;
  if (!check(link.open(), "OSC link opens")) return 1;

  char address[64] = {};
  link.trigger("/play");
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/play") == 0,
        "play trigger is sent to REAPER's port");
  // The LogicTouch pattern file only knows /1/play; both are sent.
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/1/play") == 0,
        "play is also sent in the LogicTouch spelling");
  link.action(40042);
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/action/40042") == 0,
        "go-to-start action is sent");
  link.trigger("/stop");
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/stop") == 0,
        "stop trigger is sent");
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/1/stop") == 0,
        "stop is also sent in the LogicTouch spelling");
  link.trigger("/record");
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/record") == 0,
        "record trigger is sent");
  check(receiveAddress(receiver, address, sizeof(address)) &&
            std::strcmp(address, "/1/record") == 0,
        "record is also sent in the LogicTouch spelling");

  // Feedback from a LogicTouch-style device still lights the buttons.
  sendBundle(kRecv, "/1/play", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(link.playing(), "bundle feedback is decoded");
  sendFeedback(kRecv, "/1/play", 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(!link.playing(), "bundled stop clears play state");

  sendFeedback(kRecv, "/1/record", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(link.recording(), "LogicTouch-style record feedback is decoded");
  sendFeedback(kRecv, "/1/record", 0);

  sendFeedback(kRecv, "/play", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(link.playing(), "play feedback is decoded");
  sendFeedback(kRecv, "/record", 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(link.recording(), "record feedback is decoded");
  sendFeedback(kRecv, "/play", 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(!link.playing(), "stop feedback clears play state");
  check(link.online(), "feedback marks the link online");

  link.close();
  ::close(receiver);
  std::printf("%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}

// Suite Control — console table bridge between the LV2 plug-in and its UI.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LV2 state lives in the plug-in, but the console names are typed in the UI,
// which is a separate shared object with its own copy of Shared.  Both sides
// map one small per-process POSIX segment so the plug-in can save the table in
// its state and hand it back on restore.
//
// Linux only, like the peer bus; elsewhere every call is a no-op and names are
// session-only (macOS gets the same treatment as the portable peer transport).
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#if defined(__linux__) && !defined(SUITE_PEER_STUB)
#define SUITE_CONSOLE_NATIVE 1
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#else
#define SUITE_CONSOLE_NATIVE 0
#endif

namespace suite {

struct ConsoleTable {
  static constexpr int kSlots = 9 * 32;
  static constexpr int kBytes = 24;
  char names[kSlots][kBytes];
  uint8_t muted[kSlots];
  uint8_t gainForced[kSlots];
  float savedGain[kSlots];
};

class ConsoleBridge {
 public:
  ConsoleBridge() = default;
  ~ConsoleBridge() { close(); }
  ConsoleBridge(const ConsoleBridge&) = delete;
  ConsoleBridge& operator=(const ConsoleBridge&) = delete;

  bool open() noexcept {
#if SUITE_CONSOLE_NATIVE
    if (seg_) return true;
    char name[64];
    std::snprintf(name, sizeof(name), "/suite-console-%d",
                  static_cast<int>(getpid()));
    reapStale();
    const int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return false;
    if (ftruncate(fd, static_cast<off_t>(sizeof(Segment))) != 0) {
      ::close(fd);
      return false;
    }
    void* p = mmap(nullptr, sizeof(Segment), PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, 0);
    ::close(fd);
    if (p == MAP_FAILED) return false;
    seg_ = static_cast<Segment*>(p);
    const uint64_t token = processToken();
    if (seg_->magic.load(std::memory_order_acquire) != kMagic ||
        seg_->token.load(std::memory_order_acquire) != token) {
      std::memset(&seg_->table, 0, sizeof(seg_->table));
      seg_->token.store(token, std::memory_order_relaxed);
      seg_->seq.store(0, std::memory_order_relaxed);
      seg_->magic.store(kMagic, std::memory_order_release);
    }
    return true;
#else
    return false;
#endif
  }

  bool valid() const noexcept { return seg_ != nullptr; }

  // Seqlock read; false when the writer was mid-update or nothing is mapped.
  bool read(ConsoleTable& out) const noexcept {
#if SUITE_CONSOLE_NATIVE
    if (!seg_) return false;
    for (int attempt = 0; attempt < 16; ++attempt) {
      const uint32_t before = seg_->seq.load(std::memory_order_acquire);
      if (before & 1u) continue;
      std::memcpy(&out, &seg_->table, sizeof(out));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seg_->seq.load(std::memory_order_acquire) == before) return true;
    }
#endif
    return false;
  }

  void write(const ConsoleTable& in) noexcept {
#if SUITE_CONSOLE_NATIVE
    if (!seg_) return;
    const uint32_t current = seg_->seq.load(std::memory_order_relaxed);
    seg_->seq.store(current + 1, std::memory_order_release);  // odd: writing
    std::memcpy(&seg_->table, &in, sizeof(in));
    std::atomic_thread_fence(std::memory_order_release);
    seg_->seq.store(current + 2, std::memory_order_release);  // even: stable
#endif
  }

  // Changes whenever anyone wrote the table (0 before the first write).
  uint32_t sequence() const noexcept {
#if SUITE_CONSOLE_NATIVE
    return seg_ ? seg_->seq.load(std::memory_order_acquire) : 0;
#else
    return 0;
#endif
  }

 private:
#if SUITE_CONSOLE_NATIVE
  struct Segment {
    std::atomic<uint32_t> seq;
    std::atomic<uint32_t> magic;
    std::atomic<uint64_t> token;
    ConsoleTable table;
  };
  static constexpr uint32_t kMagic = 0x53434f4eu;  // SCON

  static uint64_t processToken() noexcept {
    // /proc/self/stat field 22 is the process start time in clock ticks; it
    // makes a recycled PID look different from the previous owner.
    FILE* f = std::fopen("/proc/self/stat", "r");
    if (!f) return 0;
    char buffer[1024];
    const size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, f);
    std::fclose(f);
    buffer[n] = '\0';
    // Skip the comm field, which may contain spaces.
    char* p = std::strrchr(buffer, ')');
    if (!p) return 0;
    ++p;
    unsigned long long value = 0;
    for (int field = 3; field <= 22 && *p; ++field) {
      while (*p == ' ') ++p;
      char* end = nullptr;
      value = std::strtoull(p, &end, 10);
      p = end;
      if (field == 22) return value;
    }
    return 0;
  }

  static void reapStale() noexcept {
    DIR* dir = opendir("/dev/shm");
    if (!dir) return;
    while (dirent* e = readdir(dir)) {
      if (std::strncmp(e->d_name, "suite-console-", 14) != 0) continue;
      const long pid = std::strtol(e->d_name + 14, nullptr, 10);
      if (pid <= 0 || pid == static_cast<long>(getpid())) continue;
      if (kill(static_cast<pid_t>(pid), 0) != 0) {
        char path[128];
        std::snprintf(path, sizeof(path), "/dev/shm/%s", e->d_name);
        ::unlink(path);
      }
    }
    closedir(dir);
  }

  void close() noexcept {
    if (seg_) {
      munmap(seg_, sizeof(Segment));
      seg_ = nullptr;
    }
  }

  Segment* seg_ = nullptr;
#endif
};

} // namespace suite

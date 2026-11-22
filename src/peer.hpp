// Suite peer registry — shared-memory parameter mirror between Suite Channel
// Plus instances and Suite Control.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Both plugins are separate shared objects, so a process-global C++ variable
// cannot be shared between them.  Instead each host process owns one POSIX
// shared-memory segment named by its PID; every plugin that attaches maps the
// same pages and talks through atomics only.  No locks, no allocation and no
// syscalls on the audio thread.
#pragma once

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace suite::peer {

constexpr uint32_t kMagic = 0x53505231u;  // "SPR1"
constexpr uint32_t kVersion = 2;
constexpr int kSlots = 64;
constexpr int kMaxParams = 48;
constexpr int kNameBytes = 32;
constexpr int kBusCount = 8;
constexpr int64_t kStaleMs = 700;

inline int64_t nowMs() noexcept {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

struct Param {
  std::atomic<double> value{0.0};
  std::atomic<uint32_t> gen{0};
};

struct Slot {
  std::atomic<uint32_t> used{0};
  std::atomic<int64_t> beatMs{0};
  std::atomic<uint32_t> bus{0};
  std::atomic<uint32_t> paramCount{0};
  std::atomic<uint32_t> serial{0};
  std::atomic<int32_t> ownerPid{0};
  char name[kNameBytes] = {};
  Param param[kMaxParams];
  std::atomic<float> inPeakL{-120.0f};
  std::atomic<float> inPeakR{-120.0f};
  std::atomic<float> outPeakL{-120.0f};
  std::atomic<float> outPeakR{-120.0f};
  std::atomic<float> grDb{0.0f};
};

struct Segment {
  std::atomic<uint32_t> magic{0};
  std::atomic<uint32_t> version{0};
  std::atomic<uint32_t> ready{0};
  std::atomic<uint32_t> slotCount{0};
  Slot slot[kSlots];
};

inline std::string segmentName() noexcept {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "/suitepeer-v2-%ld",
                static_cast<long>(getpid()));
  return buf;
}

// Remove segments left behind by host processes that no longer exist.
inline void reapStale() noexcept {
  DIR* dir = opendir("/dev/shm");
  if (!dir) return;
  const std::string prefix = "suitepeer-v2-";
  while (dirent* e = readdir(dir)) {
    const char* n = e->d_name;
    if (std::strncmp(n, prefix.c_str(), prefix.size()) != 0) continue;
    const long pid = std::strtol(n + prefix.size(), nullptr, 10);
    if (pid <= 0 || pid == static_cast<long>(getpid())) continue;
    if (kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH) {
      char path[128];
      std::snprintf(path, sizeof(path), "/dev/shm/%s", n);
      unlink(path);
    }
  }
  closedir(dir);
}

class Client {
public:
  Client() = default;
  ~Client() { detach(); }
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool attach() noexcept {
    if (seg_) return true;
    reapStale();
    const std::string name = segmentName();
    fd_ = shm_open(name.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ < 0) return false;
    const size_t size = sizeof(Segment);
    struct stat st {};
    if (fstat(fd_, &st) == 0 && static_cast<size_t>(st.st_size) < size) {
      if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        close();
        return false;
      }
    }
    void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (map == MAP_FAILED) {
      close();
      return false;
    }
    seg_ = static_cast<Segment*>(map);
    // First attach initialises the header; later attaches validate it.
    if (seg_->magic.load(std::memory_order_acquire) != kMagic ||
        seg_->version.load(std::memory_order_acquire) != kVersion) {
      std::memset(seg_, 0, size);
      new (seg_) Segment();
      seg_->version.store(kVersion, std::memory_order_relaxed);
      seg_->slotCount.store(kSlots, std::memory_order_relaxed);
      seg_->magic.store(kMagic, std::memory_order_release);
      // A crashed previous owner can leave a segment whose slots look used.
      for (int i = 0; i < kSlots; ++i) {
        seg_->slot[i].used.store(0, std::memory_order_relaxed);
      }
    }
    seg_->ready.store(1, std::memory_order_release);
    return true;
  }

  void detach() noexcept {
    if (seg_) {
      munmap(seg_, sizeof(Segment));
      seg_ = nullptr;
    }
    close();
  }

  bool attached() const noexcept { return seg_ != nullptr; }
  Segment* raw() const noexcept { return seg_; }

  // ------------------------------------------------------------- publisher
  int registerSlot(uint32_t bus, const char* name, uint32_t paramCount) noexcept {
    if (!attach()) return -1;
    for (int i = 0; i < kSlots; ++i) {
      uint32_t expected = 0;
      if (seg_->slot[i].used.compare_exchange_strong(
              expected, 1, std::memory_order_acq_rel)) {
        Slot& s = seg_->slot[i];
        s.paramCount.store(paramCount, std::memory_order_relaxed);
        s.bus.store(bus, std::memory_order_relaxed);
        s.serial.fetch_add(1, std::memory_order_relaxed);
        s.ownerPid.store(static_cast<int32_t>(getpid()),
                         std::memory_order_relaxed);
        s.beatMs.store(nowMs(), std::memory_order_relaxed);
        std::strncpy(s.name, name ? name : "", kNameBytes - 1);
        s.name[kNameBytes - 1] = '\0';
        for (uint32_t p = 0; p < paramCount; ++p) {
          s.param[p].gen.store(0, std::memory_order_relaxed);
        }
        slot_ = i;
        return i;
      }
    }
    return -1;
  }

  void unregisterSlot() noexcept {
    if (!seg_ || slot_ < 0) return;
    seg_->slot[slot_].used.store(0, std::memory_order_release);
    slot_ = -1;
  }

  void beat() noexcept {
    if (!seg_ || slot_ < 0) return;
    seg_->slot[slot_].beatMs.store(nowMs(), std::memory_order_relaxed);
  }

  void setBus(uint32_t bus) noexcept {
    if (!seg_ || slot_ < 0) return;
    seg_->slot[slot_].bus.store(bus, std::memory_order_relaxed);
  }

  // Publish one parameter if it differs from the shared value.  Stateless on
  // purpose: the audio thread and the editor thread can both call it.
  void publish(int p, double value) noexcept {
    if (!seg_ || slot_ < 0 || p < 0 || p >= kMaxParams) return;
    Slot& s = seg_->slot[slot_];
    if (p >= static_cast<int>(s.paramCount.load(std::memory_order_relaxed)))
      return;
    if (value == s.param[p].value.load(std::memory_order_relaxed)) return;
    s.param[p].value.store(value, std::memory_order_relaxed);
    s.param[p].gen.fetch_add(1, std::memory_order_release);
  }

  // Returns true (and the value) when another plugin changed parameter p.
  bool takeRemote(int p, double* value) noexcept {
    if (!seg_ || slot_ < 0 || p < 0 || p >= kMaxParams) return false;
    Slot& s = seg_->slot[slot_];
    const uint32_t g = s.param[p].gen.load(std::memory_order_acquire);
    if (g == seen_[p]) return false;
    seen_[p] = g;
    const double v = s.param[p].value.load(std::memory_order_relaxed);
    *value = v;
    return true;
  }

  void publishMeters(float inL, float inR, float outL, float outR,
                     float gr) noexcept {
    if (!seg_ || slot_ < 0) return;
    Slot& s = seg_->slot[slot_];
    s.inPeakL.store(inL, std::memory_order_relaxed);
    s.inPeakR.store(inR, std::memory_order_relaxed);
    s.outPeakL.store(outL, std::memory_order_relaxed);
    s.outPeakR.store(outR, std::memory_order_relaxed);
    s.grDb.store(gr, std::memory_order_relaxed);
  }

  // --------------------------------------------------------------- consumer
  Segment* segment() noexcept {
    if (!attach()) return nullptr;
    return seg_;
  }

  // Liveness is the owning process, not the heartbeat: a plug-in that is
  // loaded but not processing (transport stopped, track silent) must still be
  // listed.  The heartbeat is only used to grey out stale meters.
  static bool alive(const Slot& s) noexcept {
    if (s.used.load(std::memory_order_acquire) == 0) return false;
    const int32_t pid = s.ownerPid.load(std::memory_order_relaxed);
    if (pid > 0 && kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
      return false;
    }
    return true;
  }

  static bool beatFresh(const Slot& s) noexcept {
    return nowMs() - s.beatMs.load(std::memory_order_relaxed) < kStaleMs;
  }

  // Writes a parameter into another plugin's slot.
  static bool writeRemote(Slot& s, int p, double value) noexcept {
    if (p < 0 || p >= static_cast<int>(s.paramCount.load(std::memory_order_relaxed)))
      return false;
    s.param[p].value.store(value, std::memory_order_relaxed);
    s.param[p].gen.fetch_add(1, std::memory_order_release);
    return true;
  }

  static void writeBus(Slot& s, uint32_t bus) noexcept {
    s.bus.store(bus, std::memory_order_relaxed);
  }

  static void writeSlotName(Slot& s, const char* name) noexcept {
    if (!name) return;
    std::strncpy(s.name, name, kNameBytes - 1);
    s.name[kNameBytes - 1] = '\0';
  }

private:
  void close() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  Segment* seg_ = nullptr;
  int fd_ = -1;
  int slot_ = -1;
  double mirror_[kMaxParams] = {};
  uint32_t seen_[kMaxParams] = {};
};

} // namespace suite::peer

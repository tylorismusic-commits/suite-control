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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The peer bus is a Linux feature.  Everywhere else (including the macOS
// builds) it compiles to a no-op so the rest of the plug-in is unaffected.
#if defined(__linux__) && !defined(SUITE_PEER_STUB)
#define SUITE_PEER_NATIVE 1
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#define SUITE_PEER_NATIVE 0
#endif

namespace suite::peer {

constexpr uint32_t kMagic = 0x53505231u;  // "SPR1"
constexpr uint32_t kVersion = 6;
constexpr int kSlots = 64;
constexpr int kMaxParams = 128;
constexpr int kNameBytes = 32;
constexpr int kBusCount = 8;
constexpr int kInsertSlots = 4;
constexpr int kMaxLabels = 12;
constexpr int kMaxCurve = 96;
constexpr int kLabelBytes = 12;
constexpr int kDisplayBytes = 20;
constexpr int64_t kStaleMs = 700;

// Slot kinds.
constexpr uint32_t kKindStrip = 0;    // Suite Channel Plus: a console strip
constexpr uint32_t kKindInsert = 1;   // an insert plug-in (Comp, Space, EQ)

// Parameter metadata, published once per instance so Suite Control can render
// an insert page without knowing the plug-in.  Written before metaReady is
// set, read only afterwards, so plain fields are safe here.
struct ParamMeta {
  char symbol[24] = {};
  char name[32] = {};
  char group[16] = {};
  char unit[8] = {};
  float min = 0.0f;
  float max = 1.0f;
  float def = 0.0f;
  uint32_t flags = 0;
  uint32_t labelCount = 0;
  char label[kMaxLabels][kLabelBytes] = {};
  // The real parameter value for each label, for enums whose values are not
  // 0..count-1 (EQ slopes, filter types, ...).  Filled by the plug-in.
  float labelValue[kMaxLabels] = {};
};

// ParamMeta::flags
constexpr uint32_t kMetaStepped = 1u << 0;
constexpr uint32_t kMetaToggle = 1u << 1;
constexpr uint32_t kMetaLog = 1u << 2;
constexpr uint32_t kMetaHidden = 1u << 3;

struct LinkInfo {
  uint32_t bus = 0;
  uint32_t channel = 0;
  uint32_t slot = 0;
};

inline int64_t nowMs() noexcept {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch())
      .count();
}

// One parameter has exactly one publisher (the plug-in that owns the slot) and
// any number of consumers (Suite Control).  Keeping the published value and
// the consumer request in separate fields means a publish can never overwrite
// a request that arrived between the plug-in's read and its write.
struct Param {
  std::atomic<double> value{0.0};       // published by the owner
  std::atomic<uint32_t> gen{0};         // owner publish counter
  std::atomic<double> request{0.0};     // written by consumers
  std::atomic<uint32_t> requestGen{0};  // consumer request counter
  // The owner's own formatted readout ("-11.6 dB", "5.0:1", "76Blue").  Only
  // the plug-in knows its model-dependent mapping, so the generic insert page
  // displays this instead of guessing from min/max.
  std::atomic<uint32_t> textSeq{0};
  char display[kDisplayBytes] = {};
};

struct Slot {
  std::atomic<uint32_t> used{0};
  std::atomic<int64_t> beatMs{0};
  std::atomic<uint32_t> bus{0};
  std::atomic<uint32_t> paramCount{0};
  std::atomic<uint32_t> serial{0};
  std::atomic<int32_t> ownerPid{0};
  std::atomic<uint64_t> instanceId{0};
  std::atomic<uint32_t> kind{kKindStrip};
  std::atomic<uint32_t> metaReady{0};
  std::atomic<uint32_t> bypassParam{0xffffffffu};
  // LINK: written by Suite Control, adopted by the insert plug-in and saved in
  // its own state so the assignment survives a reload.
  std::atomic<uint32_t> linkBus{0};
  std::atomic<uint32_t> linkChannel{0};
  std::atomic<uint32_t> linkSlot{0};
  std::atomic<uint32_t> linkStamp{0};
  char name[kNameBytes] = {};
  char shortName[8] = {};
  // Optional response curve for the insert page (log-spaced 20 Hz..20 kHz).
  std::atomic<uint32_t> curveSeq{0};
  std::atomic<uint32_t> curveCount{0};
  float curve[kMaxCurve] = {};
  // The plug-in publishes a meaningful gain-reduction value.
  std::atomic<uint32_t> hasGr{0};
  ParamMeta meta[kMaxParams];
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

#if SUITE_PEER_NATIVE

inline std::string segmentName() noexcept {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "/suitepeer-v6-%ld",
                static_cast<long>(getpid()));
  return buf;
}

// Remove segments left behind by host processes that no longer exist.
inline void reapStale() noexcept {
  DIR* dir = opendir("/dev/shm");
  if (!dir) return;
  const std::string prefix = "suitepeer-v6-";
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
  int registerSlot(uint32_t bus, const char* name, uint32_t paramCount,
                   uint32_t kind = kKindStrip,
                   int bypassParam = -1) noexcept {
    if (!attach()) return -1;
    if (paramCount > static_cast<uint32_t>(kMaxParams)) return -1;
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
        s.instanceId.store(instanceIdFor(static_cast<uint32_t>(i)),
                           std::memory_order_relaxed);
        s.kind.store(kind, std::memory_order_relaxed);
        s.metaReady.store(0, std::memory_order_relaxed);
        s.bypassParam.store(bypassParam >= 0
                                ? static_cast<uint32_t>(bypassParam)
                                : 0xffffffffu,
                            std::memory_order_relaxed);
        s.linkBus.store(0, std::memory_order_relaxed);
        s.linkChannel.store(0, std::memory_order_relaxed);
        s.linkSlot.store(0, std::memory_order_relaxed);
        s.linkStamp.store(0, std::memory_order_relaxed);
        std::strncpy(s.name, name ? name : "", kNameBytes - 1);
        s.name[kNameBytes - 1] = '\0';
        s.shortName[0] = '\0';
        s.curveCount.store(0, std::memory_order_relaxed);
        s.curveSeq.store(0, std::memory_order_relaxed);
        for (uint32_t p = 0; p < paramCount; ++p) {
          s.param[p].gen.store(0, std::memory_order_relaxed);
          s.param[p].requestGen.store(0, std::memory_order_relaxed);
          s.param[p].textSeq.store(0, std::memory_order_relaxed);
        }
        slot_ = i;
        seenLinkStamp_ = 0;
        return i;
      }
    }
    return -1;
  }

  // Copies the parameter metadata for this instance.  Call after
  // registerSlot and before the first publish.
  void setShortName(const char* text) noexcept {
    if (!seg_ || slot_ < 0 || !text) return;
    std::strncpy(seg_->slot[slot_].shortName, text,
                 sizeof(seg_->slot[slot_].shortName) - 1);
    seg_->slot[slot_].shortName[sizeof(seg_->slot[slot_].shortName) - 1] = '\0';
  }

  // Publishes the plug-in's response curve (dB, log-spaced 20 Hz..20 kHz).
  void setHasGr(bool on = true) noexcept {
    if (!seg_ || slot_ < 0) return;
    seg_->slot[slot_].hasGr.store(on ? 1u : 0u, std::memory_order_relaxed);
  }

  void publishCurve(const float* db, uint32_t count) noexcept {
    if (!seg_ || slot_ < 0 || !db || count == 0) return;
    Slot& s = seg_->slot[slot_];
    const uint32_t n = std::min<uint32_t>(count, kMaxCurve);
    const uint32_t seq = s.curveSeq.load(std::memory_order_relaxed);
    s.curveSeq.store(seq + 1, std::memory_order_release);
    for (uint32_t i = 0; i < n; ++i) s.curve[i] = db[i];
    s.curveCount.store(n, std::memory_order_release);
    s.curveSeq.store(seq + 2, std::memory_order_release);
  }

  void publishMeta(const ParamMeta* meta, uint32_t count) noexcept {
    if (!seg_ || slot_ < 0 || !meta) return;
    Slot& s = seg_->slot[slot_];
    const uint32_t n =
        std::min(count, s.paramCount.load(std::memory_order_relaxed));
    for (uint32_t i = 0; i < n; ++i) s.meta[i] = meta[i];
    s.metaReady.store(n, std::memory_order_release);
  }

  uint64_t id() const noexcept {
    return seg_ && slot_ >= 0 ? seg_->slot[slot_].instanceId.load(
                                    std::memory_order_relaxed)
                              : 0;
  }

  void publishLink(uint32_t bus, uint32_t channel, uint32_t slot) noexcept {
    if (!seg_ || slot_ < 0) return;
    Slot& s = seg_->slot[slot_];
    if (s.linkBus.load(std::memory_order_relaxed) == bus &&
        s.linkChannel.load(std::memory_order_relaxed) == channel &&
        s.linkSlot.load(std::memory_order_relaxed) == slot) {
      return;
    }
    s.linkBus.store(bus, std::memory_order_relaxed);
    s.linkChannel.store(channel, std::memory_order_relaxed);
    s.linkSlot.store(slot, std::memory_order_relaxed);
    s.linkStamp.fetch_add(1, std::memory_order_release);
  }

  // Returns true when Suite Control assigned (or cleared) this insert's link.
  bool takeRemoteLink(LinkInfo* out) noexcept {
    if (!seg_ || slot_ < 0 || !out) return false;
    Slot& s = seg_->slot[slot_];
    const uint32_t stamp = s.linkStamp.load(std::memory_order_acquire);
    if (stamp == seenLinkStamp_) return false;
    seenLinkStamp_ = stamp;
    out->bus = s.linkBus.load(std::memory_order_relaxed);
    out->channel = s.linkChannel.load(std::memory_order_relaxed);
    out->slot = s.linkSlot.load(std::memory_order_relaxed);
    return true;
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

  // Publishes the owner's value.  Never touches the request fields, so a
  // consumer edit that arrived a moment ago is still there to be adopted.
  void publish(int p, double value) noexcept {
    if (!seg_ || slot_ < 0 || p < 0 || p >= kMaxParams) return;
    Slot& s = seg_->slot[slot_];
    if (p >= static_cast<int>(s.paramCount.load(std::memory_order_relaxed)))
      return;
    if (value == s.param[p].value.load(std::memory_order_relaxed)) return;
    s.param[p].value.store(value, std::memory_order_relaxed);
    s.param[p].gen.fetch_add(1, std::memory_order_release);
  }

  // Publishes the owner's formatted readout (seqlock, so a reader never sees a
  // half-updated string).
  void publishText(int p, const char* text) noexcept {
    if (!seg_ || slot_ < 0 || p < 0 || p >= kMaxParams || !text) return;
    Slot& s = seg_->slot[slot_];
    if (p >= static_cast<int>(s.paramCount.load(std::memory_order_relaxed)))
      return;
    char trimmed[kDisplayBytes] = {};
    std::strncpy(trimmed, text, sizeof(trimmed) - 1);
    if (std::strncmp(s.param[p].display, trimmed, sizeof(trimmed)) == 0) {
      return;
    }
    const uint32_t seq =
        s.param[p].textSeq.load(std::memory_order_relaxed);
    s.param[p].textSeq.store(seq + 1, std::memory_order_release);
    std::memcpy(s.param[p].display, trimmed, sizeof(trimmed));
    s.param[p].textSeq.store(seq + 2, std::memory_order_release);
  }

  // Returns true (and the value) when a consumer asked for a new value.
  bool takeRemote(int p, double* value) noexcept {
    if (!seg_ || slot_ < 0 || p < 0 || p >= kMaxParams) return false;
    Slot& s = seg_->slot[slot_];
    const uint32_t g = s.param[p].requestGen.load(std::memory_order_acquire);
    if (g == seen_[p]) return false;
    seen_[p] = g;
    *value = s.param[p].request.load(std::memory_order_relaxed);
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

  // Asks the owner for a new value.  The owner adopts it and publishes it
  // again, which is what the surface displays, so the request field is never
  // overwritten by the owner's own publishing.
  static bool writeRemote(Slot& s, int p, double value) noexcept {
    if (p < 0 || p >= static_cast<int>(s.paramCount.load(std::memory_order_relaxed)))
      return false;
    s.param[p].request.store(value, std::memory_order_relaxed);
    s.param[p].requestGen.fetch_add(1, std::memory_order_release);
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

  static uint64_t instanceIdFor(uint32_t slotIndex) noexcept {
    static std::atomic<uint32_t> counter{0};
    const uint64_t pid = static_cast<uint64_t>(getpid()) & 0xffffffffu;
    const uint64_t unique =
        (static_cast<uint64_t>(nowMs()) ^
         (static_cast<uint64_t>(slotIndex) << 24) ^
         static_cast<uint64_t>(counter.fetch_add(1)) * 0x9e3779b9u) &
        0xffffffffu;
    const uint64_t id = (pid << 32) | unique;
    return id ? id : 1;
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
  uint32_t seenLinkStamp_ = 0;
};


// ---------------------------------------------------------------------------
// Consumer hub
// ---------------------------------------------------------------------------
// Every host process (and every plug-in process, when the host isolates them)
// owns its own segment.  The surface scans /dev/shm and aggregates all live
// segments, so it works whether Suite Channel Plus runs in this process or in
// a dedicated one.
class Hub {
public:
  struct Entry {
    Segment* seg = nullptr;
    int slot = -1;
    int serial = 0;
  };

  void refresh() noexcept {
    const int64_t now = nowMs();
    if (now - lastScan_ < 250 && !entries_.empty()) return;
    lastScan_ = now;
    reapStale();
    // Drop segments that disappeared (host exited, stale cleanup).
    DIR* dir = opendir("/dev/shm");
    std::vector<std::string> names;
    if (dir) {
      const std::string prefix = "suitepeer-v6-";
      while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, prefix.c_str(), prefix.size()) == 0) {
          names.push_back(std::string("/dev/shm/") + e->d_name);
        }
      }
      closedir(dir);
    }
    // Keep existing mappings, add new ones.
    for (const std::string& path : names) {
      bool have = false;
      for (const Map& m : maps_) {
        if (m.path == path) {
          have = true;
          break;
        }
      }
      if (have) continue;
      const int fd = open(path.c_str(), O_RDWR);
      if (fd < 0) continue;
      struct stat st {};
      if (fstat(fd, &st) != 0 ||
          static_cast<size_t>(st.st_size) < sizeof(Segment)) {
        ::close(fd);
        continue;
      }
      void* p = mmap(nullptr, sizeof(Segment), PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
      ::close(fd);
      if (p == MAP_FAILED) continue;
      auto* seg = static_cast<Segment*>(p);
      if (seg->magic.load(std::memory_order_acquire) != kMagic ||
          seg->version.load(std::memory_order_acquire) != kVersion ||
          seg->ready.load(std::memory_order_acquire) == 0) {
        munmap(p, sizeof(Segment));
        continue;
      }
      maps_.push_back({seg, path});
    }

    entries_.clear();
    for (const Map& m : maps_) {
      for (int i = 0; i < kSlots; ++i) {
        if (!Client::alive(m.seg->slot[i])) continue;
        Entry e;
        e.seg = m.seg;
        e.slot = i;
        e.serial = static_cast<int>(
            m.seg->slot[i].serial.load(std::memory_order_relaxed));
        entries_.push_back(e);
      }
    }
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry& a, const Entry& b) {
                       if (a.serial != b.serial) return a.serial < b.serial;
                       return a.slot < b.slot;
                     });
  }

  int size() const noexcept { return static_cast<int>(entries_.size()); }
  int segments() const noexcept { return static_cast<int>(maps_.size()); }
  int rawSlots() const noexcept {
    int n = 0;
    for (const Map& m : maps_) {
      for (int i = 0; i < kSlots; ++i) {
        if (Client::alive(m.seg->slot[i])) ++n;
      }
    }
    return n;
  }
  const Entry& entry(int i) const { return entries_[static_cast<size_t>(i)]; }

  uint32_t bus(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].bus.load(std::memory_order_relaxed);
  }
  int ownerPid(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].ownerPid.load(std::memory_order_relaxed);
  }
  uint32_t paramCount(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].paramCount.load(std::memory_order_relaxed);
  }
  uint64_t instanceId(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].instanceId.load(std::memory_order_relaxed);
  }
  uint32_t kind(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].kind.load(std::memory_order_relaxed);
  }
  int bypassParam(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    const uint32_t p =
        e.seg->slot[e.slot].bypassParam.load(std::memory_order_relaxed);
    return p == 0xffffffffu ? -1 : static_cast<int>(p);
  }
  bool metaReady(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].metaReady.load(std::memory_order_acquire) > 0;
  }
  const ParamMeta& meta(int i, int param) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].meta[param];
  }
  // Copies the owner's formatted readout; empty when none was published.
  void text(int i, int param, char* out, size_t size) const noexcept {
    if (!out || size == 0) return;
    out[0] = '\0';
    const Entry& e = entries_[static_cast<size_t>(i)];
    const Param& p = e.seg->slot[e.slot].param[param];
    for (int attempt = 0; attempt < 8; ++attempt) {
      const uint32_t before = p.textSeq.load(std::memory_order_acquire);
      if (before & 1u) continue;
      char copy[kDisplayBytes] = {};
      std::memcpy(copy, p.display, sizeof(copy));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (p.textSeq.load(std::memory_order_acquire) == before) {
        std::strncpy(out, copy, size - 1);
        out[size - 1] = '\0';
        return;
      }
    }
  }

  LinkInfo link(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    const Slot& s = e.seg->slot[e.slot];
    return {s.linkBus.load(std::memory_order_relaxed),
            s.linkChannel.load(std::memory_order_relaxed),
            s.linkSlot.load(std::memory_order_relaxed)};
  }
  void setLink(int i, uint32_t bus, uint32_t channel, uint32_t slot) noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    Slot& s = e.seg->slot[e.slot];
    if (s.linkBus.load(std::memory_order_relaxed) == bus &&
        s.linkChannel.load(std::memory_order_relaxed) == channel &&
        s.linkSlot.load(std::memory_order_relaxed) == slot) {
      return;
    }
    s.linkBus.store(bus, std::memory_order_relaxed);
    s.linkChannel.store(channel, std::memory_order_relaxed);
    s.linkSlot.store(slot, std::memory_order_relaxed);
    s.linkStamp.fetch_add(1, std::memory_order_release);
  }
  double value(int i, int param) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].param[param].value.load(std::memory_order_relaxed);
  }
  bool write(int i, int param, double v) noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return Client::writeRemote(e.seg->slot[e.slot], param, v);
  }
  const char* shortName(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].shortName;
  }
  bool hasGr(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].hasGr.load(std::memory_order_relaxed) != 0;
  }
  uint32_t curveCount(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].curveCount.load(std::memory_order_acquire);
  }
  // Seqlock copy of the response curve; returns the number of points copied.
  uint32_t curve(int i, float* out, uint32_t capacity) const noexcept {
    if (!out || capacity == 0) return 0;
    const Entry& e = entries_[static_cast<size_t>(i)];
    const Slot& s = e.seg->slot[e.slot];
    for (int attempt = 0; attempt < 8; ++attempt) {
      const uint32_t before = s.curveSeq.load(std::memory_order_acquire);
      if (before & 1u) continue;
      const uint32_t count =
          std::min(s.curveCount.load(std::memory_order_relaxed), capacity);
      for (uint32_t p = 0; p < count; ++p) out[p] = s.curve[p];
      std::atomic_thread_fence(std::memory_order_acquire);
      if (s.curveSeq.load(std::memory_order_acquire) == before) return count;
    }
    return 0;
  }
  const char* name(int i) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    return e.seg->slot[e.slot].name;
  }
  void setName(int i, const char* value) noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    Client::writeSlotName(e.seg->slot[e.slot], value);
  }
  void meter(int i, float* inL, float* inR, float* outL, float* outR,
             float* gr) const noexcept {
    const Entry& e = entries_[static_cast<size_t>(i)];
    const Slot& s = e.seg->slot[e.slot];
    *inL = s.inPeakL.load(std::memory_order_relaxed);
    *inR = s.inPeakR.load(std::memory_order_relaxed);
    *outL = s.outPeakL.load(std::memory_order_relaxed);
    *outR = s.outPeakR.load(std::memory_order_relaxed);
    *gr = s.grDb.load(std::memory_order_relaxed);
  }

private:
  struct Map {
    Segment* seg = nullptr;
    std::string path;
  };
  std::vector<Map> maps_;
  std::vector<Entry> entries_;
  int64_t lastScan_ = 0;
};

#else
// ---------------------------------------------------------------- no-op stubs
class Client {
public:
  bool attach() noexcept { return false; }
  void detach() noexcept {}
  bool attached() const noexcept { return false; }
  Segment* raw() const noexcept { return nullptr; }
  int registerSlot(uint32_t, const char*, uint32_t, uint32_t = kKindStrip,
                   int = -1) noexcept {
    return -1;
  }
  void publishMeta(const ParamMeta*, uint32_t) noexcept {}
  void publishText(int, const char*) noexcept {}
  void setShortName(const char*) noexcept {}
  void publishCurve(const float*, uint32_t) noexcept {}
  void setHasGr(bool = true) noexcept {}
  uint64_t id() const noexcept { return 0; }
  void publishLink(uint32_t, uint32_t, uint32_t) noexcept {}
  bool takeRemoteLink(LinkInfo*) noexcept { return false; }
  void unregisterSlot() noexcept {}
  void beat() noexcept {}
  void setBus(uint32_t) noexcept {}
  void publish(int, double) noexcept {}
  bool takeRemote(int, double*) noexcept { return false; }
  void publishMeters(float, float, float, float, float) noexcept {}
  Segment* segment() const noexcept { return nullptr; }
  static bool alive(const Slot&) noexcept { return false; }
  static bool beatFresh(const Slot&) noexcept { return false; }
  static bool writeRemote(Slot&, int, double) noexcept { return false; }
  static void writeBus(Slot&, uint32_t) noexcept {}
  static void writeSlotName(Slot&, const char*) noexcept {}
};

class Hub {
public:
  struct Entry {
    Segment* seg = nullptr;
    int slot = -1;
    int serial = 0;
  };
  void refresh() noexcept {}
  int size() const noexcept { return 0; }
  int segments() const noexcept { return 0; }
  int rawSlots() const noexcept { return 0; }
  const Entry& entry(int) const {
    static const Entry empty;
    return empty;
  }
  uint32_t bus(int) const noexcept { return 0; }
  uint32_t paramCount(int) const noexcept { return 0; }
  int ownerPid(int) const noexcept { return 0; }
  uint64_t instanceId(int) const noexcept { return 0; }
  uint32_t kind(int) const noexcept { return kKindStrip; }
  int bypassParam(int) const noexcept { return -1; }
  bool metaReady(int) const noexcept { return false; }
  const ParamMeta& meta(int, int) const noexcept {
    static const ParamMeta empty;
    return empty;
  }
  void text(int, int, char* out, size_t size) const noexcept {
    if (out && size) out[0] = '\0';
  }
  LinkInfo link(int) const noexcept { return {}; }
  void setLink(int, uint32_t, uint32_t, uint32_t) noexcept {}
  double value(int, int) const noexcept { return 0.0; }
  bool write(int, int, double) noexcept { return false; }
  void meter(int, float* inL, float* inR, float* outL, float* outR,
             float* gr) const noexcept {
    if (inL) *inL = -120.0f;
    if (inR) *inR = -120.0f;
    if (outL) *outL = -120.0f;
    if (outR) *outR = -120.0f;
    if (gr) *gr = 0.0f;
  }
  const char* name(int) const noexcept { return ""; }
  const char* shortName(int) const noexcept { return ""; }
  uint32_t curveCount(int) const noexcept { return 0; }
  bool hasGr(int) const noexcept { return false; }
  uint32_t curve(int, float* out, uint32_t capacity) const noexcept {
    (void)out;
    (void)capacity;
    return 0;
  }
  void setName(int, const char*) noexcept {}
};

#endif

} // namespace suite::peer

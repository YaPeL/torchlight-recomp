// Frame times, always measured and summarised in the log every 10 s, with or without the live mode,
// so the two can be compared:
// - the guest's (swap to swap): how fast the game itself runs; a frame past kLongFrameMs gets its
//   own "long frame:" line with what the guest did in it (guest_events.h; in the native mode the
//   live mode's "slow frame" line has the backend's side);
// - in the native mode, what reaches the screen (present to present, the backend thread) and the
//   guest frames dropped because the backend was still busy with an earlier one: when the backend
//   is the limit these are fewer than the guest's.
// And the game's level loads, timed by the level load hook (achievements/guest_hooks.cpp).
// docs/performance-profile.md uses them.

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace torchlight::live {

// One series of frame times: a 10 s summary for the log and the last kRecent seconds for the frame
// counter. Add and TakeSummary from one thread; Recent from any.
class FrameSeries {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::chrono::seconds kRecent{10};
  static constexpr std::chrono::seconds kSummaryPeriod{10};

  // A frame that ended at `now`, `dropped` frames skipped before it (counted, not timed). The
  // first call only starts the series. Returns the frame's time (ms), if there was one.
  std::optional<double> Add(Clock::time_point now, size_t dropped = 0);
  // Every kSummaryPeriod: the period's summary ("N frames, mean ... ms (... fps), p50 ..., p95 ...,
  // p99 ..., max ..., over 33 ms N, over 50 ms N"), and the frames dropped in it; then the period
  // starts over.
  std::optional<std::string> TakeSummary(Clock::time_point now, size_t* dropped = nullptr);
  // The frame times (ms) of the last kRecent, oldest first, and the frames dropped in them.
  std::vector<float> Recent(size_t* dropped = nullptr);

 private:
  Clock::time_point last_{}, period_start_{};
  std::vector<double> period_ms_;
  size_t period_dropped_ = 0;
  std::mutex recent_mutex_;
  struct Entry {
    Clock::time_point time;
    float ms;
    size_t dropped;
  };
  std::deque<Entry> recent_;
};

// A presented frame's latency: from the guest's swap that cut it to the return of its present, in
// three parts. In the frame queue until the backend took it; the backend's work up to the present
// (resources, commands, draws); the present itself, which with vsync waits for the display. What
// happens after the present returns (the driver's queue, the scanout) is not measured.
struct PresentLatency {
  double queue_ms = 0, render_ms = 0, present_ms = 0;
  double total_ms() const { return queue_ms + render_ms + present_ms; }
};
// The mean of each part over some frames, for the frame counter and the measurement scripts.
struct LatencyMeans {
  double total_ms = 0, queue_ms = 0, render_ms = 0, present_ms = 0;
  size_t frames = 0;
};
LatencyMeans MeanLatency(const std::vector<PresentLatency>& frames);

class FrameTiming {
 public:
  static FrameTiming& Get();
  static constexpr std::chrono::seconds kRecent = FrameSeries::kRecent;
  // Guest render thread, at every device swap.
  void OnSwap();
  // Live mode's backend thread, after every present that reached the window: `dropped` guest
  // frames were skipped since the previous one (the backend was behind); `latency` is the
  // presented frame's.
  void OnPresent(size_t dropped, const PresentLatency& latency = {});
  // The last kRecent seconds, oldest first, for the frame counter (any thread).
  std::vector<float> Recent() { return guest_.Recent(); }
  // Empty unless frames are being presented (the native mode).
  std::vector<float> RecentPresented(size_t* dropped) { return presented_.Recent(dropped); }
  // The presented frames' latency over the last `window` (any thread); no frames outside the native
  // mode.
  LatencyMeans RecentLatency(std::chrono::milliseconds window);
  // Backend thread: a frame reached the screen `ms` after the guest's swap that cut it (the window
  // system's presentation time, platform/presentation_time.h; development builds on Wayland).
  // `gap_ms`: since the screen's previous frame (0 for the first); past one refresh and a half, a
  // refresh showed the previous frame again (a stutter).
  void OnScreen(double ms, double gap_ms = 0);
  // The mean swap-to-screen latency over the last `window` and how many frames; 0 when unknown.
  double RecentScreenLatency(std::chrono::milliseconds window, size_t* frames = nullptr);

 private:
  FrameTiming() = default;
  FrameSeries guest_, presented_;
  std::mutex latency_mutex_;
  struct LatencyEntry {
    FrameSeries::Clock::time_point time;
    PresentLatency latency;
  };
  std::deque<LatencyEntry> latency_;  // the last kRecent
  std::deque<std::pair<FrameSeries::Clock::time_point, double>> screen_;  // the last kRecent
};

// "level load: N ms" in the log: one call of the guest's level load (guest_abi kLevelLoad).
void LogLevelLoad(std::chrono::milliseconds duration);
// The guest's level load starting (on the guest thread) and whether one is under way: until it
// ends (LogLevelLoad) the game draws no frames, and the live mode uses the backend thread's idle
// time for program precompilation (tl_backend_precompile_programs).
void LevelLoadStarted();
bool LevelLoading();

// For the development tools that follow the game frame by frame (dev/script_input.cpp): called on
// the threads FrameTiming's own calls come from (the guest's render thread at its swap, the live
// mode's backend thread after a present, the guest thread that loaded a level).
class FrameObserver {
 public:
  virtual ~FrameObserver() = default;
  // Every guest swap; `ms` is the frame's time (0 for the first).
  virtual void OnGuestFrame(double ms) = 0;
  virtual void OnPresentedFrame(double ms, size_t dropped, const PresentLatency& latency) = 0;
  // A frame reached the screen `ms` after its guest swap (FrameTiming::OnScreen).
  virtual void OnScreenFrame(double /*ms*/, double /*gap_ms*/) {}
  virtual void OnLevelLoaded() = 0;
};
// Before the guest runs; null removes it.
void SetFrameObserver(FrameObserver* observer);

}  // namespace torchlight::live

#include "session_replay.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "backend/backend_api.h"
#include "frontend/frontend.h"
#include "frontend/zip_archive.h"
#include "live/frame_step.h"
#include "live/live_content_source.h"
#include "live/session_file.h"
#include "png.h"

namespace torchlight::replay {

namespace {

// The live mode's output size until the recording's main target says otherwise (the frontend
// then sets the backend's guest size, as in the live mode: other aspect ratios).
constexpr uint32_t kWidth = 1280, kHeight = 720;

struct Output {
  uint32_t width, height;
  std::vector<uint8_t> rgba;
};

Output ReadOutput(tl_backend* backend, const frontend::Frontend& frontend) {
  Output o{frontend.guest_width() ? frontend.guest_width() : kWidth,
           frontend.guest_height() ? frontend.guest_height() : kHeight, {}};
  o.rgba.resize(size_t(o.width) * o.height * 4);
  tl_backend_read_rgba(backend, o.rgba.data(), o.width * 4);
  for (size_t i = 3; i < o.rgba.size(); i += 4) o.rgba[i] = 255;
  return o;
}

}  // namespace

int ReplaySession(const SessionOptions& o) {
  std::string error;
  // Frame count first, so the default report frame is the last one.
  uint64_t total = 0;
  {
    live::SessionReader counter;
    if (!counter.Open(o.path, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    live::LiveFrame f;
    while (counter.Next(f, error)) ++total;
    if (!error.empty()) std::fprintf(stderr, "warning: %s (after %llu frames)\n", error.c_str(),
                                     (unsigned long long)total);
  }
  if (total == 0) {
    std::fprintf(stderr, "error: the recording has no frames\n");
    return 1;
  }
  uint64_t report_frame = std::min(o.frame.value_or(total - 1), total - 1);

  char err[512] = {};
  tl_backend* backend =
      tl_backend_create(o.render_system, o.gpu.c_str(), kWidth, kHeight, "tl_backend_ogre.log",
                        nullptr, err, sizeof(err));
  if (!backend) {
    std::fprintf(stderr, "backend: %s\n", err);
    return 1;
  }
  frontend::ZipArchive pak;
  if (!pak.Open(o.data_root + "/pak.zip", error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  live::LiveContentSource content(pak);
  frontend::Frontend frontend(backend, content);
  std::map<std::pair<uint32_t, uint32_t>, commands::TextureDesc> render_targets;
  std::ostringstream region_report;

  live::SessionReader reader;
  reader.Open(o.path, error);
  live::LiveFrame frame;
  uint64_t index = 0, dropped = 0;
  uint64_t report_swap = 0;
  bool reported = false;
  live::CommandCounts command_counts{};
  while (index <= report_frame && reader.Next(frame, error)) {
    dropped += frame.dropped_before;
    for (const auto& t : frame.textures)
      if (t.render_target) render_targets[{t.id.guest_address, t.id.generation}] = t;
    for (const auto& id : frame.destroyed) render_targets.erase({id.guest_address, id.generation});
    bool is_report = index == report_frame;
    if (is_report) {
      report_swap = frame.swap;
      if (!o.draw_ranges.empty()) {
        frontend.set_draw_filter([&](size_t d) {
          return std::any_of(o.draw_ranges.begin(), o.draw_ranges.end(),
                             [&](auto& r) { return d >= r.first && d <= r.second; });
        });
      }
      if (o.region) {
        const auto& r = *o.region;
        tl_backend_set_probe(backend, 1, r[0], r[1], r[2], r[3]);
        frontend.set_draw_observer([&](size_t d) {
          uint32_t covered = tl_backend_probe_samples(backend);
          if (covered)
            region_report << "  d" << d << ": " << covered << " fragments"
                          << frontend.DescribeCurrentDraw() << "\n";
        });
      }
    }
    live::FrameStep step = live::RunFrameStep(frame, content, frontend, [&] {
      bool png = o.png_frames && index >= o.png_frames->first &&
                 index <= o.png_frames->second && (index - o.png_frames->first) % o.png_step == 0;
      if (png) {
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%06llu.png", (unsigned long long)index);
        const Output out = ReadOutput(backend, frontend);
        WritePng(o.out_dir + name, out.width, out.height, out.rgba);
      }
      if (!is_report) return;
      const Output out = ReadOutput(backend, frontend);
      WritePng(o.out_dir + "/render.png", out.width, out.height, out.rgba);
      if (o.dump_targets) {
        for (const auto& [key, t] : render_targets) {
          if (!frontend.TextureReady(t.id)) continue;
          std::vector<uint8_t> rgba(size_t(t.width) * t.height * 4);
          if (tl_backend_read_target_rgba(backend, frontend::PackId(t.id), t.width, t.height,
                                          rgba.data(), t.width * 4) == 0) {
            WritePng(o.out_dir + "/target_" + t.name + ".png", t.width, t.height, rgba);
          }
        }
      }
      reported = true;
    });
    live::AddCommandCounts(command_counts, step);
    ++index;
  }
  if (!error.empty()) std::fprintf(stderr, "error: %s\n", error.c_str());

  const auto& stats = frontend.stats();
  std::ostringstream report;
  report << "session: " << o.path << "\n";
  report << "renderer: " << tl_backend_renderer(backend) << "\n";
  report << "frames: " << total << " recorded, " << index << " replayed (" << dropped
         << " dropped by the live mode before them); report frame " << report_frame
         << " (guest swap " << report_swap << ")" << (reported ? "" : " NOT REACHED") << "\n";
  report << "draws over the replayed frames: " << stats.draws_total << " total, "
         << stats.draws_drawn << " drawn, " << stats.skipped.total() << " skipped\n";
  for (const auto& [k, v] : stats.skipped.counts) report << "  skipped  " << v << "  " << k << "\n";
  for (const auto& [k, v] : stats.degraded.counts) report << "  degraded " << v << "  " << k << "\n";
  for (const auto& [k, v] : stats.texture_problems.counts)
    report << "  problem  " << v << "  " << k << "\n";
  report << "commands consumed: " << live::FormatCommandCounts(command_counts, index) << "\n";
  report << "images: " << o.out_dir << "/render.png (report frame)\n";
  if (o.region) {
    const auto& r = *o.region;
    report << "draws of the report frame covering region " << r[0] << "," << r[1] << "-" << r[2]
           << "," << r[3] << " of the main target (fragments, occluded included):\n"
           << region_report.str();
  }
  std::printf("%s", report.str().c_str());
  if (FILE* f = std::fopen((o.out_dir + "/report.txt").c_str(), "w")) {
    std::fputs(report.str().c_str(), f);
    std::fclose(f);
  }
  tl_backend_destroy(backend);
  return reported ? 0 : 1;
}

namespace {

// Frames read ahead of the bench's backend thread, as the live mode's queue hands them over
// (decompressing and deserializing them is the producer's side, not timed). Bounded: the reader
// waits when it is kAhead frames ahead.
class ReadAhead {
 public:
  static constexpr size_t kAhead = 4;
  ReadAhead(const std::string& path, uint64_t last) : last_(last) {
    std::string error;
    if (!reader_.Open(path, error)) {
      error_ = error;
      done_ = true;
      return;
    }
    thread_ = std::thread([this] { Run(); });
  }
  ~ReadAhead() {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    changed_.notify_all();
    if (thread_.joinable()) thread_.join();
  }
  // The next frame; null at the end (error() says whether the recording was malformed).
  std::unique_ptr<live::LiveFrame> Pop() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [&] { return !frames_.empty() || done_; });
    if (frames_.empty()) return nullptr;
    auto frame = std::move(frames_.front());
    frames_.pop_front();
    changed_.notify_all();
    return frame;
  }
  std::string error() {
    std::lock_guard lock(mutex_);
    return error_;
  }

 private:
  void Run() {
    for (uint64_t index = 0; index <= last_; ++index) {
      auto frame = std::make_unique<live::LiveFrame>();
      std::string error;
      bool read = reader_.Next(*frame, error);
      std::unique_lock lock(mutex_);
      if (!read) {
        error_ = error;
        break;
      }
      changed_.wait(lock, [&] { return frames_.size() < kAhead || stop_; });
      if (stop_) break;
      frames_.push_back(std::move(frame));
      changed_.notify_all();
    }
    std::lock_guard lock(mutex_);
    done_ = true;
    changed_.notify_all();
  }

  const uint64_t last_;
  live::SessionReader reader_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::unique_ptr<live::LiveFrame>> frames_;
  bool done_ = false, stop_ = false;
  std::string error_;
  std::thread thread_;
};

struct BenchFrame {
  uint64_t index = 0, swap = 0;
  uint32_t commands = 0;
  double content = 0, commands_ms = 0, draws = 0, end = 0, present = 0, release = 0, free_ms = 0,
         total = 0, interval = 0;
};

double Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  size_t i = std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5));
  return v[i];
}

}  // namespace

int BenchSession(const SessionOptions& o, std::optional<std::pair<uint64_t, uint64_t>> timed) {
  using Clock = std::chrono::steady_clock;
  auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
  const uint64_t first = timed ? timed->first : 0;
  const uint64_t last = timed ? timed->second : UINT64_MAX;

  char err[512] = {};
  tl_backend* backend =
      tl_backend_create(o.render_system, o.gpu.c_str(), kWidth, kHeight, "tl_backend_ogre.log",
                        "torchlight replay bench", err, sizeof(err));
  if (!backend) {
    std::fprintf(stderr, "backend: %s\n", err);
    return 1;
  }
  tl_backend_set_vsync(backend, 0);
  std::string error;
  frontend::ZipArchive pak;
  if (!pak.Open(o.data_root + "/pak.zip", error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  live::LiveContentSource content(pak);
  frontend::Frontend frontend(backend, content);

  std::vector<BenchFrame> frames;
  bool window_open = true;
  uint64_t index = 0;
  Clock::time_point last_present{};
  {
    ReadAhead ahead(o.path, last);
    while (window_open) {
      auto frame = ahead.Pop();
      if (!frame) break;
      const auto start = Clock::now();
      BenchFrame b;
      b.index = index;
      b.swap = frame->swap;
      b.commands = uint32_t(frame->commands.size());
      Clock::time_point present_end{};
      live::FrameStep step = live::RunFrameStep(*frame, content, frontend, [&] {
        window_open = tl_backend_present(backend) != 0;
        present_end = Clock::now();
      });
      const auto free_start = Clock::now();
      frame.reset();  // the live mode frees the consumed frame on the backend thread too
      const auto end = Clock::now();
      b.content = step.content_ms;
      b.commands_ms = step.commands_ms;
      b.draws = step.draws_ms;
      b.end = step.end_ms;
      b.present = step.after_ms;
      b.release = step.release_ms;
      b.free_ms = ms(end - free_start);
      b.total = ms(end - start);
      b.interval = last_present.time_since_epoch().count() ? ms(present_end - last_present) : 0;
      last_present = present_end;
      if (index >= first) frames.push_back(b);
      if (++index % 500 == 0) std::fprintf(stderr, "bench: %llu frames played\n",
                                           (unsigned long long)index);
    }
    error = ahead.error();
  }
  if (!error.empty()) std::fprintf(stderr, "error: %s\n", error.c_str());
  if (!window_open) std::fprintf(stderr, "the window was closed at frame %llu\n",
                                 (unsigned long long)index);

  if (FILE* f = std::fopen((o.out_dir + "/bench.csv").c_str(), "w")) {
    std::fputs("frame,swap,commands,content_ms,commands_ms,draws_ms,end_ms,present_ms,release_ms,"
               "free_ms,total_ms,present_interval_ms\n", f);
    for (const auto& b : frames) {
      std::fprintf(f, "%llu,%llu,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                   (unsigned long long)b.index, (unsigned long long)b.swap, b.commands, b.content,
                   b.commands_ms, b.draws, b.end, b.present, b.release, b.free_ms, b.total,
                   b.interval);
    }
    std::fclose(f);
  }
  std::ostringstream report;
  report << "session: " << o.path << "\n";
  report << "renderer: " << tl_backend_renderer(backend) << "\n";
  if (frames.empty()) {
    report << "no frames timed (" << index << " played)\n";
  } else {
    // Frame time: present to present, as the live mode's presented series (the first timed
    // frame's interval reaches back into the untimed ones, so it counts too unless it is frame 0).
    std::vector<double> interval, total;
    double sum[8] = {};
    for (const auto& b : frames) {
      if (b.interval > 0) interval.push_back(b.interval);
      total.push_back(b.total);
      const double parts[8] = {b.content, b.commands_ms, b.draws, b.end,
                               b.present, b.release,     b.free_ms, b.total};
      for (int i = 0; i < 8; ++i) sum[i] += parts[i];
    }
    const double n = double(frames.size());
    double mean_interval = 0;
    for (double v : interval) mean_interval += v;
    mean_interval /= std::max<size_t>(1, interval.size());
    const double p99 = Percentile(interval, 0.99);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "frames timed: %zu (%llu-%llu of %llu played)\n"
                  "presented: mean %.3f ms (%.1f fps), p95 %.3f, p99 %.3f (1 %% low %.1f fps), "
                  "max %.3f\n"
                  "per frame (ms): content and textures %.3f, commands %.3f (backend draws %.3f), "
                  "frame end %.3f, present %.3f, content release %.3f, frame freed %.3f; "
                  "total %.3f\n",
                  frames.size(), (unsigned long long)frames.front().index,
                  (unsigned long long)frames.back().index, (unsigned long long)index,
                  mean_interval, mean_interval > 0 ? 1000 / mean_interval : 0,
                  Percentile(interval, 0.95), p99, p99 > 0 ? 1000 / p99 : 0,
                  interval.empty() ? 0 : *std::max_element(interval.begin(), interval.end()),
                  sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n, sum[4] / n, sum[5] / n,
                  sum[6] / n, sum[7] / n);
    report << line;
  }
  report << "every frame: " << o.out_dir << "/bench.csv\n";
  std::printf("%s", report.str().c_str());
  if (FILE* f = std::fopen((o.out_dir + "/bench.txt").c_str(), "w")) {
    std::fputs(report.str().c_str(), f);
    std::fclose(f);
  }
  tl_backend_destroy(backend);
  return error.empty() && !frames.empty() ? 0 : 1;
}

}  // namespace torchlight::replay

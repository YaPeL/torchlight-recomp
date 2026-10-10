#include "live/live_mode.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <rex/logging.h>

#include "backend/backend_api.h"
#include "capture/session.h"
#include "frontend/frontend.h"
#include "frontend/zip_archive.h"
#include "live/frame_reclaimer.h"
#include "live/frame_step.h"
#include "live/frame_timing.h"
#include "live/live_content_source.h"
#include "live/measured_mutex.h"
#include "live/slow_frames.h"

namespace torchlight::live {

namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

struct Series {
  std::vector<double> values;
  void Add(double v) { values.push_back(v); }
  std::string Summary() {
    if (values.empty()) return "n/a";
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double v : values) sum += v;
    auto pct = [&](double p) { return values[std::min(values.size() - 1, size_t(p * values.size()))]; };
    char buf[128];
    std::snprintf(buf, sizeof(buf), "mean %.2f ms, p95 %.2f, p99 %.2f, max %.2f", sum / values.size(),
                  pct(0.95), pct(0.99), values.back());
    values.clear();
    return buf;
  }
};

// Counts since the previous summary.
std::string Delta(const frontend::Counter& now, std::map<std::string, size_t>& before) {
  std::string out;
  for (const auto& [reason, count] : now.counts) {
    size_t d = count - before[reason];
    if (d) out += "\n    " + std::to_string(d) + "  " + reason;
    before[reason] = count;
  }
  return out.empty() ? " none" : out;
}

// The period's producer cost per hook (per frame measured, `measured`), lock contention and what
// came back from the GPU side (per live frame cut, `cuts`).
void LogMeasurements(uint64_t cuts, uint64_t measured) {
  auto& session = capture::Session::Get();
  double frames = double(std::max<uint64_t>(cuts, 1));
  double measured_frames = double(std::max<uint64_t>(measured, 1));
  struct Row {
    uint32_t hook;
    uint64_t calls, ns;
  };
  std::vector<Row> rows;
  for (uint32_t i = 0; i < capture::kHookCount; ++i) {
    auto& cost = session.hook_costs()[i];
    uint64_t calls = cost.calls.exchange(0), ns = cost.ns.exchange(0);
    if (calls) rows.push_back({i, calls, ns});
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ns > b.ns; });
  std::string hooks;
  if (!session.producer_timing()) hooks = " not timed (--native_producer_timing)";
  for (size_t i = 0; i < rows.size() && i < 8; ++i) {
    hooks += fmt::format("\n    {:.3f} ms  {:7.1f} calls  {}", rows[i].ns / 1e6 / measured_frames,
                         rows[i].calls / measured_frames, capture::HookName(rows[i].hook));
  }
  std::string locks;
  for (const auto& m : TakeMutexSummaries()) {
    if (!m.acquisitions) continue;
    locks += fmt::format(
        "\n    {:<20} {:8.1f} acquisitions, {:5.2f}% contended, wait {:.3f} ms, held ~{:.3f} ms",
        m.name, m.acquisitions / frames, 100.0 * m.contended / m.acquisitions,
        m.wait_ns / 1e6 / frames, m.hold_ns / 1e6 / frames);
  }
  auto& events = session.gpu_events();
  REXLOG_INFO(
      "live measurements (per frame over {} frames, hooks over the {} measured):\n  producer "
      "cost by hook:{}\n  locks:{}\n  CPU reads of render targets: {} locks, {} blitToMemory",
      cuts, measured, hooks.empty() ? " none" : hooks, locks.empty() ? " none" : locks,
      events.render_target_locks.exchange(0), events.render_target_blits_to_memory.exchange(0));
}

}  // namespace

// How long the guest waits for the backend to take a frame before it goes on and the frame drops
// (a minimized window, a lost device): 20 frames a second at worst.
constexpr std::chrono::milliseconds kBackpressureCap{50};

LiveMode& LiveMode::Get() {
  static LiveMode mode;
  return mode;
}

void LiveMode::Start(const LiveOptions& options) {
  options_ = options;
  running_ = true;
  capture::Session::Get().SetProducerTiming(options.producer_timing);
  if (options.backpressure) queue_.SetBackpressure(kBackpressureCap);
  capture::Session::Get().EnableLive(&queue_, &store_);
  finished_ = false;
  thread_ = std::thread([this] {
    Run();
    finished_ = true;
  });
  if (options_.backpressure) {
    REXLOG_INFO("live: backpressure on: the guest waits at its swap for the backend (up to {} ms)",
                kBackpressureCap.count());
  }
  REXLOG_INFO("live: native backend {}{}, {}x{}, frames queued at the guest swap",
              options_.only ? "as the only renderer (Xenos off)" : "in parallel",
              options_.draw ? "" : " WITHOUT DRAWING (diagnostics)", options_.width,
              options_.height);
}

void LiveMode::ResizeWindow(uint32_t width, uint32_t height) {
  if (width && height) pending_window_size_ = uint64_t(width) << 32 | height;
}

void LiveMode::SetVideo(settings::Resolution render_resolution, bool vsync) {
  std::lock_guard lock(pending_video_mutex_);
  pending_video_ = PendingVideo{render_resolution, vsync};
}

void LiveMode::Stop() {
  if (!running_.exchange(false)) return;
  queue_.Close();
  // The render thread may be waiting for the main thread to create, resize or destroy the
  // backend's window (macOS, platform::RunOnWindowThread): that work runs until the thread ends.
  platform::WaitServingWindowThread([this] { return finished_.load(); });
  if (thread_.joinable()) thread_.join();
}

void LiveMode::RunWithoutDrawing() {
  frontend::ZipArchive pak;  // not opened: no texture is decoded
  LiveContentSource content(pak);
  size_t frames = 0, frames_dropped = 0, draws = 0;
  Series consume_ms;
  auto last_summary = Clock::now();
  while (running_) {
    auto frame = queue_.Pop(std::chrono::milliseconds(100));
    if (!frame) continue;
    auto start = Clock::now();
    frames_dropped += frame->dropped_before;
    content.Apply(*frame);
    for (const auto& cmd : frame->commands) draws += std::holds_alternative<commands::Draw>(cmd.payload);
    content.Finish();
    ++frames;
    auto end = Clock::now();
    consume_ms.Add(Ms(end - start));
    if (end - last_summary >= std::chrono::seconds(10)) {
      auto& times = capture::Session::Get().producer_times();
      uint64_t cuts = times.frames.exchange(0);
      uint64_t measured = times.measured.exchange(0);
      double ns[size_t(capture::ProducerSection::kCount)];
      for (size_t i = 0; i < size_t(capture::ProducerSection::kCount); ++i)
        ns[i] = double(times.ns[i].exchange(0));
      auto per_frame = [&](double v) { return measured ? v / double(measured) / 1e6 : 0.0; };
      using PS = capture::ProducerSection;
      double constants = per_frame(ns[size_t(PS::kConstants)]);
      double resources = per_frame(ns[size_t(PS::kResources)]);
      REXLOG_INFO(
          "live (no draw): {} frames consumed, {} dropped, {} draws; consumer {}; live content "
          "held {:.1f} MiB\n  producer ms per frame: constants {:.2f}, other commands {:.2f}, "
          "resource snapshots {:.2f}, version marking {:.2f}, frame cut {:.2f}",
          frames, frames_dropped, draws, consume_ms.Summary(), content.content_bytes() / 1048576.0,
          constants, per_frame(ns[size_t(PS::kCommands)]) - constants - resources, resources,
          per_frame(ns[size_t(PS::kMarking)]), per_frame(ns[size_t(PS::kCut)]));
      LogMeasurements(cuts, measured);
      frames = frames_dropped = draws = 0;
      last_summary = end;
    }
  }
}

namespace {

// The UI overlay's texture operations and latest frame, handed to the backend before presenting.
void ApplyUi(tl_backend* backend, UiOverlay& overlay) {
  std::vector<UiTextureOp> ops;
  std::optional<UiFrame> frame;
  overlay.Take(ops, frame);
  for (const UiTextureOp& op : ops) {
    if (op.release) {
      tl_backend_ui_release_texture(backend, op.id);
    } else if (tl_backend_ui_texture(backend, op.id, op.width, op.height, op.linear, op.repeat,
                                     op.rgba.data()) != 0) {
      REXLOG_ERROR("live: UI texture {} ({}x{}) not created", op.id, op.width, op.height);
    }
  }
  if (frame) {
    tl_backend_set_ui_frame(backend, frame->space_width, frame->space_height,
                            frame->vertices.data(), uint32_t(frame->vertices.size()),
                            frame->indices.data(), uint32_t(frame->indices.size()),
                            frame->cmds.data(), uint32_t(frame->cmds.size()));
  }
}

const char* OgreLog(const LiveOptions& options) {
  return options.ogre_log_path.empty() ? nullptr : options.ogre_log_path.c_str();
}

}  // namespace

void LiveMode::Run() {
  if (!options_.draw) {
    RunWithoutDrawing();
    return;
  }
  char err[512] = {};
  const platform::NativeWindow& parent = options_.parent_window;
  const tl_native_window native = {parent.system, parent.window, parent.display};
  tl_backend* backend =
      parent.system != platform::NativeWindow::kNone
          ? tl_backend_create_child(options_.render_system, options_.gpu.c_str(), options_.width,
                                    options_.height, OgreLog(options_), &native,
                                    options_.window_width, options_.window_height,
                                    options_.vsync, err, sizeof(err))
          : tl_backend_create(options_.render_system, options_.gpu.c_str(), options_.width,
                              options_.height, OgreLog(options_),
                              "Torchlight - native OGRE backend (live)", err, sizeof(err));
  if (!backend) {
    REXLOG_ERROR("live: backend creation failed: {}", err);
    return;
  }
  REXLOG_INFO("live: renderer {}", tl_backend_renderer(backend));
  // Internal render scale for the current window size (LiveOptions::render_resolution).
  settings::Resolution window_size{options_.window_width, options_.window_height};
  auto apply_render_scale = [&] {
    const float scale = settings::RenderScaleFor(options_.render_resolution,
                                                 {options_.width, options_.height}, window_size);
    if (tl_backend_set_render_scale(backend, scale) == 0) {
      REXLOG_INFO("live: render scale {:.3g} ({}x{})", scale,
                  uint32_t(std::lround(options_.width * scale)),
                  uint32_t(std::lround(options_.height * scale)));
    } else {
      REXLOG_ERROR("live: render scale {:.3g} not applied", scale);
    }
  };
  apply_render_scale();
  frontend::ZipArchive pak;
  std::string error;
  if (!pak.Open((options_.game_data_root / "pak.zip").string(), error)) {
    REXLOG_ERROR("live: {}", error);
  }
  LiveContentSource content(pak);
  frontend::Frontend frontend(backend, content);

  // Session recording (--live_record): every consumed frame, before it is run.
  SessionWriter recorder;
  if (!options_.record_path.empty()) {
    std::string record_error;
    if (recorder.Open(options_.record_path, options_.record_max_bytes, record_error)) {
      REXLOG_INFO("live: recording the session to {} (cap {} MiB)", options_.record_path,
                  options_.record_max_bytes >> 20);
    } else {
      REXLOG_ERROR("live: session recording not started: {}", record_error);
    }
  }
  uint64_t recorded_bytes_before = recorder.bytes_written();

  Series backend_ms, latency_ms, texture_ms, present_ms, record_ms;
  // Consumer breakdown per frame (FrameStep), means over the period.
  double content_ms = 0, commands_ms = 0, draws_ms = 0, end_ms = 0, after_ms = 0, release_ms = 0;
  size_t frames = 0, frames_dropped = 0, textures_created = 0, slow_texture_frames = 0;
  CommandCounts command_counts{};
  size_t draws_before = 0, drawn_before = 0, not_ready_before = 0;
  std::map<std::string, size_t> skipped_before, degraded_before, problems_before;
  auto last_summary = Clock::now();
  bool window_open = true;
  SlowFrameDetector slow_frames;
  FrameReclaimer reclaimer;  // consumed frames are freed on its thread, not this one
  Clock::time_point last_present{};
  size_t dropped_since_present = 0;  // guest frames skipped since the last present (FrameTiming)
  FrameQueue::Waits waits_before;     // backpressure, at the last summary

  while (running_) {
    auto frame = queue_.Pop(std::chrono::milliseconds(100));
    if (!frame) continue;
    auto start = Clock::now();
    frames_dropped += frame->dropped_before;
    dropped_since_present += frame->dropped_before;
    FrameRecord record;
    record.swap = frame->swap;
    record.guest_ms = frame->producer.guest_ms;
    record.dropped = frame->dropped_before;
    record.worst_dropped_guest_ms = frame->producer.worst_dropped_guest_ms;
    record.recording_ms = frame->producer.recording_ms;
    record.commands = uint32_t(frame->commands.size());
    record.snapshot_copies = frame->producer.snapshot_copies;
    record.snapshot_bytes = frame->producer.snapshot_bytes;
    record.resources_announced =
        uint32_t(frame->vertex_buffers.size() + frame->index_buffers.size() +
                 frame->declarations.size() + frame->textures.size() + frame->programs.size());
    record.resources_destroyed = uint32_t(frame->destroyed.size());
    if (recorder.open()) {
      auto record_start = Clock::now();
      SessionWriter::Result r = recorder.Append(*frame);
      record_ms.Add(Ms(Clock::now() - record_start));
      if (r == SessionWriter::Result::kCapReached) {
        REXLOG_WARN("live: session recording stopped at the {} MiB cap after {} frames; {} is "
                    "complete up to there",
                    options_.record_max_bytes >> 20, recorder.frames(), options_.record_path);
      } else if (r == SessionWriter::Result::kError) {
        REXLOG_ERROR("live: session recording failed after {} frames (write error)",
                     recorder.frames());
      }
    }
    FrameStep step = RunFrameStep(*frame, content, frontend, [&] {
      if (uint64_t size = pending_window_size_.exchange(0)) {
        tl_backend_resize_window(backend, uint32_t(size >> 32), uint32_t(size));
        if (parent.system != platform::NativeWindow::kNone) {
          window_size = {uint32_t(size >> 32), uint32_t(size)};
          apply_render_scale();
        }
      }
      std::optional<PendingVideo> video;
      {
        std::lock_guard lock(pending_video_mutex_);
        video.swap(pending_video_);
      }
      if (video) {
        if (video->vsync != options_.vsync) {
          options_.vsync = video->vsync;
          tl_backend_set_vsync(backend, video->vsync);
          REXLOG_INFO("live: vsync {}", video->vsync ? "on" : "off");
        }
        if (video->render_resolution != options_.render_resolution) {
          options_.render_resolution = video->render_resolution;
          apply_render_scale();
        }
      }
      if (window_open) {
        if (options_.ui) ApplyUi(backend, *options_.ui);
        auto present_start = Clock::now();
        window_open = tl_backend_present(backend) != 0;
        const auto present_end = Clock::now();
        if (window_open) {
          FrameTiming::Get().OnPresent(dropped_since_present);
          dropped_since_present = 0;
        }
        record.present_ms = Ms(present_end - present_start);
        present_ms.Add(record.present_ms);
        if (last_present.time_since_epoch().count() != 0) {
          record.present_interval_ms = Ms(present_end - last_present);
        }
        last_present = present_end;
        if (options_.request_ui_frame) options_.request_ui_frame();
      }
      // F9 with a top-level backend window focused arms the same capture as the game window's
      // bind (a child window leaves its keys to the game window).
      if (tl_backend_take_keys(backend) & TL_KEY_F9) capture::Session::Get().RequestCapture();
    });
    if (frontend.guest_width() &&
        (frontend.guest_width() != options_.width || frontend.guest_height() != options_.height)) {
      options_.width = frontend.guest_width();
      options_.height = frontend.guest_height();
      REXLOG_INFO("live: guest frame {}x{}", options_.width, options_.height);
      apply_render_scale();
    }
    textures_created += step.textures_created;
    AddCommandCounts(command_counts, step);
    content_ms += step.content_ms;
    commands_ms += step.commands_ms;
    draws_ms += step.draws_ms;
    end_ms += step.end_ms;
    after_ms += step.after_ms;
    release_ms += step.release_ms;
    if (step.textures_created) {
      texture_ms.Add(step.texture_ms);
      if (step.texture_ms > 16.7) ++slow_texture_frames;
    }
    auto end = Clock::now();
    backend_ms.Add(Ms(end - start));
    latency_ms.Add(Ms(end - frame->cut));
    reclaimer.Free(std::move(*frame));
    ++frames;
    record.backend_ms = Ms(end - start);
    record.content_ms = step.content_ms;
    record.commands_ms = step.commands_ms;
    record.draws_ms = step.draws_ms;
    record.end_ms = step.end_ms;
    record.release_ms = step.release_ms;
    record.textures_created = uint32_t(step.textures_created);
    record.texture_ms = step.texture_ms;
    tl_backend_counters counters;
    tl_backend_take_counters(backend, &counters);
    record.programs_generated = counters.programs_generated;
    record.program_ms = counters.program_ms;
    record.program_generate_ms = counters.program_generate_ms;
    record.gpu_buffers_created = counters.gpu_buffers_created;
    record.vertex_layouts_created = counters.vertex_layouts_created;
    if (auto line = slow_frames.Add(record)) REXLOG_INFO("live: {}", *line);

    if (end - last_summary >= std::chrono::seconds(10)) {
      const auto& s = frontend.stats();
      REXLOG_INFO(
          "live: {} frames rendered, {} dropped (backend behind), {} draws ({} drawn), "
          "{} draws with a texture not ready, {} textures created ({} preparation(s) over 16.7 ms)"
          "\n  backend frame: {}\n  present (vsync wait included): {}"
          "\n  swap-to-presented latency: {}\n  texture preparation per "
          "frame: {}\n  live content held: {:.1f} MiB (store {:.1f} MiB)"
          "\n  skipped:{}\n  degraded:{}\n  texture problems:{}",
          frames, frames_dropped, s.draws_total - draws_before, s.draws_drawn - drawn_before,
          s.textures_not_ready - not_ready_before, textures_created, slow_texture_frames,
          backend_ms.Summary(), present_ms.Summary(), latency_ms.Summary(),
          texture_ms.Summary(),
          content.content_bytes() / 1048576.0, store_.bytes() / 1048576.0,
          Delta(s.skipped, skipped_before), Delta(s.degraded, degraded_before),
          Delta(s.texture_problems, problems_before));
      const double per = double(std::max<size_t>(frames, 1));
      if (options_.backpressure) {
        const FrameQueue::Waits w = queue_.waits();
        REXLOG_INFO("live backpressure: the guest waited {:.2f} ms per rendered frame ({} waits, {} "
                    "reached the cap) this period",
                    (w.ms - waits_before.ms) / per, w.count - waits_before.count,
                    w.timeouts - waits_before.timeouts);
        waits_before = w;
      }
      REXLOG_INFO("live consumer (backend thread, ms per frame): content and textures {:.2f}, "
                  "commands {:.2f} (frontend {:.2f}, backend draws {:.2f}), frame end {:.2f}, "
                  "present {:.2f}, content release {:.2f}",
                  content_ms / per, commands_ms / per, (commands_ms - draws_ms) / per,
                  draws_ms / per, end_ms / per, after_ms / per, release_ms / per);
      content_ms = commands_ms = draws_ms = end_ms = after_ms = release_ms = 0;
      if (recorder.frames()) {
        REXLOG_INFO("live recording: {} frames, {:.1f} MiB total ({:.1f} MiB this period){}; "
                    "per frame {}",
                    recorder.frames(), recorder.bytes_written() / 1048576.0,
                    (recorder.bytes_written() - recorded_bytes_before) / 1048576.0,
                    recorder.open() ? "" : ", stopped", record_ms.Summary());
        recorded_bytes_before = recorder.bytes_written();
      }
      // Producer (guest threads), per live frame cut in the period.
      auto& times = capture::Session::Get().producer_times();
      uint64_t cuts = times.frames.exchange(0);
      uint64_t measured = times.measured.exchange(0);
      double ns[size_t(capture::ProducerSection::kCount)];
      for (size_t i = 0; i < size_t(capture::ProducerSection::kCount); ++i)
        ns[i] = double(times.ns[i].exchange(0));
      auto per_frame = [&](double v) { return measured ? v / double(measured) / 1e6 : 0.0; };
      using PS = capture::ProducerSection;
      double constants = per_frame(ns[size_t(PS::kConstants)]);
      double resources = per_frame(ns[size_t(PS::kResources)]);
      double commands = per_frame(ns[size_t(PS::kCommands)]) - constants - resources;
      if (capture::Session::Get().producer_timing()) REXLOG_INFO(
          "live producer (guest threads, ms per frame over {} measured frames): constants {:.2f}, other "
          "commands {:.2f}, resource snapshots {:.2f}, version marking {:.2f}, frame cut {:.2f}; "
          "total {:.2f}",
          measured, constants, commands, resources, per_frame(ns[size_t(PS::kMarking)]),
          per_frame(ns[size_t(PS::kCut)]),
          constants + commands + resources + per_frame(ns[size_t(PS::kMarking)]) +
              per_frame(ns[size_t(PS::kCut)]));
      LogMeasurements(cuts, measured);
      REXLOG_INFO("live commands consumed: {}", FormatCommandCounts(command_counts, frames));
      command_counts = {};
      if (std::string slow = slow_frames.TakePeriodSummary(); !slow.empty()) {
        REXLOG_INFO("live: {}", slow);
      }
      draws_before = s.draws_total;
      drawn_before = s.draws_drawn;
      not_ready_before = s.textures_not_ready;
      frames = frames_dropped = textures_created = slow_texture_frames = 0;
      last_summary = end;
    }
  }
  if (recorder.frames()) {
    REXLOG_INFO("live: session recording {}: {} frames, {:.1f} MiB", options_.record_path,
                recorder.frames(), recorder.bytes_written() / 1048576.0);
  }
  recorder.Close();
  tl_backend_destroy(backend);
}

}  // namespace torchlight::live

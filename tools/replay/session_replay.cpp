#include "session_replay.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <sstream>

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

}  // namespace torchlight::replay

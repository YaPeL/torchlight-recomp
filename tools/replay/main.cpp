// replay: the frontend fed from a capture file, rendering offscreen, compared with the capture's
// reference image.
//
// Usage: kUsage below (replay --help prints it).
// Writes render.png, reference.png, side_by_side.png and report.txt to --out (default: the
// capture's directory) and prints the report.

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "backend/backend_api.h"
#include "commands/serialize.h"
#include "frontend/content_source.h"
#include "frontend/frontend.h"
#include "frontend/zip_archive.h"
#include "live/live_content_source.h"
#include "platform/platform.h"
#include "png.h"
#include "session_replay.h"

using namespace torchlight::commands;
using torchlight::frontend::CaptureContentSource;
using torchlight::frontend::Frontend;
using torchlight::frontend::PackId;
using torchlight::frontend::ZipArchive;
using torchlight::replay::WritePng;

namespace {
constexpr const char kUsage[] = R"(Usage: replay CAPTURE.tlcap [--game_data_root DIR] [--out DIR] [--draws LIST]
       (without --game_data_root, the game's files are not read: for captures that load none,
       such as synthetic_capture's)
              [--region X0,Y0,X1,Y1] [--dump_targets] [--window] [--via_live]
              [--frames_png] [--live_content] [--render_scale S] [--render_system NAME]
              [--gpu ID]
       replay --session RECORDING.tlses --game_data_root DIR [--out DIR] [--session_frame N]
              [--session_frames A-B] [--draws LIST] [--region ...] [--dump_targets]
              [--render_system NAME] [--gpu ID]
       replay --list_gpus
  --draws limits the draws issued to LIST, comma-separated indices or inclusive ranges
  (e.g. 0-120,150,190-194); the others are counted as excluded.
  --region lists the draws to the main target with fragments inside the rectangle (pixels,
  x1/y1 exclusive; occluded fragments count too), with their programs and textures.
  --dump_targets writes every render target's final content as target_<name>.png.
  --render_scale draws at S times the guest's size (tl_backend_set_render_scale); images are
  still written at the guest's size.
  --render_system gl3plus (default) or d3d11 (Windows): the OGRE render system drawn with.
  --gpu draws on the GPU with that platform id (Direct3D 11; default automatic); --list_gpus
  lists them, software ones too (WARP: for testing only, the game never offers it).
  --window also shows the result in a window for 3 seconds (the live mode's presentation).
  --frames_png writes each frame's output (at its Present) as frame_<index>.png, for captures
  of several frames.
  --live_content replays with the content the live stream used (captures taken in the live
  mode, format 1.5): what the backend window showed, instead of the content hashed at capture.
  --via_live feeds the capture as one live frame through the live mode's content source
  (descriptions and content announced in the frame, superseded content freed after it).
  --session replays a live session recording (--live_record) through the live mode's per-frame
  step: the state the backend accumulated in the session is reproduced. --session_frame picks
  the frame reported on (default: the last), --session_frames A-B[/STEP] writes
  frame_<index>.png for a range (every STEP-th frame); --draws, --region and --dump_targets apply to the reported frame.
)";

int UsageError(const std::string& message) {
  std::fprintf(stderr, "error: %s\n%s", message.c_str(), kUsage);
  return 2;
}
}  // namespace

int main(int argc, char** argv) {
  std::string capture_path, data_root, out_dir;
  std::vector<std::pair<size_t, size_t>> draw_ranges;  // empty: all draws
  std::optional<std::array<uint32_t, 4>> region;
  bool dump_targets = false, window = false, via_live = false, frames_png = false,
       use_live_content = false;
  std::string session_path;
  std::optional<uint64_t> session_frame;
  std::optional<std::pair<uint64_t, uint64_t>> session_frames;
  uint64_t session_frames_step = 1;
  float render_scale = 1;
  tl_render_system render_system = TL_RENDER_SYSTEM_GL3PLUS;
  std::string gpu;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--draws" && i + 1 < argc) {
      std::stringstream list(argv[++i]);
      std::string range;
      while (std::getline(list, range, ',')) {
        size_t dash = range.find('-');
        size_t first = std::stoul(range.substr(0, dash));
        size_t last = dash == std::string::npos ? first : std::stoul(range.substr(dash + 1));
        draw_ranges.push_back({first, last});
      }
    } else if (a == "--session" && i + 1 < argc) {
      session_path = argv[++i];
    } else if (a == "--session_frame" && i + 1 < argc) {
      session_frame = std::stoull(argv[++i]);
    } else if (a == "--session_frames" && i + 1 < argc) {
      unsigned long long first = 0, last = 0, step = 1;
      if (std::sscanf(argv[++i], "%llu-%llu/%llu", &first, &last, &step) < 2 || step == 0) {
        std::fprintf(stderr, "error: --session_frames A-B[/STEP]\n");
        return 2;
      }
      session_frames = std::make_pair(uint64_t(first), uint64_t(last));
      session_frames_step = step;
    } else if (a == "--render_scale" && i + 1 < argc) {
      render_scale = std::stof(argv[++i]);
    } else if (a == "--live_content") {
      use_live_content = true;
    } else if (a == "--frames_png") {
      frames_png = true;
    } else if (a == "--via_live") {
      via_live = true;
    } else if (a == "--window") {
      window = true;
    } else if (a == "--render_system" && i + 1 < argc) {
      const std::string name = argv[++i];
      if (name == "gl3plus") {
        render_system = TL_RENDER_SYSTEM_GL3PLUS;
      } else if (name == "d3d11") {
        render_system = TL_RENDER_SYSTEM_D3D11;
      } else {
        std::fprintf(stderr, "error: --render_system gl3plus|d3d11\n");
        return 2;
      }
    } else if (a == "--gpu" && i + 1 < argc) {
      gpu = argv[++i];
    } else if (a == "--list_gpus") {
      for (const torchlight::platform::Gpu& g : torchlight::platform::AllGpus()) {
        std::printf("%s  %s (%s)\n", g.id.c_str(), g.name.c_str(), g.driver.c_str());
      }
      return 0;
    } else if (a == "--dump_targets") {
      dump_targets = true;
    } else if (a == "--region" && i + 1 < argc) {
      std::array<uint32_t, 4> r{};
      if (std::sscanf(argv[++i], "%u,%u,%u,%u", &r[0], &r[1], &r[2], &r[3]) != 4) {
        std::fprintf(stderr, "error: --region X0,Y0,X1,Y1\n");
        return 2;
      }
      region = r;
    } else if (a == "--game_data_root" && i + 1 < argc) {
      data_root = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--help" || a == "-h") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (a.rfind("-", 0) == 0) {
      return UsageError("unknown option or missing value: " + a);
    } else if (!capture_path.empty()) {
      return UsageError("more than one capture: " + capture_path + ", " + a);
    } else {
      capture_path = a;
    }
  }
  if (!session_path.empty() && data_root.empty()) return UsageError("--session needs --game_data_root");
  if (!session_path.empty()) {
    torchlight::replay::SessionOptions o;
    o.path = session_path;
    o.data_root = data_root;
    o.out_dir = out_dir.empty()
                    ? std::filesystem::path(session_path).parent_path().string()
                    : out_dir;
    if (o.out_dir.empty()) o.out_dir = ".";
    std::error_code ec;
    std::filesystem::create_directories(o.out_dir, ec);
    if (ec) {
      std::fprintf(stderr, "error: cannot create %s: %s\n", o.out_dir.c_str(),
                   ec.message().c_str());
      return 1;
    }
    o.frame = session_frame;
    o.png_frames = session_frames;
    o.png_step = session_frames_step;
    o.draw_ranges = draw_ranges;
    o.region = region;
    o.dump_targets = dump_targets;
    o.render_system = render_system;
    o.gpu = gpu;
    return torchlight::replay::ReplaySession(o);
  }
  if (capture_path.empty()) return UsageError("no capture given");
  if (out_dir.empty()) out_dir = std::filesystem::path(capture_path).parent_path().string();
  if (out_dir.empty()) out_dir = ".";  // a capture named without a folder
  std::error_code ec;
  std::filesystem::create_directories(out_dir, ec);
  if (ec) {
    std::fprintf(stderr, "error: cannot create %s: %s\n", out_dir.c_str(), ec.message().c_str());
    return 1;
  }

  Capture c;
  std::string error;
  if (!ReadCaptureFile(capture_path, c, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  if (use_live_content) {
    // Swap every content key for the one the live stream used.
    if (c.live_blobs.empty()) {
      std::fprintf(stderr, "error: --live_content needs a capture taken in the live mode (1.5+)\n");
      return 1;
    }
    for (auto& b : c.live_blobs) c.blobs.push_back(b);
    size_t draws = 0, patched = 0;
    for (auto& frame : c.frames)
      for (auto& cmd : frame.commands)
        if (auto* d = std::get_if<Draw>(&cmd.payload)) {
          ++draws;
          if (d->live_keys.empty()) continue;
          size_t k = 0;
          for (auto& snap : d->vertex_buffers)
            if (k < d->live_keys.size()) snap.blob = d->live_keys[k++];
          if (d->index_buffer && k < d->live_keys.size()) d->index_buffer->blob = d->live_keys[k++];
          ++patched;
        }
    size_t textures = 0;
    for (auto& t : c.textures) {
      if (!t.content) continue;
      // The live content paired with the capture's content of this texture, else its last one.
      Hash live = 0;
      for (const auto& lt : c.live_textures)
        if (lt.id.guest_address == t.id.guest_address && lt.id.generation == t.id.generation &&
            (lt.capture == t.content || live == 0))
          live = lt.live;
      if (live) {
        t.content = live;
        ++textures;
      }
    }
    std::printf("live content: %zu of %zu draws and %zu dynamic textures use the live stream's "
                "content\n", patched, draws, textures);
  }
  uint32_t width = c.reference ? c.reference->width : 1280;
  uint32_t height = c.reference ? c.reference->height : 720;

  char err[512] = {};
  tl_backend* backend = tl_backend_create(render_system, gpu.c_str(), width, height,
                                          "tl_backend_ogre.log",
                                          window ? "torchlight replay" : nullptr, err,
                                          sizeof(err));
  if (!backend) {
    std::fprintf(stderr, "backend: %s\n", err);
    return 1;
  }
  if (tl_backend_set_render_scale(backend, render_scale) != 0) {
    std::fprintf(stderr, "error: render scale %g not accepted\n", render_scale);
    return 1;
  }
  ZipArchive pak;
  if (!data_root.empty() && !pak.Open(data_root + "/pak.zip", error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  CaptureContentSource capture_content(c, pak);
  torchlight::live::LiveContentSource live_content(pak);
  const torchlight::frontend::ContentSource& content =
      via_live ? static_cast<const torchlight::frontend::ContentSource&>(live_content)
               : capture_content;
  Frontend frontend(backend, content);
  torchlight::live::LiveFrame live_frame;
  if (via_live) {
    // The whole capture as one live frame: everything announced up front, content with the
    // resource it belongs to.
    std::unordered_map<Hash, std::shared_ptr<const Blob>> blobs;
    for (const auto& b : c.blobs) blobs[b.hash] = std::make_shared<const Blob>(b);
    live_frame.vertex_buffers = c.vertex_buffers;
    live_frame.index_buffers = c.index_buffers;
    live_frame.declarations = c.vertex_declarations;
    live_frame.textures = c.textures;
    live_frame.programs = c.programs;
    live_frame.program_sources = c.program_sources;
    for (const auto& t : c.textures)
      if (t.content && blobs.count(t.content)) live_frame.contents.push_back({t.id, blobs[t.content]});
    for (const auto& frame : c.frames)
      for (const auto& cmd : frame.commands)
        if (auto* d = std::get_if<Draw>(&cmd.payload)) {
          for (const auto& snap : d->vertex_buffers)
            if (snap.blob && blobs.count(snap.blob))
              live_frame.contents.push_back({snap.buffer, blobs[snap.blob]});
          if (d->index_buffer && d->index_buffer->blob && blobs.count(d->index_buffer->blob))
            live_frame.contents.push_back({d->index_buffer->buffer, blobs[d->index_buffer->blob]});
        }
    auto changes = live_content.Apply(live_frame);
    for (const auto& t : changes.textures) frontend.PrepareTexture(t);
  } else {
    // A capture knows all its textures up front: create them before the frame, as the live mode
    // does when the guest creates them.
    for (const auto& t : c.textures) frontend.PrepareTexture(t);
  }
  if (!draw_ranges.empty()) {
    frontend.set_draw_filter([&](size_t index) {
      return std::any_of(draw_ranges.begin(), draw_ranges.end(),
                         [&](auto& r) { return index >= r.first && index <= r.second; });
    });
  }
  std::ostringstream region_report;
  if (region) {
    tl_backend_set_probe(backend, 1, (*region)[0], (*region)[1], (*region)[2], (*region)[3]);
    frontend.set_draw_observer([&](size_t index) {
      uint32_t covered = tl_backend_probe_samples(backend);
      if (covered) {
        region_report << "  d" << index << ": " << covered << " fragments"
                      << frontend.DescribeCurrentDraw() << "\n";
      }
    });
  }

  frontend.BeginFrame();
  for (const auto& frame : c.frames) {
    for (const auto& cmd : frame.commands) {
      frontend.Execute(cmd);
      if (frames_png && std::holds_alternative<Present>(cmd.payload)) {
        frontend.EndFrame();
        // The guest's frame size (its window target), once it bound one.
        const uint32_t w = frontend.guest_width() ? frontend.guest_width() : width;
        const uint32_t h = frontend.guest_height() ? frontend.guest_height() : height;
        std::vector<uint8_t> rgba(size_t(w) * h * 4);
        tl_backend_read_rgba(backend, rgba.data(), w * 4);
        for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%03u.png", frame.index);
        WritePng(out_dir + name, w, h, rgba);
        frontend.BeginFrame();
      }
    }
  }
  frontend.EndFrame();
  if (via_live)
    for (auto key : live_content.Finish()) frontend.ReleaseContent(key);
  if (window) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until && tl_backend_present(backend))
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
  const auto& stats = frontend.stats();
  if (dump_targets) {
    for (const auto& t : c.textures) {
      if (!t.render_target || !frontend.TextureReady(t.id)) continue;
      std::vector<uint8_t> rgba(size_t(t.width) * t.height * 4);
      if (tl_backend_read_target_rgba(backend, PackId(t.id), t.width, t.height, rgba.data(),
                                      t.width * 4) == 0) {
        WritePng(out_dir + "/target_" + t.name + ".png", t.width, t.height, rgba);
      }
    }
  }

  // ---- images and metric ----
  // At the guest's frame size; a reference of another size (the console scaled its output) is not
  // compared.
  const bool reference_matches =
      !c.reference || frontend.guest_width() == 0 ||
      (c.reference->width == frontend.guest_width() && c.reference->height == frontend.guest_height());
  if (frontend.guest_width()) {
    width = frontend.guest_width();
    height = frontend.guest_height();
  }
  if (!reference_matches) {
    std::printf("reference %ux%u differs from the guest frame %ux%u: not compared\n",
                c.reference->width, c.reference->height, width, height);
  }
  std::vector<uint8_t> render(size_t(width) * height * 4);
  tl_backend_read_rgba(backend, render.data(), width * 4);
  std::vector<uint8_t> reference(size_t(width) * height * 4, 0);
  if (c.reference && reference_matches) {
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width; ++x) {
        const uint8_t* src = &c.reference->rgbx[size_t(y) * c.reference->stride + x * 4];
        uint8_t* dst = &reference[(size_t(y) * width + x) * 4];
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
      }
  }
  for (size_t i = 3; i < render.size(); i += 4) render[i] = 255;
  double squared = 0, absolute = 0;
  for (size_t i = 0; i < render.size(); ++i) {
    if (i % 4 == 3) continue;
    double d = double(render[i]) - double(reference[i]);
    squared += d * d;
    absolute += std::fabs(d);
  }
  double samples = double(width) * height * 3;
  double mse = squared / samples;
  double psnr = mse == 0 ? INFINITY : 10.0 * std::log10(255.0 * 255.0 / mse);
  std::vector<uint8_t> side(size_t(width) * 2 * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    std::copy_n(&render[size_t(y) * width * 4], width * 4, &side[size_t(y) * width * 8]);
    std::copy_n(&reference[size_t(y) * width * 4], width * 4, &side[size_t(y) * width * 8 + width * 4]);
  }
  WritePng(out_dir + "/render.png", width, height, render);
  WritePng(out_dir + "/reference.png", width, height, reference);
  WritePng(out_dir + "/side_by_side.png", width * 2, height, side);

  std::ostringstream report;
  report << "capture: " << capture_path << "\n";
  report << "renderer: " << tl_backend_renderer(backend) << "\n";
  report << "display gamma ramp: " << stats.gamma_ramp << "\n";
  report << "draws: " << stats.draws_total << " total, " << stats.draws_drawn << " drawn, " << stats.skipped.total()
         << " skipped\n";
  for (const auto& [k, v] : stats.skipped.counts) report << "  skipped  " << v << "  " << k << "\n";
  report << "degraded (drawn, approximated):\n";
  for (const auto& [k, v] : stats.degraded.counts) report << "  degraded " << v << "  " << k << "\n";
  report << "  texture combination approximated (blend mode is not modulate): "
         << stats.approx_combination << "\n";
  report << "  texture combination not recorded in this capture (format < 1.1): "
         << stats.unrecorded_combination << "\n";
  report << "textures: " << stats.named_loaded << " from pak.zip, " << stats.dynamic_loaded
         << " dynamic (untiled), " << stats.render_targets << " render targets; "
         << stats.textures_not_ready << " draw(s) with a texture not ready (fallback)\n";
  for (const auto& [k, v] : stats.texture_problems.counts) report << "  problem  " << v << "  " << k << "\n";
  char metric[128];
  std::snprintf(metric, sizeof(metric), "metric vs reference: PSNR %.2f dB, mean abs error %.2f\n",
                psnr, absolute / samples);
  report << metric;
  report << "images: " << out_dir << "/{render,reference,side_by_side}.png\n";
  if (region) {
    report << "draws covering region " << (*region)[0] << "," << (*region)[1] << "-"
           << (*region)[2] << "," << (*region)[3] << " of the main target (fragments, occluded included):\n"
           << region_report.str();
  }
  std::printf("%s", report.str().c_str());
  FILE* f = std::fopen((out_dir + "/report.txt").c_str(), "w");
  if (f) {
    std::fputs(report.str().c_str(), f);
    std::fclose(f);
  }
  tl_backend_destroy(backend);
  return 0;
}

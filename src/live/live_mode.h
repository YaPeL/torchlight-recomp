// Live mode: every guest frame is drawn by the host backend. With --native_live=parallel the guest
// keeps running on Xenos and the backend draws in a second window; with --native_live=only Xenos
// is off (rexgpu-null) and the backend is the only renderer.
//
// The guest render thread records LiveFrames (capture/session.h) and pushes them to a bounded
// queue at the swap; one backend thread owns OGRE and its GL context, applies each frame's
// resources (textures are created there, between frames), runs the frame through the same
// Frontend the replay uses and presents it. Nothing is ever waited for on the guest side.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "backend/backend_api.h"
#include "live/frame_queue.h"
#include "live/session_file.h"
#include "live/snapshot_store.h"
#include "live/ui_overlay.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

namespace torchlight::live {

struct LiveOptions {
  std::filesystem::path game_data_root;  // holds pak.zip
  uint32_t width = 1280, height = 720;
  // Diagnostics (--native_live=parallel_nodraw): record and consume the frames (resources
  // applied, content freed) without creating the backend: no GL, nothing drawn or presented. Tells
  // the backend's GPU cost apart from the rest of the live mode's.
  bool draw = true;
  // --native_live=only: Xenos is off (rexgpu-null) and this backend is the only renderer.
  bool only = false;
  // When set, the backend draws inside this window (the game window), at its size.
  platform::NativeWindow parent_window;
  uint32_t window_width = 0, window_height = 0;
  // The child window waits for vertical sync when presenting (it is the game's display).
  bool vsync = false;
  // The OGRE render system the backend draws with (host settings, only mode).
  tl_render_system render_system = TL_RENDER_SYSTEM_GL3PLUS;
  // Platform GPU id the render system draws on, where it takes one (tl_backend_create); empty:
  // automatic.
  std::string gpu;
  // Internal render resolution (host settings; 0x0 = the window's size): the backend draws at the
  // render scale that fits the guest's frame in it (settings::RenderScaleFor), recomputed when the
  // window's size changes.
  settings::Resolution render_resolution;
  // Host UI (only mode): the runtime's dialogs recorded by `ui`, drawn over the window when
  // presenting; request_ui_frame asks for the next UI frame after each present.
  UiOverlay* ui = nullptr;
  std::function<void()> request_ui_frame;
  // OGRE's log file; empty: none (debugger output only).
  std::string ogre_log_path;
  // Session recording (session_file.h): every consumed frame to this file, up to the cap.
  std::string record_path;
  uint64_t record_max_bytes = 0;
};

class LiveMode {
 public:
  static LiveMode& Get();
  // Starts the backend thread and enables recording in the capture session. Main thread, before
  // the guest runs.
  void Start(const LiveOptions& options);
  void Stop();
  // The game window's new size in pixels (any thread); a child backend window follows it.
  void ResizeWindow(uint32_t width, uint32_t height);
  // New internal render resolution and vertical sync (any thread), applied by the backend thread
  // between frames.
  void SetVideo(settings::Resolution render_resolution, bool vsync);

 private:
  LiveMode() = default;
  ~LiveMode() { Stop(); }
  void Run();
  void RunWithoutDrawing();

  LiveOptions options_;
  FrameQueue queue_{2};
  SnapshotStore store_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> finished_{false};  // Run() has returned (Stop)
  std::atomic<uint64_t> pending_window_size_{0};  // width << 32 | height, 0 when none
  struct PendingVideo {
    settings::Resolution render_resolution;
    bool vsync = true;
  };
  std::mutex pending_video_mutex_;
  std::optional<PendingVideo> pending_video_;  // guarded by pending_video_mutex_
};

}  // namespace torchlight::live

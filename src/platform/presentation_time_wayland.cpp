// Presentation times on Wayland (presentation_time.h): a wp_presentation feedback for each commit of
// the game window's surface, on an event queue of our own, so SDL's dispatching is not touched (its
// thread reads the socket; ours only dispatches what landed in our queue).

#include "platform/presentation_time.h"

#include <ctime>
#include <optional>
#include <string_view>
#include <mutex>
#include <unordered_map>

#include <wayland-client.h>

#include "presentation-time-client-protocol.h"

namespace torchlight::platform {

namespace {

class WaylandPresentationClock final : public PresentationClock {
 public:
  bool Init(wl_display* display, wl_surface* surface) {
    display_ = display;
    surface_ = surface;
    queue_ = wl_display_create_queue(display);
    auto* wrapper = static_cast<wl_display*>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper), queue_);
    registry_ = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    static const wl_registry_listener kRegistry = {
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t) {
          auto* self = static_cast<WaylandPresentationClock*>(data);
          if (std::string_view(interface) == wp_presentation_interface.name && !self->presentation_) {
            self->presentation_ = static_cast<wp_presentation*>(
                wl_registry_bind(registry, name, &wp_presentation_interface, 1));
          }
        },
        [](void*, wl_registry*, uint32_t) {}};
    wl_registry_add_listener(registry_, &kRegistry, this);
    wl_display_roundtrip_queue(display, queue_);
    if (!presentation_) return false;
    static const wp_presentation_listener kPresentation = {
        [](void* data, wp_presentation*, uint32_t clock) {
          static_cast<WaylandPresentationClock*>(data)->clock_ = clockid_t(clock);
        }};
    wp_presentation_add_listener(presentation_, &kPresentation, this);
    wl_display_roundtrip_queue(display, queue_);
    // steady_clock is CLOCK_MONOTONIC (libstdc++ and libc++ on Linux).
    return clock_ == CLOCK_MONOTONIC;
  }

  ~WaylandPresentationClock() override {
    for (auto& [feedback, id] : pending_) wp_presentation_feedback_destroy(feedback);
    if (presentation_) wp_presentation_destroy(presentation_);
    if (registry_) wl_registry_destroy(registry_);
    if (queue_) wl_event_queue_destroy(queue_);
  }

  void BeforePresent(uint64_t id) override {
    wl_display_dispatch_queue_pending(display_, queue_);
    // A compositor that never answers (the window hidden): no unbounded growth.
    if (pending_.size() > 64) return;
    struct wp_presentation_feedback* feedback = wp_presentation_feedback(presentation_, surface_);
    static const wp_presentation_feedback_listener kFeedback = {
        [](void*, struct wp_presentation_feedback*, wl_output*) {},
        [](void* data, struct wp_presentation_feedback* feedback, uint32_t sec_hi, uint32_t sec_lo,
           uint32_t nsec, uint32_t, uint32_t, uint32_t, uint32_t) {
          auto* self = static_cast<WaylandPresentationClock*>(data);
          const auto ns = std::chrono::seconds((uint64_t(sec_hi) << 32) | sec_lo) +
                          std::chrono::nanoseconds(nsec);
          self->Done(feedback, std::chrono::steady_clock::time_point(
                                   std::chrono::duration_cast<std::chrono::steady_clock::duration>(ns)));
        },
        [](void* data, struct wp_presentation_feedback* feedback) {
          static_cast<WaylandPresentationClock*>(data)->Done(feedback, std::nullopt);
        }};
    wp_presentation_feedback_add_listener(feedback, &kFeedback, this);
    pending_[feedback] = id;
  }

  std::vector<Shown> TakeShown() override {
    wl_display_dispatch_queue_pending(display_, queue_);
    std::vector<Shown> shown;
    shown.swap(shown_);
    return shown;
  }

 private:
  void Done(struct wp_presentation_feedback* feedback,
            std::optional<std::chrono::steady_clock::time_point> time) {
    auto it = pending_.find(feedback);
    if (it != pending_.end()) {
      if (time) shown_.push_back({it->second, *time});
      pending_.erase(it);
    }
    wp_presentation_feedback_destroy(feedback);
  }

  wl_display* display_ = nullptr;
  wl_surface* surface_ = nullptr;
  wl_event_queue* queue_ = nullptr;
  wl_registry* registry_ = nullptr;
  wp_presentation* presentation_ = nullptr;
  clockid_t clock_ = -1;
  std::unordered_map<struct wp_presentation_feedback*, uint64_t> pending_;
  std::vector<Shown> shown_;
};

}  // namespace

std::unique_ptr<PresentationClock> CreatePresentationClock(const NativeWindow& window) {
  if (window.system != NativeWindow::kWayland || !window.window || !window.display) return nullptr;
  auto clock = std::make_unique<WaylandPresentationClock>();
  if (!clock->Init(reinterpret_cast<wl_display*>(window.display),
                   reinterpret_cast<wl_surface*>(window.window))) {
    return nullptr;
  }
  return clock;
}

}  // namespace torchlight::platform

// The commands the capture session produces for the live mode: events (constants with their
// auto constants), state (sent once while unchanged), draws (buffer snapshots carrying the live
// content keys) and the frame cut at the swap, compared with what the hooks handed in.

#include <cstdio>
#include <cstdlib>
#include <variant>

#include "capture/session.h"
#include "hooks/hooks.h"
#include "live/frame_queue.h"
#include "live/snapshot_store.h"

// The real one lives with the hooks (linked against the recompiled code).
namespace torchlight::hooks {
SlotAttribution GetSlotAttribution(uint32_t) { return {true, ""}; }
}  // namespace torchlight::hooks

namespace {

using namespace torchlight;
using namespace torchlight::commands;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

template <typename T>
const T* As(const Command& c) {
  return std::get_if<T>(&c.payload);
}

}  // namespace

int main() {
  live::FrameQueue queue(4);
  live::SnapshotStore store;
  capture::Session& s = capture::Session::Get();
  s.EnableLive(&queue, &store);

  SetConstants constants;
  constants.stage = {uint8_t(ProgramStage::kVertex), 0};
  constants.mask = 0xFFFF;
  constants.floats.push_back({0, 0, 0, 4, 1, {1, 2, 3, 4}});
  AutoConstant world;
  world.raw_type = 3;
  world.name = "ACT_WORLDVIEWPROJ_MATRIX";
  world.element_count = 16;
  constants.autos.push_back(world);
  const SetConstants expected_constants = constants;
  s.Event(constants);

  SetBlend blend;
  blend.src = {1, 0};
  s.State(7, blend);
  s.State(7, blend);  // unchanged: not sent again

  Draw draw;
  draw.vertex_count = 3;
  draw.vertex_buffers.push_back({{ResourceKind::kVertexBuffer, 0x100, 1}, 0x1111, 1, 0, 48});
  draw.index_buffer = BufferSnapshot{{ResourceKind::kIndexBuffer, 0x200, 1}, 0x2222, 1, 0, 6};
  const Draw expected_draw = draw;
  s.DrawEvent(draw, {0xAAAA, 0xBBBB});

  s.OnSwapBegin();
  auto frame = queue.Pop(std::chrono::milliseconds(100));
  Check(frame.has_value(), "frame cut at the swap");
  const auto& cmds = frame->commands;
  Check(cmds.size() == 4, "constants, blend once, draw, present");
  const auto* c = As<SetConstants>(cmds[0]);
  Check(c && *c == expected_constants, "constants as handed in");
  Check(c->autos[0].name == "ACT_WORLDVIEWPROJ_MATRIX", "auto constant name kept");
  Check(As<SetBlend>(cmds[1]) && *As<SetBlend>(cmds[1]) == blend, "state sent once");
  const auto* d = As<Draw>(cmds[2]);
  Check(d && d->vertex_count == expected_draw.vertex_count &&
            d->vertex_buffers.size() == 1 && d->vertex_buffers[0].buffer ==
            expected_draw.vertex_buffers[0].buffer,
        "draw as handed in");
  Check(d->vertex_buffers[0].blob == 0xAAAA && d->index_buffer && d->index_buffer->blob == 0xBBBB,
        "live content keys in the draw's snapshots");
  Check(As<Present>(cmds[3]) != nullptr, "present closes the frame");

  // Early out of state hooks (Session::Unchanged / Remember).
  {
    const uint32_t key = 42;
    const uint32_t raw_a[] = {1, 2, 3}, raw_b[] = {1, 2, 4};
    Check(!s.Unchanged(44, 5, raw_a), "nothing remembered yet");
    s.State(key, SetSamplerFilter{5, {1, 0}, {2, 0}});
    s.Remember(44, 5, key, raw_a);
    Check(s.Unchanged(44, 5, raw_a), "same raw words: early out");
    Check(!s.Unchanged(44, 5, raw_b), "other raw words: built again");
    Check(!s.Unchanged(44, 6, raw_a), "another sub of the slot is separate");
    // Another hook writes the same shadow entry (e.g. separate blending after plain blending).
    s.State(key, SetSamplerFilter{5, {1, 0}, {3, 0}});
    Check(!s.Unchanged(44, 5, raw_a), "entry rewritten by another hook: no early out");
    s.Remember(44, 5, key, raw_a);
    s.EraseState(key);
    Check(!s.Unchanged(44, 5, raw_a), "erased entry: no early out");
    // Raw words read through a pointer: the same address with other contents is other words.
    const uint32_t blend_at_address[] = {0, 0x1000, 7}, blend_changed[] = {0, 0x1000, 8};
    s.State(43, SetBlend{});
    s.Remember(43, 0, 43, blend_at_address);
    Check(!s.Unchanged(43, 0, blend_changed), "same address, new contents: built again");
  }
  // A texture freed and another created at the same address (next generation): _setTexture has no
  // early out, and the shadow compares the identity with its generation, so it is sent.
  {
    queue.Pop(std::chrono::milliseconds(0));
    const ResourceId t1{ResourceKind::kTexture, 0x9000, 1}, t2{ResourceKind::kTexture, 0x9000, 2};
    s.State(77, SetTexture{0, true, false, t1}, 0x9000);
    s.State(77, SetTexture{0, true, false, t2}, 0x9000);
    s.OnSwapBegin();
    auto reused = queue.Pop(std::chrono::milliseconds(100));
    size_t textures = 0;
    for (const auto& cmd : reused->commands) textures += As<SetTexture>(cmd) != nullptr;
    Check(textures == 2, "texture at a reused address sent again");
  }
  // Live buffer cache: a buffer snapshot taken by several draws of a frame goes into the frame
  // once, again in the next frame, and a write by the guest makes a new version.
  {
    queue.Pop(std::chrono::milliseconds(0));
    const uint32_t address = 0xA000;
    const ResourceId vb{ResourceKind::kVertexBuffer, address, 1};
    capture::BufferInfo info;
    info.id = vb;
    info.element_size = 12;
    info.count = 4;
    const uint8_t bytes1[48] = {1}, bytes2[48] = {2};
    s.AddVertexBuffer(info);
    s.AddVertexBuffer(info);
    auto a1 = s.RecordContent(vb, address, bytes1, sizeof(bytes1), BlobEndian::kVertexFetch, 2);
    auto a2 = s.RecordContent(vb, address, bytes1, sizeof(bytes1), BlobEndian::kVertexFetch, 2);
    Check(a1.live == a2.live, "same version: same snapshot");
    s.OnSwapBegin();
    auto f1 = queue.Pop(std::chrono::milliseconds(100));
    Check(f1 && f1->contents.size() == 1, "snapshot once in the frame");
    Check(f1->vertex_buffers.size() == 1, "description once");
    auto a3 = s.RecordContent(vb, address, bytes1, sizeof(bytes1), BlobEndian::kVertexFetch, 2);
    s.OnContentWritten(address);
    auto a4 = s.RecordContent(vb, address, bytes2, sizeof(bytes2), BlobEndian::kVertexFetch, 2);
    Check(a3.live == a1.live && a4.live != a1.live, "a write makes a new version");
    s.OnSwapBegin();
    auto f2 = queue.Pop(std::chrono::milliseconds(100));
    Check(f2 && f2->contents.size() == 2, "the next frame holds both versions it used");
    Check(f2->vertex_buffers.empty(), "description not sent again");
    Check(f2->contents[1].content->bytes[0] == 2, "new version's bytes");
  }
  // A capture's baseline re-reads shadow entries: early outs recorded before no longer apply.
  {
    const uint32_t raw[] = {9};
    s.State(55, SetCull{{1, 0}});
    s.Remember(61, 0, 55, raw);
    Check(s.Unchanged(61, 0, raw), "remembered");
    s.Configure("session_test_capture", 1);
    s.RequestCapture();
    s.OnSwapBegin();  // the capture starts: baseline from the shadow
    Check(!s.Unchanged(61, 0, raw), "after a capture's baseline: built again");
  }

  // Interned names compare by value and keep their storage.
  std::string_view a = InternName(std::string("ACT_VIEW_MATRIX"));
  std::string_view b = InternName("ACT_VIEW_MATRIX");
  Check(a == "ACT_VIEW_MATRIX" && a.data() == b.data(), "one interned copy per name");
  std::printf("session test: ok\n");
  return 0;
}

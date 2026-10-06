// Round-trip and robustness tests for the capture serialization.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#include "commands/serialize.h"

namespace {

using namespace torchlight::commands;

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

template <typename E>
Enum<E> Known(E v, uint32_t raw) {
  return Enum<E>{static_cast<uint8_t>(v), raw};
}

ResourceId Id(ResourceKind k, uint32_t a, uint32_t g) { return ResourceId{k, a, g}; }

template <size_t... I>
void AddEveryCommand(Frame& f, std::index_sequence<I...>) {
  (f.commands.push_back(Command{0, CommandPayload(std::in_place_index<I>)}), ...);
}

Capture MakeCapture() {
  Capture c;
  c.meta.first_swap = 1234;
  c.meta.frame_count = 2;
  c.meta.created_utc = "2026-10-01T15:00:00Z";
  c.meta.notes = "test";
  c.vertex_buffers.push_back({Id(ResourceKind::kVertexBuffer, 0x40001000, 3), 32, 100, 5});
  c.index_buffers.push_back({Id(ResourceKind::kIndexBuffer, 0x40002000, 1), 2, 300, 6});
  VertexDeclarationContent decl;
  decl.id = Id(ResourceKind::kVertexDeclaration, 0x40003000, 2);
  decl.content = 0x1122334455667788ull;
  decl.elements.push_back({0, 12, 1, Known(VertexType::kFloat3, 2),
                           Known(VertexSemantic::kNormal, 4)});
  decl.elements.push_back({1, 0, 0, Enum<VertexType>{kUnknown, 99},
                           Known(VertexSemantic::kPosition, 1)});
  c.vertex_declarations.push_back(decl);
  TextureDesc tex;
  tex.id = Id(ResourceKind::kTexture, 0x40004000, 7);
  tex.name = "media/textures/ui/frame.dds";
  tex.type = Known(TextureType::k2D, 2);
  tex.width = 512;
  tex.height = 256;
  tex.num_mipmaps = 9;
  tex.raw_pixel_format = 12;
  tex.render_target = true;
  tex.unresolved = UnresolvedReason::kTextureRenderTarget;
  tex.fetch_constant = {1, 2, 3, 4, 5, 6};
  c.textures.push_back(tex);
  c.programs.push_back({Id(ResourceKind::kProgram, 0x40005000, 0), "ui_vs",
                        Known(ProgramStage::kVertex, 0)});
  c.program_sources.push_back({"ui_vs", "hlsl", "main", "vs_3_0", "void main() {}"});
  Blob live_blob;
  live_blob.hash = 0x1111;
  live_blob.bytes = {9, 9};
  c.live_blobs.push_back(live_blob);
  c.live_textures.push_back({tex.id, 0x77, 0x88});
  Blob blob;
  blob.endian = BlobEndian::kVertexFetch;
  blob.endian_raw = 2;
  blob.bytes = {1, 2, 3, 4, 5, 0xFF};
  blob.hash = HashBytes(blob.bytes.data(), blob.bytes.size(), blob.endian, blob.endian_raw);
  c.blobs.push_back(blob);

  Frame f0;
  f0.index = 0;
  f0.first_swap = 1234;
  AddEveryCommand(f0, std::make_index_sequence<std::variant_size_v<CommandPayload>>{});
  SetMatrix m;
  m.kind = Known(MatrixKind::kProjection, 0);
  for (int i = 0; i < 16; ++i) m.m[i] = float(i) * 0.5f;
  f0.commands.push_back({kFromBaseline, m});
  SetBlend blend;
  blend.separate = true;
  blend.src = Known(BlendFactor::kSourceAlpha, 7);
  blend.dst = Known(BlendFactor::kOneMinusSourceAlpha, 9);
  blend.op = Known(BlendOp::kAdd, 0);
  f0.commands.push_back({0, blend});
  SetConstants consts;
  consts.stage = Known(ProgramStage::kFragment, 1);
  consts.mask = 0xFFFF;
  consts.floats.push_back({16, 4, 32, 8, 1, {0x3F800000u, 2, 3, 4, 5, 6, 7, 8}});
  consts.autos.push_back({22, "ACT_WORLDVIEWPROJ_MATRIX", 0, 16, 0, 2});
  consts.transpose_matrices = true;
  f0.commands.push_back({0, consts});
  Draw draw;
  draw.primitive = Known(PrimitiveType::kTriangleList, 4);
  draw.vertex_count = 100;
  draw.indexed = true;
  draw.index_count = 300;
  draw.index_size = 2;
  draw.instance_count = 1;
  draw.invert_winding = true;
  draw.vertex_buffers.push_back({c.vertex_buffers[0].id, blob.hash, 1, 0xC0010000, 3200});
  draw.index_buffer = BufferSnapshot{c.index_buffers[0].id, 0, 0, 0, 0};
  draw.unresolved_mask = 1u << uint32_t(UnresolvedReason::kBufferNoDeviceResource);
  draw.live_keys = {0x1111, 0x2222};
  f0.commands.push_back({0, draw});
  SetTexture st;
  st.unit = 3;
  st.enabled = true;
  st.texture = tex.id;
  f0.commands.push_back({0, st});
  SetGammaRamp gamma;
  gamma.pwl = true;
  gamma.values[0] = 0x0040;
  gamma.values[767] = 0xFFC0;
  f0.commands.push_back({kFromBaseline, gamma});
  c.frames.push_back(f0);
  Frame f1;
  f1.index = 1;
  f1.commands.push_back({0, Present{1236}});
  c.frames.push_back(f1);

  c.stats.slots.push_back({87, "_render", true, "", 5000, 412});
  c.stats.slots.push_back({12, "setAmbientLight", false, "shared empty function", 9, 1});
  c.stats.unresolved.push_back({0, 5, UnresolvedReason::kBufferNotRegistered, "vb 0x1"});
  c.stats.instance_counts_above_one.push_back({0, 7, 4});
  ReferenceImage ref;
  ref.width = 2;
  ref.height = 1;
  ref.stride = 8;
  ref.possibly_misaligned = true;
  ref.wait_ms = 210;
  ref.rgbx = {1, 2, 3, 0, 4, 5, 6, 0};
  c.reference = ref;
  return c;
}

void TestRoundTrip() {
  Capture original = MakeCapture();
  std::vector<uint8_t> bytes = SerializeCapture(original);
  Capture read;
  std::string error;
  Check(DeserializeCapture(bytes, read, error), "deserialize");
  Check(SerializeCapture(read) == bytes, "round trip is byte identical");
  for (const auto& cmd : read.frames[0].commands) {
    if (const auto* c = std::get_if<SetConstants>(&cmd.payload); c && !c->autos.empty()) {
      Check(c->autos[0].name == "ACT_WORLDVIEWPROJ_MATRIX", "auto constant name read back");
    }
  }
  Check(read.frames.size() == 2, "frame count");
  Check(read.frames[0].commands.size() == std::variant_size_v<CommandPayload> + 6,
        "every opcode survives");
  for (size_t i = 0; i < std::variant_size_v<CommandPayload>; ++i) {
    Check(read.frames[0].commands[i].payload.index() == i, "opcode order");
  }
  const auto& m = std::get<SetMatrix>(read.frames[0].commands[std::variant_size_v<CommandPayload>].payload);
  Check(read.frames[0].commands[std::variant_size_v<CommandPayload>].flags == kFromBaseline,
        "flags");
  Check(m.m[15] == 7.5f && m.kind.get() == MatrixKind::kProjection, "matrix payload");
  const auto& d = std::get<Draw>(read.frames[0].commands[std::variant_size_v<CommandPayload> + 3].payload);
  Check(d.live_keys.size() == 2 && d.live_keys[1] == 0x2222, "draw live keys");
  Check(read.live_blobs.size() == 1 && read.live_blobs[0].bytes.size() == 2 &&
            read.live_textures.size() == 1 && read.live_textures[0].live == 0x88,
        "live chunk");
  Check(d.index_count == 300 && d.invert_winding && d.vertex_buffers.size() == 1 &&
            d.vertex_buffers[0].guest_virtual == 0xC0010000 && d.index_buffer.has_value(),
        "draw payload");
  Check(read.vertex_declarations[0].elements[1].type.raw == 99 &&
            !read.vertex_declarations[0].elements[1].type.known(),
        "unknown enum keeps its raw value");
  Check(read.textures[0].name == "media/textures/ui/frame.dds", "texture name");
  const auto* gamma = std::get_if<SetGammaRamp>(&read.frames[0].commands.back().payload);
  Check(gamma && gamma->pwl && gamma->values[0] == 0x0040 && gamma->values[767] == 0xFFC0,
        "gamma ramp");
  Check(read.program_sources.size() == 1 && read.program_sources[0].target == "vs_3_0" &&
            read.program_sources[0].source == "void main() {}",
        "program source");
  Check(read.reference && read.reference->possibly_misaligned && read.reference->rgbx.size() == 8,
        "reference image");
  Check(read.stats.slots[1].note == "shared empty function", "slot note");
}

void TestFile() {
  Capture original = MakeCapture();
  std::string path = (std::filesystem::temp_directory_path() / "commands_test.tlcap").string();
  std::string error;
  Check(WriteCaptureFile(path, original, error), "write file");
  Capture read;
  Check(ReadCaptureFile(path, read, error), "read file");
  Check(SerializeCapture(read) == SerializeCapture(original), "file round trip");
  std::remove(path.c_str());
}

void TestRejections() {
  std::vector<uint8_t> bytes = SerializeCapture(MakeCapture());
  Capture c;
  std::string error;

  std::vector<uint8_t> bad_major = bytes;
  bad_major[8] = 2;
  Check(!DeserializeCapture(bad_major, c, error), "rejects another major version");

  std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
  Check(!DeserializeCapture(truncated, c, error), "rejects truncated file");

  std::vector<uint8_t> not_tlcap = bytes;
  not_tlcap[0] = 'X';
  Check(!DeserializeCapture(not_tlcap, c, error), "rejects wrong magic");

  // An unknown chunk (newer minor version) is skipped.
  std::vector<uint8_t> extra = bytes;
  const uint8_t chunk[] = {'Z', 'Z', 'Z', 'Z', 3, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9};
  extra.insert(extra.end(), std::begin(chunk), std::end(chunk));
  Check(DeserializeCapture(extra, c, error), "skips unknown chunk");
}

void TestUnknownOpcodeSkipped() {
  Capture original;
  Frame f;
  f.commands.push_back({0, BeginFrame{}});
  f.commands.push_back({0, EndFrame{}});
  original.frames.push_back(f);
  std::vector<uint8_t> bytes = SerializeCapture(original);
  // Find the two command headers (BeginFrame then EndFrame, both empty) and give the first an
  // unknown opcode.
  const uint8_t header[] = {1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0};
  auto it = std::search(bytes.begin(), bytes.end(), std::begin(header), std::end(header));
  Check(it != bytes.end(), "found command header");
  if (it == bytes.end()) return;
  *it = 0xEE;
  *(it + 1) = 0x7F;
  Capture read;
  std::string error;
  Check(DeserializeCapture(bytes, read, error), "unknown opcode is not fatal");
  Check(read.frames.size() == 1 && read.frames[0].commands.size() == 1 &&
            std::holds_alternative<EndFrame>(read.frames[0].commands[0].payload),
        "unknown opcode skipped, the rest kept");
}

void TestHash() {
  const uint8_t a[] = {1, 2, 3};
  Check(HashBytes(a, 3, BlobEndian::kVertexFetch, 1) != HashBytes(a, 3, BlobEndian::kVertexFetch, 2),
        "hash includes endianness");
  Check(HashBytes(a, 0, BlobEndian::kGuestCpuBigEndian, 0) != 0, "hash is never 0");
}

// A session frame: the sample capture's first frame and resources, one content with its bytes and
// one written with an earlier frame.
void TestSessionFrame() {
  Capture c = MakeCapture();
  SessionFrame f;
  f.swap = 77;
  f.dropped_before = 2;
  f.commands = c.frames[0].commands;
  f.vertex_buffers = c.vertex_buffers;
  f.index_buffers = c.index_buffers;
  f.declarations = c.vertex_declarations;
  f.textures = c.textures;
  f.programs = c.programs;
  f.program_sources.push_back({"p", "hlsl", "main", "vs_3_0", "void main() {}"});
  f.destroyed.push_back(c.textures[0].id);
  SessionContent with_bytes;
  with_bytes.id = c.vertex_buffers[0].id;
  with_bytes.blob = c.blobs[0];
  SessionContent by_key;
  by_key.id = c.textures[0].id;
  by_key.blob.hash = 0x1234;
  by_key.bytes_in_record = false;
  f.contents = {with_bytes, by_key};
  std::vector<uint8_t> bytes = SerializeSessionFrame(f);
  SessionFrame read;
  std::string error;
  Check(DeserializeSessionFrame(bytes.data(), bytes.size(), read, error), "session frame reads");
  Check(SerializeSessionFrame(read) == bytes, "session frame round trip is byte identical");
  Check(read.swap == 77 && read.dropped_before == 2, "session frame header");
  Check(read.commands.size() == c.frames[0].commands.size(), "session frame commands");
  Check(read.contents.size() == 2 && read.contents[0].blob.bytes == c.blobs[0].bytes &&
            !read.contents[1].bytes_in_record && read.contents[1].blob.hash == 0x1234,
        "session frame contents");
  Check(!DeserializeSessionFrame(bytes.data(), bytes.size() / 2, read, error),
        "truncated session frame is rejected");
}

}  // namespace

int main() {
  TestSessionFrame();
  TestRoundTrip();
  TestFile();
  TestRejections();
  TestUnknownOpcodeSkipped();
  TestHash();
  if (failures == 0) std::printf("commands test: ok\n");
  return failures == 0 ? 0 : 1;
}

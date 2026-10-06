// Neutral capture format: render commands and resources with no OGRE or PPC types.
//
// A capture is self-contained: the resources it references plus the command stream of one or
// more frames. Guest enum values are translated to the neutral enums below by the hooks; a value
// that has no translation keeps its raw guest value and the neutral field is kUnknown.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace torchlight::commands {

inline constexpr uint32_t kFormatVersionMajor = 1;
// 1.1: SetTextureBlend, 1.2: transpose flag, 1.3: program sources (chunk PSRC),
// 1.4: SetGammaRamp, 1.5: live content keys (Draw::live_keys, chunk LIVE), 1.6: SetSceneClip.
inline constexpr uint32_t kFormatVersionMinor = 6;
inline constexpr uint8_t kUnknown = 0xFF;

// ---------------------------------------------------------------------------------------------
// Neutral enums

enum class PrimitiveType : uint8_t {
  kPointList, kLineList, kLineStrip, kTriangleList, kTriangleStrip, kTriangleFan,
};
enum class BlendFactor : uint8_t {
  kOne, kZero, kDestColour, kSourceColour, kOneMinusDestColour, kOneMinusSourceColour,
  kDestAlpha, kSourceAlpha, kOneMinusDestAlpha, kOneMinusSourceAlpha,
};
enum class BlendOp : uint8_t { kAdd, kSubtract, kReverseSubtract, kMin, kMax };
enum class CompareFunc : uint8_t {
  kNever, kAlways, kLess, kLessEqual, kEqual, kNotEqual, kGreaterEqual, kGreater,
};
enum class CullMode : uint8_t { kNone, kClockwise, kAnticlockwise };
enum class FilterStage : uint8_t { kMin, kMag, kMip };
enum class Filter : uint8_t { kNone, kPoint, kLinear, kAnisotropic };
enum class AddressMode : uint8_t { kWrap, kMirror, kClamp, kBorder };
enum class PolygonMode : uint8_t { kPoints, kWireframe, kSolid };
enum class StencilOp : uint8_t {
  kKeep, kZero, kReplace, kIncrement, kDecrement, kIncrementWrap, kDecrementWrap, kInvert,
};
enum class VertexType : uint8_t {
  kFloat1, kFloat2, kFloat3, kFloat4, kColour, kShort1, kShort2, kShort3, kShort4, kUbyte4,
  kColourArgb, kColourAbgr,
};
enum class VertexSemantic : uint8_t {
  kPosition, kBlendWeights, kBlendIndices, kNormal, kDiffuse, kSpecular, kTextureCoordinates,
  kBinormal, kTangent,
};
enum class ProgramStage : uint8_t { kVertex, kFragment, kGeometry };
enum class MatrixKind : uint8_t { kWorld, kView, kProjection, kTexture };
enum class ResourceKind : uint8_t {
  kVertexBuffer, kIndexBuffer, kTexture, kVertexDeclaration, kProgram,
};
enum class TextureType : uint8_t { k1D, k2D, k3D, kCubeMap };

// Byte order of a blob as the guest stored it. Nothing is swapped at capture time.
enum class BlobEndian : uint8_t {
  kGuestCpuBigEndian,  // written by the guest CPU in its native order (endian_raw unused)
  kVertexFetch,        // endian_raw = the 2-bit swap mode of the vertex fetch constant
  kTextureFetch,       // endian_raw = the 2-bit swap mode of the texture fetch constant
};

enum class UnresolvedReason : uint16_t {
  kNone = 0,
  kUnknownEnumValue,        // a guest enum value with no neutral translation
  kBufferNotRegistered,     // a draw referenced a buffer whose creation was never seen
  kBufferNoDeviceResource,  // no per-device resource and no system memory copy
  kBufferOutOfRange,        // fetch constant address/size outside guest memory
  kTextureNoName,           // texture without name and without readable content
  kTextureRenderTarget,     // content produced by the GPU inside the frame
  kTextureUnknownFormatSize,  // raw dump impossible: no size known for the Xenos format
  kTextureNoDeviceResource,
  kProgramNotBound,         // draw without a bound program on a stage
  kNullPointer,             // a guest pointer argument was null
  kStateNotObserved,        // state never set since the process started
};

// Neutral value plus the raw guest value it came from.
template <typename E>
struct Enum {
  uint8_t value = kUnknown;
  uint32_t raw = 0;
  bool known() const { return value != kUnknown; }
  E get() const { return static_cast<E>(value); }
  bool operator==(const Enum&) const = default;
};

struct ResourceId {
  ResourceKind kind = ResourceKind::kVertexBuffer;
  uint32_t guest_address = 0;
  uint32_t generation = 0;
  bool operator==(const ResourceId&) const = default;
};

using Hash = uint64_t;  // FNV-1a 64 of blob bytes + endianness

// ---------------------------------------------------------------------------------------------
// Commands

// guest _beginFrame
struct BeginFrame {
  bool operator==(const BeginFrame&) const = default;
};
// guest _endFrame (several may happen per presentation)
struct EndFrame {
  bool operator==(const EndFrame&) const = default;
};
struct Present {         // guest device swap (VdSwap); closes a captured frame
  uint64_t swap_number = 0;
  bool operator==(const Present&) const = default;
};
struct SetMatrix {
  Enum<MatrixKind> kind;
  uint32_t texture_unit = 0;    // for kTexture
  std::array<float, 16> m{};    // OGRE row-major m[row][col], guest values
  bool operator==(const SetMatrix&) const = default;
};
struct SetWorldMatrices {
  std::vector<std::array<float, 16>> matrices;
  bool operator==(const SetWorldMatrices&) const = default;
};
struct SetRenderTarget {
  uint32_t guest_address = 0;
  std::string name;
  uint32_t width = 0, height = 0, colour_depth = 0;
  bool requires_flipping = false;
  bool operator==(const SetRenderTarget&) const = default;
};
struct SetViewport {
  uint32_t guest_address = 0;
  uint32_t target_guest_address = 0;
  int32_t left = 0, top = 0, width = 0, height = 0;  // OGRE actual rectangle (not flipped)
  bool operator==(const SetViewport&) const = default;
};
struct Clear {
  uint32_t buffers = 0;  // bit0 colour, bit1 depth, bit2 stencil
  std::array<float, 4> colour{};
  float depth = 0;
  uint32_t stencil = 0;
  bool operator==(const Clear&) const = default;
};
struct SetTexture {
  uint32_t unit = 0;
  bool enabled = false;
  bool vertex_texture = false;
  std::optional<ResourceId> texture;
  bool operator==(const SetTexture&) const = default;
};
struct DisableTextureUnit {
  uint32_t unit = 0;
  bool operator==(const DisableTextureUnit&) const = default;
};
struct SetSamplerFilter {
  uint32_t unit = 0;
  Enum<FilterStage> stage;
  Enum<Filter> filter;
  bool operator==(const SetSamplerFilter&) const = default;
};
struct SetSamplerAddress {
  uint32_t unit = 0;
  std::array<Enum<AddressMode>, 3> uvw{};
  bool operator==(const SetSamplerAddress&) const = default;
};
struct SetSamplerAnisotropy {
  uint32_t unit = 0;
  uint32_t max_anisotropy = 0;
  bool operator==(const SetSamplerAnisotropy&) const = default;
};
struct SetSamplerMipBias {
  uint32_t unit = 0;
  float bias = 0;
  bool operator==(const SetSamplerMipBias&) const = default;
};
struct SetSamplerBorder {
  uint32_t unit = 0;
  std::array<float, 4> colour{};
  bool operator==(const SetSamplerBorder&) const = default;
};
struct SetTexCoordSet {
  uint32_t unit = 0;
  uint32_t index = 0;
  bool operator==(const SetTexCoordSet&) const = default;
};
struct SetTexCoordCalc {
  uint32_t unit = 0;
  uint32_t raw_method = 0;  // TexCoordCalcMethod is not translated: fixed function only
  bool operator==(const SetTexCoordCalc&) const = default;
};
struct SetBlend {
  bool separate = false;
  Enum<BlendFactor> src, dst, src_alpha, dst_alpha;
  Enum<BlendOp> op, op_alpha;
  bool operator==(const SetBlend&) const = default;
};
struct SetDepthCheck {
  bool enabled = false;
  bool operator==(const SetDepthCheck&) const = default;
};
struct SetDepthWrite {
  bool enabled = false;
  bool operator==(const SetDepthWrite&) const = default;
};
struct SetDepthFunc {
  Enum<CompareFunc> requested;  // what OGRE asked for
  Enum<CompareFunc> effective;  // after the guest's Runic depth inversion
  bool operator==(const SetDepthFunc&) const = default;
};
struct SetDepthBias {
  float constant = 0, slope_scale = 0;
  bool operator==(const SetDepthBias&) const = default;
};
struct SetCull {
  Enum<CullMode> mode;
  bool operator==(const SetCull&) const = default;
};
struct SetAlphaReject {
  Enum<CompareFunc> func;
  uint32_t reference = 0;
  bool alpha_to_coverage = false;
  bool operator==(const SetAlphaReject&) const = default;
};
struct SetColourWrite {
  bool r = true, g = true, b = true, a = true;
  bool operator==(const SetColourWrite&) const = default;
};
struct SetPolygonMode {
  Enum<PolygonMode> mode;
  bool operator==(const SetPolygonMode&) const = default;
};
struct SetStencilCheck {
  bool enabled = false;
  bool operator==(const SetStencilCheck&) const = default;
};
struct SetStencil {
  Enum<CompareFunc> func;
  uint32_t reference = 0, mask = 0;
  Enum<StencilOp> fail, depth_fail, pass;
  bool two_sided = false;
  bool operator==(const SetStencil&) const = default;
};
struct SetScissor {
  bool enabled = false;
  uint32_t left = 0, top = 0, right = 0, bottom = 0;
  bool operator==(const SetScissor&) const = default;
};
struct SetClipPlanes {
  std::vector<std::array<float, 4>> planes;  // normal xyz, d
  bool operator==(const SetClipPlanes&) const = default;
};
struct SetPointSprites {
  bool enabled = false;
  bool operator==(const SetPointSprites&) const = default;
};
struct SetInvertWinding {
  bool invert = false;
  bool operator==(const SetInvertWinding&) const = default;
};
struct SetDeriveDepthBias {
  bool derive = false;
  float base = 0, multiplier = 0, slope_scale = 0;
  bool operator==(const SetDeriveDepthBias&) const = default;
};
struct SetPassIterationCount {
  uint32_t count = 0;
  bool operator==(const SetPassIterationCount&) const = default;
};
struct SetVertexDeclaration {
  std::optional<ResourceId> declaration;
  Hash content = 0;  // VertexDeclarationContent hash
  bool operator==(const SetVertexDeclaration&) const = default;
};
struct VertexStream {
  uint32_t stream = 0;
  std::optional<ResourceId> buffer;
  bool operator==(const VertexStream&) const = default;
};
struct SetVertexBuffers {
  std::vector<VertexStream> streams;
  bool operator==(const SetVertexBuffers&) const = default;
};
struct BindProgram {
  Enum<ProgramStage> stage;
  ResourceId program;
  bool operator==(const BindProgram&) const = default;
};
struct UnbindProgram {
  Enum<ProgramStage> stage;
  bool operator==(const UnbindProgram&) const = default;
};
// Constants uploaded by bindGpuProgramParameters, in the guest D3D9 register convention.
struct ConstantRange {
  uint32_t logical_index = 0;  // floats: element index (register = logical_index / 4)
  uint32_t register_index = 0;
  uint32_t physical_index = 0;  // index into the guest constant array (auto constants use it)
  uint32_t element_count = 0;   // floats or ints
  uint16_t variability = 0;
  std::vector<uint32_t> data;  // raw 32-bit values (float bits or ints), host order
  bool operator==(const ConstantRange&) const = default;
};
struct AutoConstant {
  uint32_t raw_type = 0;   // guest AutoConstantType value
  // From guest_abi (ACT_*), empty if out of range. Static storage (guest_abi's table, or
  // InternName when read from a file): recorded hundreds of times per frame without copying.
  std::string_view name;
  uint32_t physical_index = 0;
  uint32_t element_count = 0;
  uint32_t data = 0;
  uint16_t variability = 0;
  bool operator==(const AutoConstant&) const = default;
};
struct SetConstants {
  Enum<ProgramStage> stage;
  uint16_t mask = 0;
  uint32_t parameters_guest_address = 0;
  std::vector<ConstantRange> floats;
  std::vector<ConstantRange> ints;
  std::vector<AutoConstant> autos;
  // GpuProgramParameters::mTransposeMatrices: matrices in `floats` were written transposed.
  // Absent in captures older than 1.2.
  std::optional<bool> transpose_matrices;
  bool operator==(const SetConstants&) const = default;
};
struct BufferSnapshot {
  ResourceId buffer;
  Hash blob = 0;          // 0 = no snapshot (see unresolved)
  uint32_t source = 0;    // 0 none, 1 device fetch constant, 2 system memory copy
  uint32_t guest_virtual = 0;
  uint32_t size = 0;
  bool operator==(const BufferSnapshot&) const = default;
};
struct Draw {
  Enum<PrimitiveType> primitive;
  uint32_t vertex_start = 0, vertex_count = 0;
  bool indexed = false;
  uint32_t index_start = 0, index_count = 0;
  uint32_t index_size = 0;            // 2 or 4
  uint32_t instance_count = 0;        // Runic RenderOperation +0x14
  bool invert_winding = false;        // render system +0x290 at the draw
  bool target_flipping = false;       // active render target requires flipping
  uint32_t pass_iteration_count = 0;  // render system +0x298 at the draw
  std::vector<BufferSnapshot> vertex_buffers;
  std::optional<BufferSnapshot> index_buffer;
  uint32_t unresolved_mask = 0;       // bit per UnresolvedReason
  // 1.5, captures taken in the live mode: the content key the live stream used for each buffer
  // snapshot (vertex buffers, then the index buffer); see Capture::live_blobs.
  std::vector<Hash> live_keys;
  bool operator==(const Draw&) const = default;
};

// Fixed-function texture combination (_setTextureBlendMode). Guest values are kept raw: only
// "is plain modulate" is translated, through guest_abi.
struct SetTextureBlend {
  uint32_t unit = 0;
  uint32_t raw_blend_type = 0;  // 0 colour, 1 alpha
  uint32_t raw_operation = 0;
  uint32_t raw_source1 = 0, raw_source2 = 0;
  bool is_modulate = false;
  std::array<float, 4> colour_arg1{}, colour_arg2{};
  float alpha_arg1 = 0, alpha_arg2 = 0, factor = 0;
  bool operator==(const SetTextureBlend&) const = default;
};

// Display gamma ramp D3D writes to the GPU (DC_LUT registers) when it presents. Values as the guest
// stored them: table form R[256], G[256], B[256]; PWL form {base, delta}[128] per channel in the
// same R, G, B order (guest_abi d3d_gamma_ramp).
struct SetGammaRamp {
  bool pwl = false;
  std::array<uint16_t, 768> values{};
  bool operator==(const SetGammaRamp&) const = default;
};

// The front end's 3D scene on a frame wider than 16:9 (host decision, not a guest call): the draws
// and clears of the viewport `viewport` (its guest address) are limited to the frame's centred 16:9
// strip, the rest of the frame black; viewport 0 = no clip.
struct SetSceneClip {
  uint32_t viewport = 0;
  bool operator==(const SetSceneClip&) const = default;
};

using CommandPayload = std::variant<
    BeginFrame, EndFrame, Present, SetMatrix, SetWorldMatrices, SetRenderTarget, SetViewport,
    Clear, SetTexture, DisableTextureUnit, SetSamplerFilter, SetSamplerAddress,
    SetSamplerAnisotropy, SetSamplerMipBias, SetSamplerBorder, SetTexCoordSet, SetTexCoordCalc,
    SetBlend, SetDepthCheck, SetDepthWrite, SetDepthFunc, SetDepthBias, SetCull, SetAlphaReject,
    SetColourWrite, SetPolygonMode, SetStencilCheck, SetStencil, SetScissor, SetClipPlanes,
    SetPointSprites, SetInvertWinding, SetDeriveDepthBias, SetPassIterationCount,
    SetVertexDeclaration, SetVertexBuffers, BindProgram, UnbindProgram, SetConstants, Draw,
    SetTextureBlend, SetGammaRamp, SetSceneClip>;

// Opcode = variant index + 1. Never reorder; append only (bump the minor version).
inline constexpr uint16_t OpcodeOf(const CommandPayload& p) {
  return static_cast<uint16_t>(p.index() + 1);
}

// Names by opcode - 1, in the variant's order (summaries).
inline constexpr const char* kOpcodeNames[] = {
    "BeginFrame", "EndFrame", "Present", "SetMatrix", "SetWorldMatrices", "SetRenderTarget",
    "SetViewport", "Clear", "SetTexture", "DisableTextureUnit", "SetSamplerFilter",
    "SetSamplerAddress", "SetSamplerAnisotropy", "SetSamplerMipBias", "SetSamplerBorder",
    "SetTexCoordSet", "SetTexCoordCalc", "SetBlend", "SetDepthCheck", "SetDepthWrite",
    "SetDepthFunc", "SetDepthBias", "SetCull", "SetAlphaReject", "SetColourWrite",
    "SetPolygonMode", "SetStencilCheck", "SetStencil", "SetScissor", "SetClipPlanes",
    "SetPointSprites", "SetInvertWinding", "SetDeriveDepthBias", "SetPassIterationCount",
    "SetVertexDeclaration", "SetVertexBuffers", "BindProgram", "UnbindProgram", "SetConstants",
    "Draw", "SetTextureBlend", "SetGammaRamp", "SetSceneClip"};
static_assert(std::size(kOpcodeNames) == std::variant_size_v<CommandPayload>,
              "one name per command type");
inline constexpr size_t kOpcodeCount = std::size(kOpcodeNames);

enum CommandFlags : uint16_t {
  kFromBaseline = 1u << 0,  // emitted from the state shadow when the capture started
};

struct Command {
  uint16_t flags = 0;
  CommandPayload payload;
};

struct Frame {
  uint32_t index = 0;
  uint64_t first_swap = 0;
  std::vector<Command> commands;
};

// ---------------------------------------------------------------------------------------------
// Resources

struct VertexBufferDesc {
  ResourceId id;
  uint32_t vertex_size = 0, num_vertices = 0, usage = 0;
};
struct IndexBufferDesc {
  ResourceId id;
  uint32_t index_size = 0, num_indexes = 0, usage = 0;
};
struct VertexElement {
  uint32_t source = 0, offset = 0, index = 0;
  Enum<VertexType> type;
  Enum<VertexSemantic> semantic;
};
struct VertexDeclarationContent {
  ResourceId id;
  Hash content = 0;
  std::vector<VertexElement> elements;
};
struct TextureDesc {
  ResourceId id;
  std::string name;
  Enum<TextureType> type;
  uint32_t width = 0, height = 0, depth = 0, num_mipmaps = 0;
  uint32_t raw_pixel_format = 0;  // guest PixelFormat, not translated
  uint32_t usage = 0;
  bool render_target = false, manual = false;
  // Raw Xenos content (base level, tiled) for dynamic or unnamed textures.
  Hash content = 0;
  std::array<uint32_t, 6> fetch_constant{};
  UnresolvedReason unresolved = UnresolvedReason::kNone;
};
struct ProgramDesc {
  ResourceId id;
  std::string name;
  Enum<ProgramStage> stage;
};
// Source of a high-level program as the guest created it (RTSS ProgramManager::createGpuProgram),
// matched to ProgramDesc by name. Recorded at creation, which happens before a capture starts.
struct ProgramSource {
  std::string name;
  std::string language;     // "hlsl" for every program the guest D3D9 render system compiles
  std::string entry_point;  // empty for languages other than hlsl
  std::string target;       // HLSL profile (e.g. vs_3_0); empty for other languages
  std::string source;
};
struct Blob {
  Hash hash = 0;
  BlobEndian endian = BlobEndian::kGuestCpuBigEndian;
  uint8_t endian_raw = 0;
  std::vector<uint8_t> bytes;
};

// ---------------------------------------------------------------------------------------------
// Statistics, reference image and the capture itself

struct SlotCount {
  uint32_t slot = 0;
  std::string name;
  bool attributable = true;
  std::string note;  // why it is not attributable
  uint64_t total = 0;    // since process start
  uint64_t captured = 0;  // during the captured frames
};
struct UnresolvedEntry {
  uint32_t frame = 0, command = 0;
  UnresolvedReason reason = UnresolvedReason::kNone;
  std::string detail;
};
struct InstanceCountEntry {
  uint32_t frame = 0, command = 0, instance_count = 0;
};
struct Stats {
  std::vector<SlotCount> slots;
  std::vector<UnresolvedEntry> unresolved;
  std::vector<InstanceCountEntry> instance_counts_above_one;
};
struct ReferenceImage {
  uint32_t width = 0, height = 0, stride = 0;
  bool possibly_misaligned = false;
  uint32_t wait_ms = 0;
  std::vector<uint8_t> rgbx;
};
struct Meta {
  uint32_t version_major = kFormatVersionMajor, version_minor = kFormatVersionMinor;
  uint64_t first_swap = 0;
  uint32_t frame_count = 0;
  std::string created_utc;
  std::string notes;
};

// 1.5: content of a texture as the live stream had it, next to the capture's.
struct LiveTextureContent {
  ResourceId id;
  Hash capture = 0, live = 0;
};

struct Capture {
  Meta meta;
  std::vector<VertexBufferDesc> vertex_buffers;
  std::vector<IndexBufferDesc> index_buffers;
  std::vector<VertexDeclarationContent> vertex_declarations;
  std::vector<TextureDesc> textures;
  std::vector<ProgramDesc> programs;
  std::vector<ProgramSource> program_sources;  // 1.3+
  // 1.5, captures taken in the live mode: the content the live stream (the backend window) used,
  // by live content key, to replay what the window showed (chunk LIVE).
  std::vector<Blob> live_blobs;
  std::vector<LiveTextureContent> live_textures;
  std::vector<Blob> blobs;
  std::vector<Frame> frames;
  Stats stats;
  std::optional<ReferenceImage> reference;
};

Hash HashBytes(const uint8_t* data, size_t size, BlobEndian endian, uint8_t endian_raw);
// A string with static storage equal to `s` (one per distinct value, kept for the process).
std::string_view InternName(std::string_view s);
const char* ReasonName(UnresolvedReason reason);

}  // namespace torchlight::commands

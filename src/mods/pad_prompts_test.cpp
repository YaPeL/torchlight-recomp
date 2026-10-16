// Tests for the pad prompt styles (pad_prompts.h) on synthetic icons, textures, imagesets and paks.

#include "mods/pad_prompts.h"

#include <miniz.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace m = torchlight::mods;
namespace fs = std::filesystem;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

m::RgbaImage Solid(uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  m::RgbaImage img{w, h, {}};
  for (uint32_t i = 0; i < w * h; ++i) img.rgba.insert(img.rgba.end(), {r, g, b, a});
  return img;
}

const uint8_t* Pixel(const m::RgbaImage& img, uint32_t x, uint32_t y) { return &img.rgba[(size_t(y) * img.width + x) * 4]; }

std::string Imageset(const std::string& texture) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Imageset Name=\"w\" Imagefile=\"" + texture +
         "\" NativeHorzRes=\"1024\" NativeVertRes=\"768\" AutoScaled=\"true\" >\n"
         "\t<Image Name=\"frame\" XPos=\"0\" YPos=\"0\" Width=\"4\" Height=\"4\" />\n"
         "\t<Image Name=\"a_button\" XPos=\"8\" YPos=\"0\" Width=\"8\" Height=\"4\" />\n"
         "</Imageset>\n";
}

void WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::vector<uint8_t> ReadAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string Text(const m::PromptIcon* icon) { return icon ? icon->file : ""; }

const m::PromptIcon* Find(const std::vector<m::PromptIcon>& icons, const char* image) {
  for (const auto& i : icons) {
    if (i.image == image) return &i;
  }
  return nullptr;
}

}  // namespace

int main() {
  // The style in effect.
  {
    using S = m::PadStyle;
    using K = m::PadKind;
    Check(m::EffectivePadStyle(std::nullopt, K::kPlayStation) == S::kPlayStation, "auto: a PlayStation pad");
    Check(m::EffectivePadStyle(std::nullopt, K::kNintendo) == S::kNintendo, "auto: a Nintendo pad");
    Check(m::EffectivePadStyle(std::nullopt, K::kOther) == S::kXbox, "auto: another pad, Xbox");
    Check(m::EffectivePadStyle(std::nullopt, K::kNone) == S::kXbox, "auto: no pad, Xbox");
    Check(m::EffectivePadStyle(S::kNintendo, K::kPlayStation) == S::kNintendo, "a chosen style wins");
    Check(m::SwapsConfirmCancel(S::kNintendo, true) && !m::SwapsConfirmCancel(S::kNintendo, false) &&
              !m::SwapsConfirmCancel(S::kPlayStation, true) && !m::SwapsConfirmCancel(S::kXbox, true),
          "the swap only with Nintendo's style");
  }
  // Confirm and cancel swapped in what the guest reads.
  {
    Check(m::SwapButtonBits(0x1000, 0x1000, 0x2000) == 0x2000 && m::SwapButtonBits(0x2000, 0x1000, 0x2000) == 0x1000,
          "A becomes B and B becomes A");
    Check(m::SwapButtonBits(0x3010, 0x1000, 0x2000) == 0x3010 && m::SwapButtonBits(0x4001, 0x1000, 0x2000) == 0x4001,
          "both or neither: unchanged, other buttons kept");
    Check(m::SwapVirtualKey(0x5800, 0x5800, 0x5801) == 0x5801 && m::SwapVirtualKey(0x5801, 0x5800, 0x5801) == 0x5800 &&
              m::SwapVirtualKey(0x5802, 0x5800, 0x5801) == 0x5802,
          "keystrokes: A <-> B only");
  }
  // The icons of each style.
  {
    Check(m::PromptIcons(m::PadStyle::kXbox, true).empty(), "Xbox: the game's images");
    const auto ps = m::PromptIcons(m::PadStyle::kPlayStation, true);
    Check(ps.size() == 11 && Text(Find(ps, "controller")) == "controller" && Text(Find(ps, "a_button")) == "cross" && Text(Find(ps, "b_button")) == "circle" &&
              Text(Find(ps, "button_x")) == "square" && Text(Find(ps, "y_button")) == "triangle",
          "PlayStation: the face buttons by position, the swap ignored");
    const auto by_position = m::PromptIcons(m::PadStyle::kNintendo, false);
    Check(Text(Find(by_position, "a_button")) == "b" && Text(Find(by_position, "b_button")) == "a" &&
              Text(Find(by_position, "button_x")) == "y" && Text(Find(by_position, "y_button")) == "x",
          "Nintendo by position: each prompt shows its button's own letter");
    const auto swapped = m::PromptIcons(m::PadStyle::kNintendo, true);
    Check(Text(Find(swapped, "a_button")) == "a" && Text(Find(swapped, "b_button")) == "b" &&
              Text(Find(swapped, "button_x")) == "y" && Text(Find(swapped, "controller")) == "controller",
          "Nintendo swapped: confirm is A, cancel B");
    Check(!Find(ps, "dpad_up") && !Find(ps, "analog_button_left_lr"), "the d-pad and sticks stay the game's");
  }
  // Fitting: aspect kept, centred, transparent around; colour kept on edges.
  {
    const auto wide = m::FitIcon(Solid(8, 4, 0, 0, 255), 8, 8);
    Check(wide.width == 8 && wide.height == 8, "the region's size");
    Check(Pixel(wide, 4, 1)[3] == 0 && Pixel(wide, 4, 6)[3] == 0, "transparent above and below a wide icon");
    Check(Pixel(wide, 0, 3)[3] == 255 && Pixel(wide, 7, 4)[2] == 255, "the icon centred, full width");
    m::RgbaImage half = Solid(2, 1, 255, 0, 0);
    half.rgba[4 + 3] = 0;  // the second pixel transparent (and black)
    const auto one = m::FitIcon(half, 1, 1);
    Check(one.rgba[0] == 255 && one.rgba[3] > 120 && one.rgba[3] < 135, "premultiplied: red at half alpha");
  }
  // Patching a texture.
  {
    const auto texture = m::EncodeDdsArgb(Solid(16, 8, 255, 0, 0));
    size_t replaced = 0;
    const auto patched =
        m::PatchPromptTexture(Imageset("t.dds"), texture, {{"a_button", Solid(4, 4, 0, 255, 0)}}, &replaced, nullptr);
    const auto image = patched ? m::DecodeDds(*patched) : std::nullopt;
    Check(image && replaced == 1, "one image redrawn");
    Check(image && Pixel(*image, 1, 1)[0] == 255, "another image untouched");
    Check(image && Pixel(*image, 8, 1)[3] == 0 && Pixel(*image, 15, 1)[3] == 0,
          "the region cleared beside a square icon in a wide place");
    Check(image && Pixel(*image, 12, 2)[1] == 255 && Pixel(*image, 12, 2)[3] == 255, "the icon in the region's centre");
    Check(image && Pixel(*image, 12, 0)[3] == 0 && Pixel(*image, 12, 3)[3] == 0,
          "a transparent border above and below the icon");
    Check(image && Pixel(*image, 12, 6)[0] == 255, "below the region untouched");
    std::string why;
    const std::string outside = "<Imageset Name=\"w\" Imagefile=\"t.dds\"><Image Name=\"a_button\" XPos=\"14\" "
                                "YPos=\"0\" Width=\"8\" Height=\"4\" /></Imageset>";
    Check(!m::PatchPromptTexture(outside, texture, {{"a_button", Solid(4, 4, 0, 255, 0)}}, nullptr, &why) &&
              !why.empty(),
          "an image outside its texture: refused");
  }
  // Files: a synthetic pak and icon folder.
  {
    const fs::path root = fs::temp_directory_path() / "pad_prompts_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    mz_zip_archive zip{};
    Check(mz_zip_writer_init_heap(&zip, 0, 0), "zip writer");
    auto add = [&](const char* name, const std::string& bytes) {
      mz_zip_writer_add_mem(&zip, name, bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION);
    };
    const auto tex = m::EncodeDdsArgb(Solid(16, 8, 255, 0, 0));
    add("media/UI/icons/w.imageset", Imageset("media/ui/icons/W.dds"));
    add("media/UI/icons/w.dds", std::string(tex.begin(), tex.end()));
    add("media/UI/other.imageset", "<Imageset Name=\"o\" Imagefile=\"media/ui/o.dds\"><Image Name=\"frame\" "
                                   "XPos=\"0\" YPos=\"0\" Width=\"4\" Height=\"4\" /></Imageset>");
    void* buffer = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    WriteFile(root / "pak.zip", std::vector<uint8_t>(static_cast<uint8_t*>(buffer), static_cast<uint8_t*>(buffer) + size));
    mz_zip_writer_end(&zip);
    WriteFile(root / "icons" / "playstation" / "cross.dds", m::EncodeDdsArgb(Solid(4, 4, 0, 0, 255)));
    WriteFile(root / "out" / "stale.dds", {1});
    std::vector<std::string> log;
    const auto written =
        m::WritePromptTextures(root / "pak.zip", root / "icons", m::PadStyle::kPlayStation, false, root / "out", log);
    Check(written.size() == 1 && written[0].texture == "media/ui/icons/w.dds" && written[0].replaced == 1,
          "the texture holding a prompt, written at its path");
    Check(!fs::exists(root / "out" / "stale.dds"), "the folder emptied first");
    const auto image = m::DecodeDds(ReadAll(root / "out" / "media/ui/icons/w.dds"));
    Check(image && Pixel(*image, 12, 2)[2] == 255, "the icon drawn into the written texture");
    Check(!log.empty(), "the missing icons logged");
    log.clear();
    Check(m::WritePromptTextures(root / "pak.zip", root / "icons", m::PadStyle::kXbox, false, root / "out", log)
                  .empty() &&
              !fs::exists(root / "out" / "media"),
          "Xbox: nothing written, the folder emptied");
    fs::remove_all(root, ec);
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("pad_prompts_test: ok\n");
  return EXIT_SUCCESS;
}

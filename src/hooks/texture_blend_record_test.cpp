// The fields a SetTextureBlend record keeps (texture_blend_record.h).

#include <cstdio>

#include "hooks/texture_blend_record.h"

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}
}  // namespace

int main() {
  using torchlight::hooks::TextureBlendRecordedFields;
  constexpr uint32_t kColour = 0, kAlpha = 1, kModulate = 2, kBlendManual = 12;
  constexpr uint32_t kCurrent = 0, kTexture = 1, kManual = 4;
  auto f = TextureBlendRecordedFields(kColour, kModulate, kTexture, kCurrent);
  Check(!f.colour_args && !f.alpha_args && !f.factor, "modulate texture by current reads no argument");
  f = TextureBlendRecordedFields(kAlpha, kModulate, kTexture, kCurrent);
  Check(!f.colour_args && !f.alpha_args && !f.factor, "the alpha type likewise");
  f = TextureBlendRecordedFields(kColour, kModulate, kManual, kCurrent);
  Check(f.colour_args && !f.alpha_args && !f.factor, "a manual source reads the colour arguments of a colour mode");
  f = TextureBlendRecordedFields(kAlpha, kModulate, kTexture, kManual);
  Check(!f.colour_args && f.alpha_args && !f.factor, "a manual source reads the alpha arguments of an alpha mode");
  f = TextureBlendRecordedFields(kColour, kBlendManual, kTexture, kCurrent);
  Check(!f.colour_args && !f.alpha_args && f.factor, "blend manual reads the factor");
  f = TextureBlendRecordedFields(kAlpha, kBlendManual, kManual, kManual);
  Check(!f.colour_args && f.alpha_args && f.factor, "both together");
  if (failures) return 1;
  std::puts("texture_blend_record_test: ok");
  return 0;
}

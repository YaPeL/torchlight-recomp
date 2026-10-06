"""Apply the narrow Xenos Vulkan stencil-only ownership transfer fix."""
from pathlib import Path
import difflib
import shutil

root = Path.home() / 'rexglue-sdk'
relative = Path('src/graphics/vulkan/render_target_cache.cpp')
p = root / relative
before = p.read_text()
old = '''    } else if (source_depth_float[0] != spv::NoResult) {
      if (mode.output == TransferOutput::kDepth && dest_depth_format == source_depth_format) {'''
new = '''    } else if (mode.output == TransferOutput::kStencilBit) {
      // Stencil-only transfers don't load depth. Feed the source stencil to
      // the per-bit discard below; otherwise every pass writes its bit and
      // the destination becomes 0xFF regardless of the source value.
      assert_true(source_stencil[0] != spv::NoResult);
      packed = source_stencil[0];
    } else if (source_depth_float[0] != spv::NoResult) {
      if (mode.output == TransferOutput::kDepth && dest_depth_format == source_depth_format) {'''
assert before.count(old) == 1
after = before.replace(old, new)
backup = Path('docs/bringup-artifacts/nvidia-minimap/sdk-originals') / relative
backup.parent.mkdir(parents=True, exist_ok=True)
assert not backup.exists()
shutil.copy2(p, backup)
patch_dir = Path('patches')
patch_dir.mkdir(exist_ok=True)
(patch_dir / 'rexglue-vulkan-stencil-transfer.patch').write_text(''.join(difflib.unified_diff(
    before.splitlines(keepends=True), after.splitlines(keepends=True),
    fromfile='a/' + str(relative), tofile='b/' + str(relative))))
p.write_text(after)
print('Fixed stencil-only transfer shader generation:', p)

"""Temporary opt-in RenderDoc/X11 instance compatibility; no drawing changes."""
from pathlib import Path
import shutil
import sys

root = Path.home() / 'rexglue-sdk'
relative = Path('src/ui/vulkan/vulkan_instance.cpp')
p = root / relative
s = p.read_text()
anchor = '  std::vector<const char*> enabled_extensions;'
gate = '''  // Temporary capture-only gate: this RenderDoc binary has no Wayland WSI.
  // The normal instance and drawing behavior are unchanged without this env.
  if (std::getenv("TORCHLIGHT_CAPTURE_X11")) {
    requested_extensions.erase("VK_KHR_wayland_surface");
    requested_extensions.erase("VK_KHR_portability_enumeration");
  }

'''
if '--restore' in sys.argv:
    assert s.count(gate) == 1
    p.write_text(s.replace(gate, '', 1))
else:
    assert s.count(anchor) == 1 and 'TORCHLIGHT_CAPTURE_X11' not in s
    backup = Path('docs/bringup-artifacts/nvidia-minimap/sdk-originals') / relative
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(p, backup)
    p.write_text(s.replace(anchor, gate + anchor))

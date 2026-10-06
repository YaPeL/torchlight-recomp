"""Close the live window before ReXApp deletes it during guest-initiated exit."""
from pathlib import Path
import difflib
import shutil

sdk = Path.home() / 'rexglue-sdk'
source = sdk / 'src/ui/rex_app.cpp'
installed = sdk / 'out/install/linux-amd64/share/rexglue/rex_app.cpp'
before = source.read_text()
old = '''    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
  }
  window_.reset();
  runtime_.reset();'''
new = '''    window_->RemoveInputListener(this);
    window_->RemoveListener(this);
    // Guest-initiated exit may quit the UI loop without closing the window.
    // Notify the remaining listeners while their window is still alive so
    // input drivers detach before window destruction and runtime teardown.
    window_->RequestClose();
  }
  window_.reset();
  runtime_.reset();'''
assert before.count(old) == 1
assert installed.read_text() == before, 'Installed SDK source differs; review before staging'
after = before.replace(old, new)
backup = Path('docs/bringup-artifacts/shutdown')
backup.mkdir(parents=True, exist_ok=True)
assert not (backup / 'rex_app.original.cpp').exists()
shutil.copy2(source, backup / 'rex_app.original.cpp')
Path('patches/rexglue-shutdown-window-lifecycle.patch').write_text(''.join(difflib.unified_diff(
    before.splitlines(keepends=True), after.splitlines(keepends=True),
    fromfile='a/src/ui/rex_app.cpp', tofile='b/src/ui/rex_app.cpp')))
source.write_text(after)
installed.write_text(after)
print('Updated SDK source and installed consumer source:', source)

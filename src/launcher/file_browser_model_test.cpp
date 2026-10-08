// Tests for the launcher's fallback file browser, over a made-up tree: the rows of a folder (order,
// hidden names, the parent, "use this folder"), the selection, opening and choosing, the top, and
// folders that cannot be read.

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "launcher/file_browser_model.h"

namespace {

using namespace torchlight::launcher;
using Kind = FileBrowserModel::RowKind;
using Mode = FileBrowserModel::Mode;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// The tree, by generic path; "/locked" is there but cannot be read.
ListFolder FakeTree() {
  static const std::map<std::string, std::vector<FolderEntry>> tree = {
      {"/", {{"home", true, 0}, {"locked", true, 0}}},
      {"/home", {{"deck", true, 0}}},
      {"/home/deck",
       {{"zeta", true, 0},
        {"b.txt", false, 5},
        {".hidden", true, 0},
        {"Games", true, 0},
        {"A_package", false, 100},
        {"Alpha", true, 0}}},
      {"/home/deck/Games", {{"LIVEPKG", false, 1234}}},
      {"/home/deck/Alpha", {}},
      {"/home/deck/zeta", {}},
  };
  return [](const std::filesystem::path& folder, std::vector<FolderEntry>& entries,
            std::string& error) {
    const auto it = tree.find(folder.generic_string());
    if (it == tree.end()) {
      error = "Permission denied";
      return false;
    }
    entries = it->second;
    return true;
  };
}

std::string Names(const FileBrowserModel& b) {
  std::string out;
  for (const auto& row : b.rows()) {
    if (!out.empty()) out += ",";
    switch (row.kind) {
      case Kind::kParent: out += ".."; break;
      case Kind::kUseFolder: out += "[use]"; break;
      case Kind::kFolder: out += row.name + "/"; break;
      case Kind::kFile: out += row.name; break;
    }
  }
  return out;
}

void TestFileMode() {
  FileBrowserModel b(Mode::kFile, FakeTree(), "/home/deck/", {"/home"});
  Check(b.folder().generic_string() == "/home/deck", "starts where asked, without the slash");
  Check(Names(b) == "..,Alpha/,Games/,zeta/,A_package,b.txt",
        "parent, folders then files, by name ignoring case, no hidden ones");
  Check(b.rows()[4].size == 100, "file sizes");
  Check(b.selected() == 0, "the first row selected");
  b.Move(-1);
  Check(b.selected() == 0, "stops at the top");
  b.Move(100);
  Check(b.selected() == b.rows().size() - 1, "stops at the bottom");
  Check(!b.Activate(2) && b.folder().generic_string() == "/home/deck/Games", "a folder opens");
  Check(Names(b) == "..,LIVEPKG" && b.selected() == 0, "its rows, selection reset");
  Check(!b.ActivateSelected() && b.folder().generic_string() == "/home/deck", "the parent opens");
  const auto chosen = b.Activate(4);
  Check(chosen && chosen->generic_string() == "/home/deck/A_package", "a file is the choice");
  Check(!b.Activate(99), "a row out of range does nothing");
}

void TestTop() {
  FileBrowserModel b(Mode::kFile, FakeTree(), "/home", {});
  Check(b.Up() && b.folder().generic_string() == "/", "up to the top");
  Check(Names(b) == "home/,locked/", "no parent row at the top");
  Check(!b.Up(), "nothing above the top");
}

void TestFolderMode() {
  FileBrowserModel b(Mode::kFolder, FakeTree(), "/home/deck/Games", {});
  Check(Names(b) == "..,[use],LIVEPKG", "\"use this folder\", files shown");
  Check(!b.Activate(2), "a file is not a choice in folder mode");
  const auto chosen = b.Activate(1);
  Check(chosen && chosen->generic_string() == "/home/deck/Games", "this folder is the choice");
}

void TestUnreadable() {
  FileBrowserModel b(Mode::kFile, FakeTree(), "/locked", {"/locked", "/home"});
  Check(b.folder().generic_string() == "/home", "unreadable start: the first place that opens");
  Check(b.places().size() == 2, "places kept");
  b.GoTo(0);
  Check(b.folder().generic_string() == "/home", "an unreadable place: stays");
  Check(b.error().text == FileBrowserModel::kTextCannotOpen && b.error().values.size() == 2 &&
            b.error().values[1].second == "Permission denied",
        "and says why");
  b.GoTo(1);
  Check(b.error().empty(), "the error goes once a folder opens");
  b.Up();
  Check(!b.Activate(1) && b.folder().generic_string() == "/" && !b.error().empty(),
        "a subfolder that cannot be read: stays, with the error");
  FileBrowserModel none(Mode::kFile, FakeTree(), "/nowhere", {});
  Check(none.folder().empty() && none.rows().empty() && !none.error().empty(),
        "nothing opens: empty, with the error");
  Check(!none.Up() && !none.ActivateSelected(), "and nothing to do");
}

}  // namespace

int main() {
  TestFileMode();
  TestTop();
  TestFolderMode();
  TestUnreadable();
  if (failures) return 1;
  std::printf("file browser model test: ok\n");
  return 0;
}

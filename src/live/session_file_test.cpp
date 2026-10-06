// Session recording: frames read back as written, content written once per key and shared, the
// size cap leaving a well-formed file, and superseded content forgotten on both sides.

#include "live/session_file.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace torchlight;
using live::LiveFrame;
using live::SessionReader;
using live::SessionWriter;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

commands::ResourceId Id(uint32_t address) {
  commands::ResourceId id;
  id.guest_address = address;
  id.generation = 1;
  return id;
}

live::SnapshotStore::Content Content(commands::Hash hash, uint8_t fill, size_t size) {
  commands::Blob b;
  b.hash = hash;
  b.bytes.assign(size, fill);
  return std::make_shared<const commands::Blob>(std::move(b));
}

LiveFrame Frame(uint64_t swap) {
  LiveFrame f;
  f.swap = swap;
  f.commands.push_back({0, commands::Present{swap}});
  return f;
}

}  // namespace

int main() {
  std::string dir = (std::filesystem::temp_directory_path() / "session_file_test").string();
  std::filesystem::create_directories(dir);
  std::string path = dir + "/session.tlses";
  std::string error;

  // Three frames: A (buffer 1) and B (buffer 2); A again by key; A's next version and B destroyed.
  auto a = Content(0xA1, 1, 4096), b = Content(0xB1, 2, 4096), a2 = Content(0xA2, 3, 4096);
  LiveFrame f1 = Frame(10), f2 = Frame(11), f3 = Frame(12);
  f1.contents = {{Id(1), a}, {Id(2), b}};
  f1.textures.push_back({});
  f2.contents = {{Id(1), a}};
  f2.dropped_before = 3;
  f3.contents = {{Id(1), a2}};
  f3.destroyed = {Id(2)};
  {
    SessionWriter w;
    Check(w.Open(path, 0, error), "open for writing");
    Check(w.Append(f1) == SessionWriter::Result::kWritten, "frame 1 written");
    uint64_t after_first = w.bytes_written();
    Check(w.Append(f2) == SessionWriter::Result::kWritten, "frame 2 written");
    Check(w.bytes_written() - after_first < 1024, "content already written is not written again");
    Check(w.Append(f3) == SessionWriter::Result::kWritten, "frame 3 written");
    Check(w.frames() == 3, "three frames");
    w.Close();
  }
  {
    SessionReader r;
    Check(r.Open(path, error), "open for reading");
    LiveFrame g1, g2, g3, end;
    Check(r.Next(g1, error) && r.Next(g2, error) && r.Next(g3, error), "three frames read");
    Check(!r.Next(end, error) && error.empty(), "clean end");
    Check(g1.swap == 10 && g2.swap == 11 && g3.swap == 12, "swaps");
    Check(g1.textures.size() == 1 && g2.dropped_before == 3, "frame fields");
    Check(g1.contents.size() == 2 && g1.contents[0].content->bytes == a->bytes &&
              g1.contents[1].content->bytes == b->bytes,
          "contents with their bytes");
    Check(g2.contents.size() == 1 && g2.contents[0].content == g1.contents[0].content,
          "content written once is shared by the frames that hold it");
    Check(g3.contents[0].content->bytes == a2->bytes && g3.destroyed.size() == 1,
          "new version and destruction");
    Check(std::holds_alternative<commands::Present>(g3.commands[0].payload), "commands");
  }

  // Cap: room for the first frame only; the file ends after it.
  {
    SessionWriter w;
    Check(w.Open(path, 0, error), "open for writing (cap probe)");
    w.Append(f1);
    uint64_t one_frame = w.bytes_written();
    w.Close();
    Check(w.Open(path, one_frame, error), "open with a cap");
    Check(w.Append(f1) == SessionWriter::Result::kWritten, "first frame fits");
    Check(w.Append(f2) == SessionWriter::Result::kCapReached, "second frame reaches the cap");
    Check(!w.open(), "closed at the cap");
    SessionReader r;
    LiveFrame g, end;
    Check(r.Open(path, error) && r.Next(g, error), "capped file reads");
    Check(!r.Next(end, error) && error.empty(), "capped file ends cleanly after one frame");
  }

  // Not a recording.
  {
    std::ofstream(dir + "/bad.tlses") << "TLCAP   whatever";
    SessionReader r;
    Check(!r.Open(dir + "/bad.tlses", error), "rejects another format");
  }

  std::filesystem::remove_all(dir);
  if (failures) return EXIT_FAILURE;
  std::printf("session_file_test: ok\n");
  return EXIT_SUCCESS;
}

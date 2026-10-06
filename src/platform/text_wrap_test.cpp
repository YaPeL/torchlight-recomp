// Tests for the dialogs' line breaks (text_wrap.h): no line longer than the width, every word kept
// in order, existing line breaks kept, long words on their own line, UTF-8 counted by character.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/text_wrap.h"

namespace {

using torchlight::platform::WrapText;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

std::vector<std::string> Split(const std::string& text, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t end = text.find(separator, start);
    parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) return parts;
    start = end + 1;
  }
}

size_t Characters(const std::string& text) {
  size_t count = 0;
  for (unsigned char c : text) count += (c & 0xC0) != 0x80;
  return count;
}

// The words of a text, whatever separates them.
std::vector<std::string> Words(const std::string& text) {
  std::vector<std::string> words;
  for (const std::string& line : Split(text, '\n')) {
    for (const std::string& word : Split(line, ' ')) {
      if (!word.empty()) words.push_back(word);
    }
  }
  return words;
}

void CheckWrapped(const std::string& text, size_t width, const char* what) {
  const std::string wrapped = WrapText(text, width);
  Check(Words(wrapped) == Words(text), what);
  for (const std::string& line : Split(wrapped, '\n')) {
    Check(Characters(line) <= width || Split(line, ' ').size() == 1, what);
    Check(line.empty() || (line.front() != ' ' && line.back() != ' '), what);
  }
}

void TestParagraphs() {
  const std::string english =
      "Choose which achievements to earn. Xbox 360: the game's original 12 achievements. PC: the 66 "
      "achievements of the PC version; some cannot be earned yet. Each set keeps its own progress. "
      "You can change this later in the settings (restart required).";
  CheckWrapped(english, 70, "English paragraph");
  Check(Split(WrapText(english, 70), '\n').size() == 4, "English paragraph: four lines of 70");
  const std::string german =
      "Wähle, welche Erfolge du verdienen möchtest. Xbox 360: die 12 ursprünglichen Erfolge des "
      "Spiels. PC: die 66 Erfolge der PC-Version; einige lassen sich noch nicht verdienen.";
  CheckWrapped(german, 70, "German paragraph");
  // 20 characters with two of them 2-byte: fits in a line of 20, not by bytes.
  Check(WrapText("äöüäöüäöüä äöüäöüäöü", 20) == "äöüäöüäöüä äöüäöüäöü", "UTF-8 counted by character");
}

void TestShapes() {
  Check(WrapText("") == "", "empty");
  Check(WrapText("short line") == "short line", "a short line stays");
  Check(WrapText("one two three", 7) == "one two\nthree", "broken at the last space that fits");
  Check(WrapText("first\n\nsecond", 70) == "first\n\nsecond", "existing line breaks kept");
  Check(WrapText("a  b", 70) == "a b", "runs of spaces collapse");
  const std::string path = "C:\\Users\\WDAGUtilityAccount\\AppData\\Local\\TorchlightRecomp\\game";
  Check(WrapText("Installed to " + path + " now.", 20) == "Installed to\n" + path + "\nnow.",
        "a word longer than the width gets its own line");
  CheckWrapped("The game files go to " + path + ". Choose your package.", 30, "with a path");
}

}  // namespace

int main() {
  TestParagraphs();
  TestShapes();
  std::printf("text_wrap_test: ok\n");
  return 0;
}

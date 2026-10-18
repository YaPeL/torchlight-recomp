#include "backend/program_list.h"

#include <fstream>
#include <sstream>

namespace torchlight::backend {

namespace {

constexpr int kFormat = 1;

std::string Header(size_t key_size) {
  return "tl-programs " + std::to_string(kFormat) + " " + std::to_string(key_size);
}

std::string ToHex(const std::string& bytes) {
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out += kDigits[c >> 4];
    out += kDigits[c & 15];
  }
  return out;
}

int Digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

}  // namespace

std::vector<std::string> ReadProgramList(const std::filesystem::path& path, size_t key_size) {
  std::vector<std::string> keys;
  std::ifstream in(path);
  std::string line;
  if (!in || !std::getline(in, line) || line != Header(key_size)) return keys;
  while (std::getline(in, line)) {
    if (line.size() != key_size * 2) continue;
    std::string key(key_size, '\0');
    bool ok = true;
    for (size_t i = 0; i < key_size && ok; ++i) {
      const int high = Digit(line[2 * i]), low = Digit(line[2 * i + 1]);
      ok = high >= 0 && low >= 0;
      key[i] = char(high << 4 | low);
    }
    if (ok) keys.push_back(std::move(key));
  }
  return keys;
}

bool AppendProgram(const std::filesystem::path& path, const std::string& key, size_t key_size,
                   bool fresh) {
  if (key.size() != key_size) return false;
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream out(path, fresh ? std::ios::trunc : std::ios::app);
  if (!out) return false;
  if (fresh) out << Header(key_size) << '\n';
  out << ToHex(key) << '\n';
  return bool(out.flush());
}

}  // namespace torchlight::backend

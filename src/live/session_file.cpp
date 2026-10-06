#include "live/session_file.h"

#include <cstring>
#include <memory>
#include <vector>

#include <zlib.h>

#include "commands/serialize.h"

namespace torchlight::live {

namespace {

using commands::Hash;
using commands::SessionContent;
using commands::SessionFrame;

constexpr char kMagic[8] = {'T', 'L', 'S', 'E', 'S', 0, 0, 0};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxRecordBytes = 1u << 30;

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t GetU32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

}  // namespace

bool SessionWriter::Open(const std::string& path, uint64_t max_bytes, std::string& error) {
  file_.open(path, std::ios::binary | std::ios::trunc);
  if (!file_) {
    error = "cannot create " + path;
    return false;
  }
  std::vector<uint8_t> header(kMagic, kMagic + sizeof(kMagic));
  PutU32(header, kVersion);
  file_.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
  max_bytes_ = max_bytes;
  bytes_ = header.size();
  frames_ = 0;
  written_.clear();
  keys_ = ContentKeys();
  return bool(file_);
}

SessionWriter::Result SessionWriter::Append(const LiveFrame& frame) {
  if (!file_.is_open()) return Result::kError;
  SessionFrame s;
  s.swap = frame.swap;
  s.dropped_before = frame.dropped_before;
  s.commands = frame.commands;
  s.vertex_buffers = frame.vertex_buffers;
  s.index_buffers = frame.index_buffers;
  s.declarations = frame.declarations;
  s.textures = frame.textures;
  s.programs = frame.programs;
  s.program_sources = frame.program_sources;
  s.destroyed = frame.destroyed;
  std::vector<Hash> first_seen;
  for (const auto& c : frame.contents) {
    if (!c.content) continue;
    SessionContent sc;
    sc.id = c.id;
    if (written_.count(c.content->hash)) {
      sc.blob.hash = c.content->hash;
      sc.bytes_in_record = false;
    } else {
      sc.blob = *c.content;
      first_seen.push_back(c.content->hash);
    }
    s.contents.push_back(std::move(sc));
  }
  std::vector<uint8_t> raw = commands::SerializeSessionFrame(s);
  uLongf packed_size = compressBound(uLong(raw.size()));
  std::vector<uint8_t> record(8 + packed_size);
  if (raw.size() > kMaxRecordBytes ||
      compress2(record.data() + 8, &packed_size, raw.data(), uLong(raw.size()), 1) != Z_OK) {
    Close();
    return Result::kError;
  }
  record.resize(8 + packed_size);
  std::vector<uint8_t> sizes;
  PutU32(sizes, uint32_t(packed_size));
  PutU32(sizes, uint32_t(raw.size()));
  std::memcpy(record.data(), sizes.data(), 8);
  if (max_bytes_ && bytes_ + record.size() > max_bytes_) {
    Close();
    return Result::kCapReached;
  }
  file_.write(reinterpret_cast<const char*>(record.data()), std::streamsize(record.size()));
  if (!file_) {
    Close();
    return Result::kError;
  }
  for (Hash h : first_seen) written_.insert(h);
  // Same rule as the reader: superseded and destroyed content is forgotten.
  for (const auto& c : frame.contents)
    if (c.content)
      if (auto old = keys_.Set(c.id, c.content->hash)) written_.erase(*old);
  for (const auto& id : frame.destroyed)
    if (auto old = keys_.Erase(id)) written_.erase(*old);
  bytes_ += record.size();
  ++frames_;
  return Result::kWritten;
}

void SessionWriter::Close() {
  if (file_.is_open()) file_.close();
}

bool SessionReader::Open(const std::string& path, std::string& error) {
  file_.open(path, std::ios::binary);
  if (!file_) {
    error = "cannot open " + path;
    return false;
  }
  char header[12];
  if (!file_.read(header, sizeof(header)) || std::memcmp(header, kMagic, sizeof(kMagic)) != 0) {
    error = path + " is not a session recording";
    return false;
  }
  uint32_t version = GetU32(reinterpret_cast<const uint8_t*>(header + 8));
  if (version != kVersion) {
    error = "unsupported session recording version " + std::to_string(version);
    return false;
  }
  return true;
}

bool SessionReader::Next(LiveFrame& frame, std::string& error) {
  error.clear();
  uint8_t sizes[8];
  if (!file_.read(reinterpret_cast<char*>(sizes), sizeof(sizes))) return false;  // end
  uint32_t packed_size = GetU32(sizes), raw_size = GetU32(sizes + 4);
  if (packed_size > kMaxRecordBytes || raw_size > kMaxRecordBytes) {
    error = "malformed session record";
    return false;
  }
  std::vector<uint8_t> packed(packed_size), raw(raw_size);
  if (!file_.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed_size))) {
    error = "truncated session record";
    return false;
  }
  uLongf out_size = raw_size;
  if (uncompress(raw.data(), &out_size, packed.data(), packed_size) != Z_OK ||
      out_size != raw_size) {
    error = "corrupt session record";
    return false;
  }
  SessionFrame s;
  if (!commands::DeserializeSessionFrame(raw.data(), raw.size(), s, error)) return false;
  frame = LiveFrame();
  frame.swap = s.swap;
  frame.dropped_before = s.dropped_before;
  frame.commands = std::move(s.commands);
  frame.vertex_buffers = std::move(s.vertex_buffers);
  frame.index_buffers = std::move(s.index_buffers);
  frame.declarations = std::move(s.declarations);
  frame.textures = std::move(s.textures);
  frame.programs = std::move(s.programs);
  frame.program_sources = std::move(s.program_sources);
  frame.destroyed = std::move(s.destroyed);
  for (auto& c : s.contents) {
    SnapshotStore::Content content;
    if (c.bytes_in_record) {
      content = std::make_shared<const commands::Blob>(std::move(c.blob));
      contents_[content->hash] = content;
    } else {
      auto it = contents_.find(c.blob.hash);
      if (it == contents_.end()) {
        error = "session record refers to content never written";
        return false;
      }
      content = it->second;
    }
    frame.contents.push_back({c.id, content});
  }
  // Same rule as the writer, after the whole frame.
  for (const auto& c : frame.contents)
    if (auto old = keys_.Set(c.id, c.content->hash)) contents_.erase(*old);
  for (const auto& id : frame.destroyed)
    if (auto old = keys_.Erase(id)) contents_.erase(*old);
  return true;
}

}  // namespace torchlight::live

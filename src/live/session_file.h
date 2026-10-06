// Live session recording (--live_record): every frame the live mode's backend consumed, in order,
// so the state the backend accumulates over a session can be reproduced offline (replay
// --session).
//
// File: magic "TLSES\0\0\0", u32 version, then one record per frame: u32 compressed size, u32 raw
// size, zlib(commands::SerializeSessionFrame). Content is written once per key; later frames
// refer to it by key. A recording cut by the size cap or by the end of the session holds whole
// records only.

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "commands/types.h"
#include "live/content_keys.h"
#include "live/frame_queue.h"
#include "live/snapshot_store.h"

namespace torchlight::live {

class SessionWriter {
 public:
  enum class Result { kWritten, kCapReached, kError };

  // Creates the file; frames are recorded until `max_bytes` would be exceeded.
  bool Open(const std::string& path, uint64_t max_bytes, std::string& error);
  // Records the frame as the backend consumed it. kCapReached: the record would exceed the cap,
  // nothing is written and the file is closed. kError: write failure, the file is closed.
  Result Append(const LiveFrame& frame);
  void Close();

  bool open() const { return file_.is_open(); }
  uint64_t bytes_written() const { return bytes_; }
  uint64_t frames() const { return frames_; }

 private:
  std::ofstream file_;
  uint64_t max_bytes_ = 0, bytes_ = 0, frames_ = 0;
  std::unordered_set<commands::Hash> written_;
  ContentKeys keys_;
};

class SessionReader {
 public:
  bool Open(const std::string& path, std::string& error);
  // The next recorded frame (content shared with the frames that hold the same key). False at the
  // end of the recording (error empty) or on malformed input (error set).
  bool Next(LiveFrame& frame, std::string& error);

 private:
  std::ifstream file_;
  std::unordered_map<commands::Hash, SnapshotStore::Content> contents_;
  ContentKeys keys_;
};

}  // namespace torchlight::live

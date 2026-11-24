export module vf.Storage.RecordFile;

import std.compat;

// Field 1, length-delimited: the `records` field of every appended-record file.
constexpr char RECORDS_TAG = 0x0A;

inline bool readVarint(std::string_view& in, uint64_t& out) {
  out = 0;
  for (int shift = 0; shift < 64 && !in.empty(); shift += 7) {
    const uint8_t byte = in.front();
    in.remove_prefix(1);
    out |= uint64_t(byte & 0x7F) << shift;
    if (!(byte & 0x80)) return true;
  }
  return false;
}

export {

// The payload of the next entry of a file of appended single-record messages
// (`vf.journal.File`, `vf.journal.SampleFile`, `vf.stats.SessionLog`),
// advancing `in` past it. False at the end, which includes a truncated entry:
// a file still being written ends mid-record.
inline bool nextRecord(std::string_view& in, std::string_view& record) {
  std::string_view at = in;
  if (at.empty() || at.front() != RECORDS_TAG) return false;
  at.remove_prefix(1);
  uint64_t size = 0;
  if (!readVarint(at, size) || size > at.size()) return false;
  record = at.substr(0, size);
  in = at.substr(size);
  return true;
}

// The file from byte `offset` on; empty when it is missing.
inline std::string readFrom(const std::filesystem::path& path, uintmax_t offset) {
  std::ifstream in(path, std::ios::binary);
  in.seekg(offset);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // export

export module vf.Storage.RecordFile;

import std.compat;

// Field 1, length-delimited: the `records` field of every appended-record file.
constexpr char RECORDS_TAG = 0x0A;

export {

enum class RecordError { Incomplete, Malformed };

// A partial final record can still be in flight. Advance only over complete records.
inline std::expected<void, RecordError> nextRecord(std::string_view& in, std::string_view& record) {
  using enum RecordError;
  if (in.empty()) return std::unexpected(Incomplete);
  if (in.front() != RECORDS_TAG) return std::unexpected(Malformed);
  auto at = in.substr(1);
  uint64_t size = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (at.empty()) return std::unexpected(Incomplete);
    const uint8_t byte = at.front();
    at.remove_prefix(1);
    if (shift == 63 && byte > 1) return std::unexpected(Malformed);
    size |= uint64_t(byte & 0x7F) << shift;
    if (byte & 0x80) continue;
    if (size > at.size()) return std::unexpected(Incomplete);
    record = at.substr(0, size);
    in = at.substr(size);
    return {};
  }
  return std::unexpected(Malformed);
}

// The file from byte `offset` on; empty when it is missing.
inline std::string readFrom(const std::filesystem::path& path, uintmax_t offset) {
  std::ifstream in(path, std::ios::binary);
  in.seekg(offset);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

}  // export

export module vf.LuaExports.Msgpack;

import std.compat;

export namespace VF::LuaExports {

// One complete msgpack value. A distinct type so raw text cannot be returned
// across the FFI unpacked.
struct Packed {
  std::string bytes;
};

// Append-only msgpack encoder. Containers are count-prefixed: `map(n)` is
// followed by exactly n key/value pairs, `array(n)` by n values.
//
// nil is never written: vim.mpack decodes it to vim.NIL (truthy userdata), so
// an absent field is omitted instead.
class MsgpackWriter {
 public:
  MsgpackWriter& map(size_t count) { return container(count, 0x80, 0xde); }
  MsgpackWriter& array(size_t count) { return container(count, 0x90, 0xdc); }

  MsgpackWriter& str(std::string_view text) {
    const size_t n = text.size();
    if (n < 32) {
      put(0xa0 | n);
    } else if (n < 0x100) {
      put(0xd9);
      bigEndian(n, 1);
    } else if (n < 0x10000) {
      put(0xda);
      bigEndian(n, 2);
    } else {
      put(0xdb);
      bigEndian(n, 4);
    }
    out_.append(text);
    return *this;
  }

  MsgpackWriter& key(std::string_view name) { return str(name); }

  MsgpackWriter& integer(int64_t v) {
    if (v >= 0 && v < 0x80) {
      put(v);
    } else {
      put(0xd3);
      bigEndian(v, 8);
    }
    return *this;
  }

  MsgpackWriter& number(double v) {
    put(0xcb);
    bigEndian(std::bit_cast<uint64_t>(v), 8);
    return *this;
  }

  MsgpackWriter& boolean(bool v) {
    put(v ? 0xc3 : 0xc2);
    return *this;
  }

  template <typename Strings>
  MsgpackWriter& strings(const Strings& values) {
    array(std::size(values));
    for (std::string_view value : values) str(value);
    return *this;
  }

  MsgpackWriter& value(const Packed& packed) {
    out_ += packed.bytes;
    return *this;
  }

  Packed take() { return Packed{std::move(out_)}; }

 private:
  void put(uint64_t byte) { out_.push_back(static_cast<char>(byte)); }

  void bigEndian(uint64_t v, int width) {
    for (int shift = (width - 1) * 8; shift >= 0; shift -= 8) put(v >> shift);
  }

  // `wide` is the 16-bit-count tag; the 32-bit tag is always the next byte.
  MsgpackWriter& container(size_t count, uint64_t fix, uint64_t wide) {
    if (count < 16) {
      put(fix | count);
    } else if (count < 0x10000) {
      put(wide);
      bigEndian(count, 2);
    } else {
      put(wide + 1);
      bigEndian(count, 4);
    }
    return *this;
  }

  std::string out_;
};

inline Packed packString(std::string_view text) {
  return MsgpackWriter().str(text).take();
}

inline Packed packInteger(int64_t v) {
  return MsgpackWriter().integer(v).take();
}

template <typename Strings>
Packed packStrings(const Strings& values) {
  return MsgpackWriter().strings(values).take();
}

}  // namespace VF::LuaExports

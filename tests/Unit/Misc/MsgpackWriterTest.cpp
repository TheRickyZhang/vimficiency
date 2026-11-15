#include <gtest/gtest.h>
#include <msgpack.hpp>

import std.compat;
import vf.LuaExports.Msgpack;

using namespace std;
using VF::LuaExports::MsgpackWriter;
using VF::LuaExports::Packed;

namespace {

// Decodes with msgpack-c and requires the buffer to be exactly one value.
msgpack::object_handle unpackWhole(const Packed& packed) {
  size_t offset = 0;
  msgpack::object_handle handle =
      msgpack::unpack(packed.bytes.data(), packed.bytes.size(), offset);
  EXPECT_EQ(offset, packed.bytes.size());
  return handle;
}

string asString(const msgpack::object& object) {
  EXPECT_EQ(object.type, msgpack::type::STR);
  return string(object.via.str.ptr, object.via.str.size);
}

}  // namespace

TEST(MsgpackWriterTest, StringsRoundTripAcrossEveryLengthHeader) {
  for (size_t length : {0, 1, 31, 32, 255, 256, 65535, 65536}) {
    const string text(length, 'x');
    const auto handle = unpackWhole(MsgpackWriter().str(text).take());
    EXPECT_EQ(asString(handle.get()), text) << "length " << length;
  }
}

TEST(MsgpackWriterTest, StringsPreserveArbitraryBytes) {
  const string text("a\0b\xff\x1e\x1f\n\t \xe2\x86\x92", 13);
  const auto handle = unpackWhole(MsgpackWriter().str(text).take());
  EXPECT_EQ(asString(handle.get()), text);
}

TEST(MsgpackWriterTest, IntegersRoundTrip) {
  for (int64_t value : {int64_t{0}, int64_t{1}, int64_t{127}, int64_t{128},
                        int64_t{-1}, int64_t{-33}, int64_t{1} << 40,
                        numeric_limits<int64_t>::min(),
                        numeric_limits<int64_t>::max()}) {
    const auto handle = unpackWhole(MsgpackWriter().integer(value).take());
    EXPECT_EQ(handle.get().as<int64_t>(), value);
  }
}

TEST(MsgpackWriterTest, NumbersStayFloat64) {
  for (double value : {0.0, 1.5, -2.25, 3.0, 1e300, 0.1}) {
    const auto handle = unpackWhole(MsgpackWriter().number(value).take());
    ASSERT_EQ(handle.get().type, msgpack::type::FLOAT64) << value;
    EXPECT_EQ(handle.get().via.f64, value);
  }
}

TEST(MsgpackWriterTest, BooleansRoundTrip) {
  EXPECT_TRUE(unpackWhole(MsgpackWriter().boolean(true).take()).get().as<bool>());
  EXPECT_FALSE(unpackWhole(MsgpackWriter().boolean(false).take()).get().as<bool>());
}

TEST(MsgpackWriterTest, ArraysRoundTripAcrossEveryCountHeader) {
  for (size_t count : {0, 15, 16, 65535, 65536}) {
    MsgpackWriter w;
    w.array(count);
    for (size_t i = 0; i < count; ++i) w.integer(i);
    const auto handle = unpackWhole(w.take());
    const msgpack::object& object = handle.get();
    ASSERT_EQ(object.type, msgpack::type::ARRAY) << "count " << count;
    ASSERT_EQ(object.via.array.size, count);
    if (count > 0) {
      EXPECT_EQ(object.via.array.ptr[count - 1].as<size_t>(), count - 1);
    }
  }
}

TEST(MsgpackWriterTest, MapsRoundTripAcrossEveryCountHeader) {
  for (size_t count : {0, 15, 16, 65536}) {
    MsgpackWriter w;
    w.map(count);
    for (size_t i = 0; i < count; ++i) w.key(to_string(i)).integer(i);
    const auto handle = unpackWhole(w.take());
    const msgpack::object& object = handle.get();
    ASSERT_EQ(object.type, msgpack::type::MAP) << "count " << count;
    ASSERT_EQ(object.via.map.size, count);
    if (count > 0) {
      const msgpack::object_kv& last = object.via.map.ptr[count - 1];
      EXPECT_EQ(asString(last.key), to_string(count - 1));
      EXPECT_EQ(last.val.as<size_t>(), count - 1);
    }
  }
}

TEST(MsgpackWriterTest, NestedContainersAndSplicedValues) {
  const Packed inner = MsgpackWriter().strings(vector<string>{"a", "", "b c"}).take();
  MsgpackWriter w;
  w.map(2);
  w.key("rows").value(inner);
  w.key("pos").map(2).key("row").integer(3).key("col").integer(4);
  const auto handle = unpackWhole(w.take());

  const msgpack::object& object = handle.get();
  ASSERT_EQ(object.type, msgpack::type::MAP);
  ASSERT_EQ(object.via.map.size, 2u);

  const msgpack::object_kv& rows = object.via.map.ptr[0];
  EXPECT_EQ(asString(rows.key), "rows");
  ASSERT_EQ(rows.val.via.array.size, 3u);
  EXPECT_EQ(asString(rows.val.via.array.ptr[1]), "");
  EXPECT_EQ(asString(rows.val.via.array.ptr[2]), "b c");

  const msgpack::object_kv& pos = object.via.map.ptr[1];
  EXPECT_EQ(asString(pos.key), "pos");
  ASSERT_EQ(pos.val.via.map.size, 2u);
  EXPECT_EQ(pos.val.via.map.ptr[1].val.as<int>(), 4);
}

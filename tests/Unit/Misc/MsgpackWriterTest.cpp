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
  for (size_t length : {0, 31, 32, 255, 256, 65535, 65536}) {
    const string text(length, 'x');
    EXPECT_EQ(asString(unpackWhole(MsgpackWriter().str(text).take()).get()), text)
        << "length " << length;
  }
  const string bytes("a\0b\xff\x1e\n\xe2\x86\x92", 9);
  EXPECT_EQ(asString(unpackWhole(MsgpackWriter().str(bytes).take()).get()), bytes);
}

TEST(MsgpackWriterTest, ScalarsRoundTrip) {
  for (int64_t value : {int64_t{0}, int64_t{127}, int64_t{128}, int64_t{-1},
                        numeric_limits<int64_t>::min(),
                        numeric_limits<int64_t>::max()}) {
    EXPECT_EQ(unpackWhole(MsgpackWriter().integer(value).take()).get().as<int64_t>(), value);
  }
  // A whole-valued double must stay float64, not collapse to an integer.
  for (double value : {0.0, 3.0, -2.25, 0.1, 1e300}) {
    const auto handle = unpackWhole(MsgpackWriter().number(value).take());
    ASSERT_EQ(handle.get().type, msgpack::type::FLOAT64) << value;
    EXPECT_EQ(handle.get().via.f64, value);
  }
  EXPECT_TRUE(unpackWhole(MsgpackWriter().boolean(true).take()).get().as<bool>());
  EXPECT_FALSE(unpackWhole(MsgpackWriter().boolean(false).take()).get().as<bool>());
}

TEST(MsgpackWriterTest, ContainersRoundTripAcrossEveryCountHeader) {
  for (size_t count : {0, 15, 16, 65535, 65536}) {
    MsgpackWriter array;
    array.array(count);
    for (size_t i = 0; i < count; ++i) array.integer(i);
    const auto arrayHandle = unpackWhole(array.take());
    ASSERT_EQ(arrayHandle.get().type, msgpack::type::ARRAY) << "count " << count;
    ASSERT_EQ(arrayHandle.get().via.array.size, count);

    MsgpackWriter map;
    map.map(count);
    for (size_t i = 0; i < count; ++i) map.key(to_string(i)).integer(i);
    const auto mapHandle = unpackWhole(map.take());
    ASSERT_EQ(mapHandle.get().type, msgpack::type::MAP) << "count " << count;
    ASSERT_EQ(mapHandle.get().via.map.size, count);
    if (count > 0) {
      const msgpack::object_kv& last = mapHandle.get().via.map.ptr[count - 1];
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
  ASSERT_EQ(object.via.map.size, 2u);
  const msgpack::object_kv& rows = object.via.map.ptr[0];
  EXPECT_EQ(asString(rows.key), "rows");
  ASSERT_EQ(rows.val.via.array.size, 3u);
  EXPECT_EQ(asString(rows.val.via.array.ptr[2]), "b c");
  const msgpack::object_kv& pos = object.via.map.ptr[1];
  EXPECT_EQ(asString(pos.key), "pos");
  EXPECT_EQ(pos.val.via.map.ptr[1].val.as<int>(), 4);
}

module;
#include "LuaExports/Api.h"
#include "proto/common.pb.h"

module vf.LuaExports.Common;

import std.compat;

namespace VF::LuaExports {

VFByteSlice storeResult(std::string& storage, ProtoResult result) {
  vf::Envelope envelope;
  if (result) {
    envelope.set_value(std::move(result->bytes));
  } else {
    envelope.set_error(std::move(result.error().message));
  }
  storage = envelope.SerializeAsString();
  return byteSlice(storage);
}

VFByteSlice storePending(std::string& storage) {
  vf::Envelope envelope;
  envelope.set_pending(true);
  storage = envelope.SerializeAsString();
  return byteSlice(storage);
}

}  // namespace VF::LuaExports

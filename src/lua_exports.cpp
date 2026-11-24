module;
#include "AbiHash.h"
#include "BuildConfig.h"
#include "LuaExports/Api.h"
#include "proto/common.pb.h"
#include "proto/config.pb.h"

module vf.LuaExports.Common;

import std.compat;
import vf.Utils.Debug;
import vf.Versions;

namespace VF::LuaExports {

static const char* g_key_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroKey.inc"
#undef X
};

static const char* g_hand_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroHand.inc"
#undef X
};

static const char* g_finger_names[] = {
#define X(name, str) str,
#include "Keyboard/XMacroFinger.inc"
#undef X
};

namespace {

Proto textMessage(std::string text) {
  vf::Text msg;
  msg.set_text(std::move(text));
  return proto(msg);
}

}  // namespace

}  // namespace VF::LuaExports

using namespace VF::LuaExports;

extern "C" {

const char *vf_version_string() { return VIMF_VERSION; }

const char *vf_abi_hash() { return VF_ABI_HASH; }

// Defined in the generated descriptor.cpp (scripts/embed-binary.cmake).
extern const unsigned char VF_PROTO_DESCRIPTOR[];
extern const size_t VF_PROTO_DESCRIPTOR_SIZE;

VFByteSlice vf_proto_descriptor() {
  return VFByteSlice{
      .data = reinterpret_cast<const char*>(VF_PROTO_DESCRIPTOR),
      .len = VF_PROTO_DESCRIPTOR_SIZE,
  };
}

VFByteSlice vf_versions() {
  static std::string storage;
  return guarded(storage, [] {
    vf::Versions msg;
    msg.set_release(VIMF_VERSION);
    msg.set_journal_format(JOURNAL_FORMAT);
    msg.set_samples_format(SAMPLES_FORMAT);
    msg.set_rollup_format(ROLLUP_FORMAT);
    msg.set_session_log_format(SESSION_LOG_FORMAT);
    msg.set_scoring_version(SCORING_VERSION);
    msg.set_journal_schema(JOURNAL_SCHEMA);
    msg.set_samples_schema(SAMPLES_SCHEMA);
    msg.set_rollup_schema(ROLLUP_SCHEMA);
    msg.set_session_log_schema(SESSION_LOG_SCHEMA);
    msg.set_scoring_signature(SCORING_SIGNATURE);
    return proto(msg);
  });
}

VFByteSlice vf_enums() {
  static std::string storage;
  return guarded(storage, [] {
    vf::config::Enums msg;
    for (const char* name : g_key_names) msg.add_keys(name);
    for (const char* name : g_finger_names) msg.add_fingers(name);
    for (const char* name : g_hand_names) msg.add_hands(name);
    for (const char* name : countClassNames()) msg.add_count_classes(name);
    return proto(msg);
  });
}

VFByteSlice vf_apply_config(const char* request, size_t request_len) {
  static std::string storage;
  return guarded(storage, [&]() -> ProtoResult {
    const auto applied = applyConfig(std::string_view(request, request_len));
    if (!applied) return std::unexpected(applied.error());
    return proto(vf::Empty{});
  });
}

void vf_reset_config() {
  noUnwind([] { resetConfig(); });
}

VFByteSlice vf_get_warnings() {
  static std::string storage;
  return guarded(storage, [] { return textMessage(consume_warning_output()); });
}

VFByteSlice vf_debug_config() {
  static std::string storage;
  return guarded(storage, [] { return textMessage(describeConfig()); });
}

}

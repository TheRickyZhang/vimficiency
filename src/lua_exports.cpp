module;
#include "AbiHash.h"
#include "BuildConfig.h"
#include "LuaExports/Api.h"
#include "proto/common.pb.h"
#include "proto/config.pb.h"

module vf.LuaExports.Common;

import std.compat;
import vf.Keyboard.Config;
import vf.Optimizer.CountPenalty;
import vf.Optimizer.GlobalRuntimeOptions;
import vf.Utils.Debug;
import vf.VimCore.VimOptions;

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

static const char* g_count_class_names[] = {
#define X(name, str) str,
#include "Optimizer/XMacroCountClass.inc"
#undef X
};

Config g_config_internal = Config::uniform();

namespace {

// What Lua has configured so far. Each vf_apply_config merges into it, then
// `syncConfig` rebuilds the globals the optimizer reads.
struct ConfigState {
  int defaultKeyboard = UNIFORM;
  ScoreWeights weights;
  std::array<KeyInfo, KEY_COUNT> keys{};
  int shiftwidth = -1;
  bool useCountPenaltyOverrides = false;
  CountPenaltyOverrideTable countPenaltyOverrides{};
};

ConfigState g_config_state;

void syncConfig() {
  switch (g_config_state.defaultKeyboard) {
  case QWERTY:
    g_config_internal = Config::qwerty();
    break;
  case COLEMAK_DH:
    g_config_internal = Config::colemakDh();
    break;
  case UNIFORM:
    g_config_internal = Config::uniform();
    break;
  case NONE:
    break;
  }

  g_config_internal.weights = g_config_state.weights;

  for (size_t i = 0; i < KEY_COUNT; i++) {
    const KeyInfo& src = g_config_state.keys[i];
    if (src.hand != Hand::None) g_config_internal.keyInfo[i] = src;
  }

  // Reject 0 as well as the -1 "unset" sentinel: shiftwidth 0 means "follow
  // tabstop" in Neovim, which this layer can't model (no tabstop). Callers
  // resolve it to the effective value before this point; keep the default 8.
  if (g_config_state.shiftwidth >= 1) {
    VimOptions::shiftwidthRef() = g_config_state.shiftwidth;
  }

  auto& runtimeOptions = globalRuntimeOptions();
  runtimeOptions.useCountPenaltyOverrides = g_config_state.useCountPenaltyOverrides;
  runtimeOptions.countPenaltyOverrides = g_config_state.countPenaltyOverrides;
}

Expected<int> countClassIndex(const vf::config::CountPenaltyOverride& override) {
  if (override.has_class_name()) {
    const auto it = std::find(
        std::begin(g_count_class_names), std::end(g_count_class_names),
        std::string_view(override.class_name()));
    if (it == std::end(g_count_class_names)) {
      return unexpectedError("Unknown count penalty class: " + override.class_name());
    }
    return it - std::begin(g_count_class_names);
  }
  const int index = override.class_index();
  if (index < 0 || index >= static_cast<int>(CountClassCOUNT)) {
    return unexpectedError("Count penalty class out of range: " + std::to_string(index));
  }
  return index;
}

ProtoResult applyConfig(const vf::config::Config& msg) {
  ConfigState next = g_config_state;

  if (msg.has_default_keyboard()) next.defaultKeyboard = msg.default_keyboard();
  if (msg.has_shiftwidth()) next.shiftwidth = msg.shiftwidth();

  const vf::config::Weights& w = msg.weights();
  if (w.has_key_weight())         next.weights.key_weight = w.key_weight();
  if (w.has_same_finger_weight()) next.weights.same_finger_weight = w.same_finger_weight();
  if (w.has_same_key_weight())    next.weights.same_key_weight = w.same_key_weight();
  if (w.has_alt_hand_weight())    next.weights.alt_hand_weight = w.alt_hand_weight();
  if (w.has_good_roll_weight())   next.weights.good_roll_weight = w.good_roll_weight();
  if (w.has_bad_roll_weight())    next.weights.bad_roll_weight = w.bad_roll_weight();

  for (const vf::config::KeyInfo& key : msg.keys()) {
    if (key.key() < 0 || key.key() >= static_cast<int>(KEY_COUNT)) {
      return unexpectedError("Key index out of range: " + std::to_string(key.key()));
    }
    next.keys[key.key()] = KeyInfo{
        .hand = static_cast<Hand>(key.hand()),
        .finger = static_cast<Finger>(key.finger()),
        .base_cost = key.base_cost(),
    };
  }

  if (msg.count_penalty_overrides_size() > 0) {
    next.useCountPenaltyOverrides = true;
    next.countPenaltyOverrides = {};
    for (const vf::config::CountPenaltyOverride& override : msg.count_penalty_overrides()) {
      auto index = countClassIndex(override);
      if (!index) return std::unexpected(index.error());
      PartialCountPenaltyParams params;
      if (override.has_base()) params.base = override.base();
      if (override.has_count_slope()) params.countSlope = override.count_slope();
      if (override.has_span_slope()) params.spanSlope = override.span_slope();
      if (params.base || params.countSlope || params.spanSlope) {
        next.countPenaltyOverrides[*index] = params;
      }
    }
  }
  if (msg.has_use_count_penalty_overrides()) {
    next.useCountPenaltyOverrides = msg.use_count_penalty_overrides();
  }

  g_config_state = next;
  syncConfig();
  return proto(vf::Empty{});
}

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

VFByteSlice vf_enums() {
  static std::string storage;
  return guarded(storage, [] {
    vf::config::Enums msg;
    for (const char* name : g_key_names) msg.add_keys(name);
    for (const char* name : g_finger_names) msg.add_fingers(name);
    for (const char* name : g_hand_names) msg.add_hands(name);
    for (const char* name : g_count_class_names) msg.add_count_classes(name);
    return proto(msg);
  });
}

VFByteSlice vf_apply_config(const char* request, size_t request_len) {
  static std::string storage;
  return guardedRequest<vf::config::Config>(storage, request, request_len, applyConfig);
}

void vf_reset_config() {
  noUnwind([] {
    g_config_state = {};
    VimOptions::shiftwidthRef() = 8;
    syncConfig();
  });
}

VFByteSlice vf_get_warnings() {
  static std::string storage;
  return guarded(storage, [] { return textMessage(consume_warning_output()); });
}

VFByteSlice vf_debug_config() {
  static std::string storage;
  return guarded(storage, [] {
    std::ostringstream oss;

    oss << "=== Config Debug ===\n";
    oss << "default_keyboard: " << g_config_state.defaultKeyboard << "\n";
    oss << "\n--- Weights (configured) ---\n";
    oss << "key_weight: " << g_config_state.weights.key_weight << "\n";
    oss << "same_finger_weight: " << g_config_state.weights.same_finger_weight << "\n";
    oss << "same_key_weight: " << g_config_state.weights.same_key_weight << "\n";
    oss << "alt_hand_weight: " << g_config_state.weights.alt_hand_weight << "\n";

    oss << "\n--- Weights (Internal) ---\n";
    oss << "key_weight: " << g_config_internal.weights.key_weight << "\n";
    oss << "same_finger_weight: " << g_config_internal.weights.same_finger_weight << "\n";
    oss << "\n--- Count Penalty Overrides ---\n";
    oss << "use_count_penalty_overrides: " << g_config_state.useCountPenaltyOverrides << "\n";

    oss << "\n--- Sample Keys (Internal) ---\n";
    auto show_key = [&](Key k, const char *name) {
      auto &info = g_config_internal.keyInfo[static_cast<size_t>(k)];
      oss << name << ": hand=" << static_cast<int>(info.hand)
          << " finger=" << static_cast<int>(info.finger)
          << " cost=" << info.base_cost << "\n";
    };
    show_key(Key::Key_Space, "Space");
    show_key(Key::Key_J, "J");
    show_key(Key::Key_K, "K");

    return textMessage(oss.str());
  });
}

}

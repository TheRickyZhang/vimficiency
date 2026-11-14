module;
#include "Optimizer/OptimizerParamFields.h"

module vf.Optimizer.OptimizerParamOverrides;

import std.compat;
import vf.Utils.ParseNumber;

namespace {

enum class SetStatus { Ok, UnknownKey, BadValue };

template <class T> std::optional<T> parseAs(std::string_view s);
template <> std::optional<int> parseAs<int>(std::string_view s) { return parseInt(s); }
template <> std::optional<double> parseAs<double>(std::string_view s) { return parseDouble(s); }
template <> std::optional<bool> parseAs<bool>(std::string_view s) {
  if (s == "1" || s == "true") return true;
  if (s == "0" || s == "false") return false;
  return std::nullopt;
}

#define VF_PATCH_SET(type, name, withName, def)                        \
  if (key == #name) {                                                  \
    const auto v = parseAs<type>(value);                               \
    if (!v) return SetStatus::BadValue;                                \
    patch.values.name = *v;                                            \
    patch.set.set(size_t(Params::Field::name));                        \
    return SetStatus::Ok;                                              \
  }

#define VF_PATCH_APPLY(type, name, withName, def)                      \
  if (patch.set.test(size_t(Params::Field::name))) p.name = patch.values.name;

// One overload pair per params type over that type's full field list.
#define VF_PATCH_OPS(P, FIELDS)                                                   \
  SetStatus setField(ParamPatch<P>& patch, std::string_view key, std::string_view value) { \
    using Params = P;                                                             \
    FIELDS(VF_PATCH_SET)                                                          \
    return SetStatus::UnknownKey;                                                 \
  }                                                                               \
  void applyPatch(const ParamPatch<P>& patch, P& p) {                             \
    using Params = P;                                                             \
    FIELDS(VF_PATCH_APPLY)                                                        \
  }

VF_PATCH_OPS(OptimizerParamsBase, OPTIMIZER_BASE_FIELDS)
VF_PATCH_OPS(NavOptimizerParams, NAV_ALL_FIELDS)
VF_PATCH_OPS(TransformOptimizerParams, TRANSFORM_ALL_FIELDS)
VF_PATCH_OPS(CompositionOptimizerParams, COMPOSITION_ALL_FIELDS)

#undef VF_PATCH_OPS
#undef VF_PATCH_APPLY
#undef VF_PATCH_SET

// Distinguishes "no such key" from "key exists, wrong scope" in errors.
bool isKnownKey(std::string_view key) {
#define VF_KEY(type, name, withName, def) if (key == #name) return true;
  OPTIMIZER_BASE_FIELDS(VF_KEY)
  MOTION_CLASS_FIELDS(VF_KEY)
  NAV_OWN_FIELDS(VF_KEY)
  TRANSFORM_FIELDS(VF_KEY)
  COMPOSITION_OWN_FIELDS(VF_KEY)
#undef VF_KEY
  return false;
}

// Whitespace is NOT stripped — Lua emits clean "key=value".
std::optional<std::pair<std::string_view, std::string_view>>
splitOnce(std::string_view s, char c) {
  const auto pos = s.find(c);
  if (pos == std::string_view::npos) return std::nullopt;
  return std::make_pair(s.substr(0, pos), s.substr(pos + 1));
}

}  // namespace

std::expected<OptimizerParamOverrides, OptimizerParamOverrideErrors>
OptimizerParamOverrides::parse(std::string_view encoded) {
  OptimizerParamOverrides out;
  OptimizerParamOverrideErrors errors;
  int lineNumber = 0;
  auto fail = [&](std::string what) {
    errors.push_back("line " + std::to_string(lineNumber) + ": " + std::move(what));
  };

  while (!encoded.empty()) {
    const auto eolPos = encoded.find('\n');
    const auto line = encoded.substr(0, eolPos);
    encoded.remove_prefix(
        eolPos == std::string_view::npos ? encoded.size() : eolPos + 1);
    lineNumber++;
    if (line.empty()) continue;

    const auto scopeSplit = splitOnce(line, ':');
    if (!scopeSplit) {
      fail("expected '<scope>:<key>=<value>'");
      continue;
    }
    const auto& [scope, rest] = *scopeSplit;
    const auto kvSplit = splitOnce(rest, '=');
    if (!kvSplit) {
      fail("expected '<scope>:<key>=<value>'");
      continue;
    }
    const auto& [key, value] = *kvSplit;

    SetStatus status;
    if (scope == "shared") status = setField(out.shared_, key, value);
    else if (scope == "nav") status = setField(out.nav_, key, value);
    else if (scope == "transform") status = setField(out.transform_, key, value);
    else if (scope == "composition") status = setField(out.composition_, key, value);
    else {
      fail("unknown scope '" + std::string(scope) +
           "', expected shared/nav/transform/composition");
      continue;
    }

    const std::string scoped = std::string(scope) + ":" + std::string(key);
    switch (status) {
      case SetStatus::Ok:
        break;
      case SetStatus::BadValue:
        fail("invalid value for optimizer key '" + scoped + "': '" + std::string(value) + "'");
        break;
      case SetStatus::UnknownKey:
        fail(isKnownKey(key)
                 ? "optimizer key '" + scoped + "' is not valid in that scope"
                 : "unknown optimizer key '" + scoped + "'");
        break;
    }
  }
  if (!errors.empty()) return std::unexpected(std::move(errors));
  return out;
}

std::string formatOptimizerParamOverrideErrors(
    const OptimizerParamOverrideErrors& errors) {
  std::string out = "invalid optimizer overrides";
  for (const auto& error : errors) {
    out += "\n";
    out += error;
  }
  return out;
}

void OptimizerParamOverrides::applyTo(NavOptimizerParams& p) const {
  applyPatch(shared_, p);
  applyPatch(nav_, p);
  p.validate();
}

void OptimizerParamOverrides::applyTo(TransformOptimizerParams& p) const {
  applyPatch(shared_, p);
  applyPatch(transform_, p);
  p.validate();
}

void OptimizerParamOverrides::applyTo(CompositionOptimizerParams& p) const {
  applyPatch(shared_, p);
  applyPatch(composition_, p);
  p.validate();
}

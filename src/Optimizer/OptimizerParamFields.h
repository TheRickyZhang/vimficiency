#pragma once

// Textual on purpose: macros cannot be exported from a module.
//
// X-macro field lists are the single source of truth for the struct decls,
// `with*` setters, knownKeys(), and the override applier in
// OptimizerParamOverrides.cpp. Columns: (type, fieldName, withName, default,
// parser). `parser` is just a token here — only the override applier resolves
// it (parseInt/parseDouble/parseBool).

#define OPTIMIZER_BASE_FIELDS(F)                                                       \
    F(int,    maxResults,     withMaxResults,     20,    parseInt)                     \
    F(int,    maxNodesPopped, withMaxNodesPopped, 50000, parseInt)                     \
    F(double, exploreFactor,  withExploreFactor,  2.0,   parseDouble)                  \
    F(double, effortWeight,   withEffortWeight,   1.0,   parseDouble)                  \
    F(double, distanceWeight, withDistanceWeight, 1.0,   parseDouble)                  \
    /* minPrefixCount > maxPrefixCount disables count-prefixed exploration             \
       while leaving unprefixed exploration intact. */                                 \
    F(int,    minPrefixCount, withMinCountRepeat, 4,     parseInt)                     \
    F(int,    maxPrefixCount, withMaxCountRepeat, CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT, parseInt)

// Shared between Nav and Composition. Not lifted to base — Transform doesn't
// enumerate motions, so these would be dead there.
#define MOTION_CLASS_FIELDS(F)                                                          \
    F(int,  fMotionThreshold,      withFMotionThreshold,    3,    parseInt)             \
    F(bool, useDirectionalPruning, withDirectionalPruning,  true, parseBool)

#define NAV_OWN_FIELDS(F)                                                               \
    F(int, maxResultsPerEndPos, withMaxResultsPerEndPos, 1, parseInt)

#define NAV_FIELDS(F)        \
    MOTION_CLASS_FIELDS(F)   \
    NAV_OWN_FIELDS(F)

#define TRANSFORM_FIELDS(F)                                                             \
    F(int, maxResultsPerStartPos, withMaxResultsPerStartPos, 1, parseInt)               \
    F(bool, allowLinewisePureDeletion, withLinewisePureDeletion, true, parseBool)

#define COMPOSITION_OWN_FIELDS(F)                                                          \
    F(int,    navPaddingAbove, withNavLinePaddingAbove, 1, parseInt)                       \
    F(int,    navPaddingBelow, withNavLinePaddingBelow, 1, parseInt)                       \
    F(double, overshootPenalty, withOvershootPenalty, 3.0, parseDouble)                    \
    F(double, moveDeleteScale, withMoveDeleteScale, 1.0, parseDouble)                      \
    F(int,    transformMaxResultsPerStartPos, withTransformMaxResultsPerStartPos, 1, parseInt) \
    F(int,    diffAlgorithm, withDiffAlgorithm, 0, parseInt)  /* 0 = VimDiff default; 1 = MyersDiff fallback */

#define COMPOSITION_FIELDS(F)     \
    MOTION_CLASS_FIELDS(F)        \
    COMPOSITION_OWN_FIELDS(F)

#define VF_PARAMS_DECLARE_OWN_FIELD(type, name, withName, def, parser)  \
    type name = def;

// Derived structs invoke this after `#define VF_PARAMS_SELF <derived>` so the
// setter chains return the derived type.
#define VF_PARAMS_WITH_SETTER(type, name, withName, def, parser)        \
    VF_PARAMS_SELF& withName(type v) { name = v; return *this; }

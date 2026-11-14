#pragma once

// Textual on purpose: macros cannot be exported from a module.
//
// X-macro field lists are the single source of truth for the struct decls,
// `with*` setters, each struct's `Field` enum, the override patch ops in
// OptimizerParamOverrides.cpp, and vf_get_optimizer_defaults(). Columns:
// (type, fieldName, withName, default). `type` must be int, double, or bool.

#define OPTIMIZER_BASE_FIELDS(F)                                                       \
    F(int,    maxResults,     withMaxResults,     20)                                  \
    F(int,    maxNodesPopped, withMaxNodesPopped, 50000)                               \
    F(double, exploreFactor,  withExploreFactor,  2.0)                                 \
    F(double, effortWeight,   withEffortWeight,   1.0)                                 \
    F(double, distanceWeight, withDistanceWeight, 1.0)                                 \
    /* minPrefixCount > maxPrefixCount disables count-prefixed exploration             \
       while leaving unprefixed exploration intact. */                                 \
    F(int,    minPrefixCount, withMinCountRepeat, 4)                                   \
    F(int,    maxPrefixCount, withMaxCountRepeat, CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT)

// Shared between Nav and Composition. Not lifted to base — Transform doesn't
// enumerate motions, so these would be dead there.
#define MOTION_CLASS_FIELDS(F)                                                          \
    F(int,  fMotionThreshold,      withFMotionThreshold,    3)                          \
    F(bool, useDirectionalPruning, withDirectionalPruning,  true)

#define NAV_OWN_FIELDS(F)                                                               \
    F(int, maxResultsPerEndPos, withMaxResultsPerEndPos, 1)

#define TRANSFORM_FIELDS(F)                                                             \
    F(int, maxResultsPerStartPos, withMaxResultsPerStartPos, 1)                         \
    F(double, startWorkFactor, withStartWorkFactor, 8.0)                                \
    F(bool, allowLinewisePureDeletion, withLinewisePureDeletion, true)

#define COMPOSITION_OWN_FIELDS(F)                                                          \
    F(int,    navPaddingAbove, withNavLinePaddingAbove, 1)                                 \
    F(int,    navPaddingBelow, withNavLinePaddingBelow, 1)                                 \
    F(double, overshootPenalty, withOvershootPenalty, 3.0)                                 \
    F(double, moveDeleteScale, withMoveDeleteScale, 1.0)                                   \
    F(int,    transformMaxResultsPerStartPos, withTransformMaxResultsPerStartPos, 1)       \
    F(int,    diffAlgorithm, withDiffAlgorithm, 0)  /* 0 = VimDiff default; 1 = MyersDiff fallback */

// Fields each derived struct declares itself (excludes the base).
#define NAV_FIELDS(F)        \
    MOTION_CLASS_FIELDS(F)   \
    NAV_OWN_FIELDS(F)

#define COMPOSITION_FIELDS(F)     \
    MOTION_CLASS_FIELDS(F)        \
    COMPOSITION_OWN_FIELDS(F)

// Every field a struct exposes, base included.
#define NAV_ALL_FIELDS(F)         OPTIMIZER_BASE_FIELDS(F) NAV_FIELDS(F)
#define TRANSFORM_ALL_FIELDS(F)   OPTIMIZER_BASE_FIELDS(F) TRANSFORM_FIELDS(F)
#define COMPOSITION_ALL_FIELDS(F) OPTIMIZER_BASE_FIELDS(F) COMPOSITION_FIELDS(F)

#define VF_PARAMS_DECLARE_OWN_FIELD(type, name, withName, def)  \
    type name = def;

// Scoped, so the enumerators never collide with the data members of the
// same name. Indexes the presence bitset in OptimizerParamOverrides.
#define VF_PARAMS_FIELD_ENUM(type, name, withName, def) name,

// Derived structs invoke this after `#define VF_PARAMS_SELF <derived>` so the
// setter chains return the derived type.
#define VF_PARAMS_WITH_SETTER(type, name, withName, def)                \
    VF_PARAMS_SELF& withName(type v) { name = v; return *this; }

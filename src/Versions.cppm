export module vf.Versions;

import std.compat;

export {

// Bump when a journal record changes meaning, so readers stop at older files.
inline constexpr int JOURNAL_FORMAT = 3;
// Bump when what a `.samples` file holds changes, so older files are rescored.
inline constexpr int SAMPLES_FORMAT = 7;
// Bump when a rollup changes meaning; newer records are skipped.
inline constexpr int ROLLUP_FORMAT = 1;
// Bump when a session log record changes meaning; a newer log is an error.
inline constexpr int SESSION_LOG_FORMAT = 1;
// Bump when an optimizer or effort-model change moves costs.
inline constexpr int SCORING_VERSION = 8;

// Recorded signatures of what each version above guards.
inline constexpr std::string_view JOURNAL_SCHEMA = "5d90c7714d097453";
inline constexpr std::string_view SAMPLES_SCHEMA = "68cf6cc22e7a71e5";
inline constexpr std::string_view ROLLUP_SCHEMA = "16fea25bce12ad21";
inline constexpr std::string_view SESSION_LOG_SCHEMA = "4291f96a7f325b98";
inline constexpr std::string_view SCORING_SIGNATURE = "179843d124931949";

}  // export

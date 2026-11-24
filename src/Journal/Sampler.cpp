module;
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "proto/journal.pb.h"
#include "proto/summary.pb.h"

module vf.Journal.Sampler;

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.Storage.RecordFile;
import vf.Storage.Samples;
import vf.Optimizer.Analyze;
import vf.Stats.Rollup;
import vf.Stats.Run;
import vf.Types.Lines;
import vf.Utils.Debug;

using namespace std;
namespace fs = std::filesystem;

namespace Journal {

namespace {

class SamplingLock {
public:
  explicit SamplingLock(int fd) : fd_(fd) {}
  ~SamplingLock() { ::close(fd_); }

  SamplingLock(const SamplingLock&) = delete;
  SamplingLock& operator=(const SamplingLock&) = delete;

private:
  int fd_;
};

// The `.inprogress` file of the journal being sampled. Only assigned while
// `g_evaluating` is clear, so the handler never reads it half written.
string g_progress_path;
volatile std::sig_atomic_t g_evaluating = 0;

void stopQuietly(int) {
  if (g_evaluating) ::unlink(g_progress_path.c_str());
  std::_Exit(0);
}

void append(ofstream& out, const vf::journal::SampleRecord& record) {
  vf::journal::SampleFile file;
  *file.add_records() = record;
  out << file.SerializeAsString();
  out.flush();
}

// What a samples file already holds.
struct Existing {
  // Made with the requested params.
  bool current = false;
  // Samples per window size.
  map<int, int> done;
  int64_t journalSize = -1;
  // The file's length without a truncated last record.
  size_t wholeBytes = 0;
};

Existing readSamples(const fs::path& path, const string& paramsBytes) {
  const string bytes = readFrom(path, 0);
  string_view rest = bytes;
  string_view entry;
  Existing existing;
  vf::journal::SampleRecord record;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    switch (record.body_case()) {
      case vf::journal::SampleRecord::kParams:
        existing.current = record.params().SerializeAsString() == paramsBytes;
        break;
      case vf::journal::SampleRecord::kSample:
        existing.done[record.sample().window_size()]++;
        break;
      case vf::journal::SampleRecord::kJournalSize:
        existing.journalSize = record.journal_size();
        break;
      case vf::journal::SampleRecord::BODY_NOT_SET:
        break;
    }
    existing.wholeBytes = bytes.size() - rest.size();
  }
  return existing;
}

vf::journal::Sample::Outcome outcomeOf(Outcome outcome) {
  switch (outcome) {
    case Outcome::Scored:      return vf::journal::Sample::scored;
    case Outcome::TooLarge:    return vf::journal::Sample::too_large;
    case Outcome::NoResult:    return vf::journal::Sample::no_result;
    case Outcome::Unsupported: return vf::journal::Sample::unsupported;
    case Outcome::Unbeaten:    return vf::journal::Sample::unbeaten;
  }
  return vf::journal::Sample::scored;
}

constexpr string_view INVALID_WINDOW_SIZES = "window_sizes must be positive";

bool validWindowSizes(const vf::journal::SampleParams& params) {
  return !params.window_sizes().empty() &&
         ranges::none_of(params.window_sizes(), [](int size) { return size < 1; });
}

WindowCutter cutterOf(const vf::journal::SampleParams& params) {
  return WindowCutter(span<const int>(params.window_sizes().data(), params.window_sizes_size()),
                      params.idle_gap_ms());
}

// All but `overrides`, which takes parsing.
EvaluateParams evaluateParamsOf(const vf::journal::SampleParams& params) {
  const vf::stats::Stamp& stamp = params.stamp();
  return EvaluateParams{
      .slicePadding = stamp.slice_padding(),
      .maxSearchLines = stamp.max_search_lines(),
      .resultsCalculated = stamp.results_calculated(),
      .deadlineMs = params.deadline_ms(),
  };
}

expected<string, string> detailOf(const Window& window, const EvaluateParams& params) {
  const WindowSlice sliced = sliceWindow(window, params);
  if (sliced.outcome != Outcome::Scored) {
    return unexpected(format("a run that was {} has no slice",
                             vf::journal::Sample::Outcome_Name(outcomeOf(sliced.outcome))));
  }
  const SearchInputs& in = sliced.inputs;
  vf::stats::RunDetail detail;
  for (const Line& line : in.initialLines) detail.add_lines(line);
  for (const Line& line : in.goalLines) detail.add_goal_lines(line);
  detail.set_start_row(in.initialPos.line);
  detail.set_start_col(in.initialPos.col);
  detail.set_end_row(in.goalPos.line);
  detail.set_end_col(in.goalPos.col);
  detail.set_keys(in.userSeq);
  detail.set_typed(window.typed);
  detail.set_filetype(window.start.filetype);
  return detail.SerializeAsString();
}

int sampleJournal(const fs::path& journal, const vf::journal::SampleParams& params,
                  const EvaluateParams& evaluateParams) {
  fs::path samplesPath = journal;
  samplesPath.replace_extension(".samples");
  fs::path progressPath = journal;
  progressPath.replace_extension(".inprogress");
  g_progress_path = progressPath.string();

  Existing existing = readSamples(samplesPath, params.SerializeAsString());
  if (existing.current && existing.journalSize == int64_t(fs::file_size(journal))) return 0;
  if (!existing.current) {
    fs::remove(samplesPath);
    fs::remove(progressPath);
    existing = {};
  } else {
    fs::resize_file(samplesPath, existing.wholeBytes);
  }
  const string bytes = readFrom(journal, 0);

  ofstream out(samplesPath, ios::binary | ios::app);
  vf::journal::SampleRecord record;
  if (!existing.current) {
    *record.mutable_params() = params;
    append(out, record);
  }

  // Left behind by a run that died: the window it names comes right after the
  // samples of its size.
  if (fs::exists(progressPath)) {
    if (record.mutable_sample()->ParseFromString(readFrom(progressPath, 0))) {
      append(out, record);
      existing.done[record.sample().window_size()]++;
    }
    fs::remove(progressPath);
  }

  Replay replay(bytes);
  WindowCutter cutter = cutterOf(params);
  Event event;
  vector<Window> windows;
  int evaluated = 0;
  while (replay.next(event)) {
    windows.clear();
    cutter.push(event, windows);
    for (const Window& window : windows) {
      // The cut is deterministic, so the first windows are the ones on file.
      int& done = existing.done[window.size];
      if (done > 0) {
        done--;
        continue;
      }

      vf::journal::Sample& sample = *record.mutable_sample();
      sample.Clear();
      sample.set_window_size(window.size);
      sample.set_first_atom(window.firstAtom);
      sample.set_last_atom(window.lastAtom);
      sample.set_atom_count(window.atomCount);
      sample.set_wall_epoch(window.wallEpoch);
      sample.set_lossy(window.lossy);
      for (const Command& command : window.commands) {
        vf::stats::Command& out = *sample.add_commands();
        out.set_keys(command.keys);
        out.set_kind(command.kind);
        if (command.withinLine) sample.add_within_line(sample.commands_size() - 1);
      }
      if (window.typedInsertKnown) sample.set_typed_insert(window.typedInsert);
      sample.set_outcome(vf::journal::Sample::crashed);
      g_evaluating = 1;
      ofstream(progressPath, ios::binary | ios::trunc) << sample.SerializeAsString();

      const Sample scored =
          evaluate(window, evaluateParams, VF::LuaExports::g_config_internal);
      // Both streams grow for as long as nothing reads them.
      consume_debug_output();
      consume_warning_output();

      sample.set_outcome(outcomeOf(scored.outcome));
      sample.set_user_cost(scored.userCost);
      sample.set_best_cost(scored.bestCost);
      sample.set_best_seq(scored.bestSeq);
      sample.set_nav_only(scored.navOnly);
      sample.set_net_zero(scored.netZero);
      sample.set_deadline_hit(scored.deadlineHit);
      sample.set_analyze_ms(scored.analyzeMs);
      append(out, record);
      fs::remove(progressPath);
      g_evaluating = 0;
      evaluated++;
    }
  }

  record.set_journal_size(bytes.size());
  append(out, record);
  return evaluated;
}

// The runs of `samplesPath` when its last params are `params` and its samples
// cover `journalSize` bytes; false otherwise.
bool scoredRuns(const fs::path& samplesPath, string_view params, int64_t journalSize,
                Stats::RunTable& runs) {
  const string bytes = readFrom(samplesPath, 0);
  string_view rest = bytes;
  string_view entry;
  vf::journal::SampleRecord record;
  bool current = false;
  int64_t scored = -1;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    switch (record.body_case()) {
      case vf::journal::SampleRecord::kParams:
        current = record.params().SerializeAsString() == params;
        runs.clear();
        scored = -1;
        break;
      case vf::journal::SampleRecord::kSample:
        Stats::addRun(runs, Stats::journalRun(record.sample()));
        break;
      case vf::journal::SampleRecord::kJournalSize:
        scored = record.journal_size();
        break;
      case vf::journal::SampleRecord::BODY_NOT_SET:
        break;
    }
  }
  return current && scored == journalSize;
}

}  // namespace

expected<int, string> rollUpJournals(const fs::path& dir, string_view params, int keepDays) {
  const fs::path rollupPath = dir / Stats::ROLLUP_FILE;
  unordered_set<string> rolledUp;
  for (Stats::RolledJournal& rolled : Stats::readRollups(rollupPath)) {
    rolledUp.insert(std::move(rolled.journal));
  }

  const fs::file_time_type cutoff = fs::file_time_type::clock::now() - chrono::days(keepDays);
  vector<fs::directory_entry> old;
  for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".vfj" && entry.last_write_time() < cutoff) {
      old.push_back(entry);
    }
  }
  ranges::sort(old);

  int deleted = 0;
  for (const fs::directory_entry& entry : old) {
    fs::path samplesPath = entry.path();
    samplesPath.replace_extension(".samples");
    Stats::RolledJournal rolled{.journal = entry.path().filename().string(),
                                .params = string(params)};
    if (!rolledUp.contains(rolled.journal)) {
      if (!scoredRuns(samplesPath, params, entry.file_size(), rolled.runs)) continue;
      const auto appended = Stats::appendRollup(rollupPath, rolled);
      if (!appended) return unexpected(appended.error());
    }
    fs::path progressPath = entry.path();
    progressPath.replace_extension(".inprogress");
    // The journal last: while it is there, the next run finishes this one.
    fs::remove(samplesPath);
    fs::remove(progressPath);
    fs::remove(entry.path());
    deleted++;
  }
  return deleted;
}

void stopQuietlyWhenTerminated() {
  std::signal(SIGTERM, stopQuietly);
  std::signal(SIGINT, stopQuietly);
}

SampleRun sampleJournals(string_view requestBytes) {
  using VF::LuaExports::applyConfig;
  using VF::LuaExports::logic::parseOverrides;

  vf::journal::SampleRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return {.error = "malformed vf.journal.SampleRequest"};
  }
  vf::journal::SampleParams params = request.params();
  stampSampleParams(params);
  const vf::stats::Stamp& stamp = params.stamp();

  if (!validWindowSizes(params)) return {.error = string(INVALID_WINDOW_SIZES)};
  if (request.keep_days() < 1) return {.error = "keep_days must be positive"};
  const auto applied = applyConfig(stamp.config().SerializeAsString());
  if (!applied) return {.error = applied.error().message};
  auto overrides = parseOverrides(stamp.optimizer_overrides());
  if (!overrides) return {.error = overrides.error().message};

  EvaluateParams evaluateParams = evaluateParamsOf(params);
  evaluateParams.overrides = std::move(*overrides);

  const fs::path dir = request.journal_dir();
  const fs::path lockPath = dir / "sampler.lock";
  const int fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
  if (fd == -1) {
    const error_code error(errno, generic_category());
    return {.error = format("could not open {}: {}", lockPath.string(), error.message())};
  }
  const SamplingLock lock(fd);
  if (::flock(fd, LOCK_EX | LOCK_NB) == -1) {
    const error_code error(errno, generic_category());
    if (error == errc::operation_would_block) return {.busy = true};
    return {.error = format("could not lock {}: {}", lockPath.string(), error.message())};
  }

  vector<fs::path> journals;
  for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".vfj") journals.push_back(entry.path());
  }
  ranges::sort(journals);

  SampleRun run;
  for (const fs::path& journal : journals) {
    run.evaluated += sampleJournal(journal, params, evaluateParams);
    run.journals++;
  }
  const auto rolledUp =
      rollUpJournals(dir, params.SerializeAsString(), request.keep_days());
  if (!rolledUp) return {.error = rolledUp.error()};
  run.rolledUp = *rolledUp;
  return run;
}

expected<string, string> runDetail(string_view requestBytes) {
  vf::stats::RunRequest request;
  if (!request.ParseFromArray(requestBytes.data(), requestBytes.size())) {
    return unexpected("malformed vf.stats.RunRequest");
  }
  if (!request.id().has_journal()) return unexpected("a session run has no journal window");
  const vf::stats::RunId::Journal& id = request.id().journal();
  const vf::journal::SampleParams& params = request.sample_params();
  if (!validWindowSizes(params)) return unexpected(string(INVALID_WINDOW_SIZES));
  if (id.file().contains('/')) {
    return unexpected(format("journal file name '{}' holds a path separator", id.file()));
  }
  const fs::path path = fs::path(request.journal_dir()) / id.file();
  if (!fs::is_regular_file(path)) return unexpected(format("journal {} is gone", path.string()));

  const string bytes = readFrom(path, 0);
  Replay replay(bytes);
  WindowCutter cutter = cutterOf(params);
  Event event;
  vector<Window> windows;
  bool past = false;
  while (!past && replay.next(event)) {
    windows.clear();
    cutter.push(event, windows);
    for (const Window& window : windows) {
      if (window.size != id.window_size() || window.lastAtom < id.last_atom()) continue;
      if (window.lastAtom > id.last_atom()) {
        past = true;
        break;
      }
      return detailOf(window, evaluateParamsOf(params));
    }
  }
  return unexpected(format("no window of {} atoms ends at atom {} of {}", id.window_size(),
                           id.last_atom(), path.string()));
}

}  // namespace Journal

module;
#include <csignal>

#include "proto/journal.pb.h"

module vf.Journal.Sampler;

import std.compat;
import vf.Journal.Evaluate;
import vf.Journal.Replay;
import vf.Journal.Windows;
import vf.LuaExports.Common;
import vf.LuaExports.Logic;
import vf.LuaExports.RecordFile;
import vf.LuaExports.Samples;
import vf.Utils.Debug;

using namespace std;
namespace fs = std::filesystem;

namespace Journal {

namespace {

// The `.inprogress` file of the journal being sampled. Only assigned while
// `g_evaluating` is clear, so the handler never reads it half written.
string g_progress_path;
volatile std::sig_atomic_t g_evaluating = 0;

void stopQuietly(int) {
  if (g_evaluating) std::remove(g_progress_path.c_str());
  std::_Exit(0);
}

string readFile(const fs::path& path) {
  ifstream in(path, ios::binary);
  return string(istreambuf_iterator<char>(in), {});
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
  const string bytes = readFile(path);
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
  const string bytes = readFile(journal);

  ofstream out(samplesPath, ios::binary | ios::app);
  vf::journal::SampleRecord record;
  if (!existing.current) {
    *record.mutable_params() = params;
    append(out, record);
  }

  // Left behind by a run that died: the window it names comes right after the
  // samples of its size.
  if (fs::exists(progressPath)) {
    if (record.mutable_sample()->ParseFromString(readFile(progressPath))) {
      append(out, record);
      existing.done[record.sample().window_size()]++;
    }
    fs::remove(progressPath);
  }

  Replay replay(bytes);
  WindowCutter cutter(span<const int>(params.window_sizes().data(), params.window_sizes_size()),
                      params.idle_gap_ms());
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
      }
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

}  // namespace

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

  if (params.window_sizes().empty() ||
      ranges::any_of(params.window_sizes(), [](int size) { return size < 1; })) {
    return {.error = "window_sizes must be positive"};
  }
  const auto applied = applyConfig(stamp.config().SerializeAsString());
  if (!applied) return {.error = applied.error().message};
  auto overrides = parseOverrides(stamp.optimizer_overrides());
  if (!overrides) return {.error = overrides.error().message};

  const EvaluateParams evaluateParams{
      .slicePadding = stamp.slice_padding(),
      .maxSearchLines = stamp.max_search_lines(),
      .resultsCalculated = stamp.results_calculated(),
      .deadlineMs = params.deadline_ms(),
      .overrides = std::move(*overrides),
  };

  vector<fs::path> journals;
  for (const fs::directory_entry& entry : fs::directory_iterator(request.journal_dir())) {
    if (entry.path().extension() == ".vfj") journals.push_back(entry.path());
  }
  ranges::sort(journals);

  SampleRun run;
  for (const fs::path& journal : journals) {
    run.evaluated += sampleJournal(journal, params, evaluateParams);
    run.journals++;
  }
  return run;
}

}  // namespace Journal

module;
#include "proto/journal.pb.h"

module vf.Stats.Rollup;

import std.compat;
import vf.Storage.RecordFile;
import vf.Stats.Commands;
import vf.Stats.Run;
import vf.Stats.Score;
import vf.Versions;

using namespace std;
namespace fs = std::filesystem;

namespace Stats {

namespace {

void toTally(vf::journal::CommandTally& out, const CommandCounts& counts) {
  for (const auto& [command, n] : counts.commands) {
    vf::journal::CommandCount& entry = *out.add_commands();
    entry.set_command(command);
    entry.set_count(n);
  }
  out.set_typed_chars(counts.typedChars);
}

CommandCounts fromTally(const vf::journal::CommandTally& tally) {
  CommandCounts counts;
  for (const vf::journal::CommandCount& entry : tally.commands()) {
    counts.commands[entry.command()] += entry.count();
  }
  counts.typedChars = tally.typed_chars();
  return counts;
}

}  // namespace

bool isScoring(int outcome) {
  return outcome == vf::journal::Sample::scored || outcome == vf::journal::Sample::unbeaten;
}

RunKey keyOf(const Run& run) {
  return {run.day, run.windowSize, run.navOnly, run.outcome};
}

bool isCounted(const Run& run) {
  return !isScoring(run.outcome) || run.userCost > 0;
}

void addRun(RunTable& runs, const Run& run) {
  if (!isCounted(run)) return;
  RunTotals& totals = runs[keyOf(run)];
  totals.runs++;
  if (!isScoring(run.outcome)) return;

  totals.effort.add(run.userCost, run.bestCost, Weight::Effort);
  totals.uniform.add(run.userCost, run.bestCost, Weight::Uniform);
  if (run.deadlineHit) totals.deadlineHit++;
  if (run.outcome == vf::journal::Sample::unbeaten) return;
  totals.user.merge(run.user);
  totals.optimizer.merge(run.optimizer);
}

vector<RolledJournal> readRollups(const fs::path& path) {
  const string bytes = readFrom(path, 0);
  string_view rest = bytes;
  string_view entry;
  vf::journal::Rollup record;
  vector<RolledJournal> out;
  while (nextRecord(rest, entry) && record.ParseFromArray(entry.data(), entry.size())) {
    if (record.format() > ROLLUP_FORMAT) continue;
    RolledJournal& rolled = out.emplace_back();
    rolled.journal = record.journal();
    rolled.params = record.params().SerializeAsString();
    for (const vf::journal::RollupEntry& e : record.entries()) {
      RunTotals& totals = rolled.runs[{e.day(), e.window_size(), e.nav_only(), e.outcome()}];
      totals.runs = e.runs();
      const int spans = isScoring(e.outcome()) ? e.runs() : 0;
      totals.effort = {spans, e.effort_weighted(), e.effort_weights()};
      totals.uniform = {spans, e.uniform_weighted(), e.uniform_weights()};
      totals.deadlineHit = e.deadline_hit();
      totals.user = fromTally(e.user());
      totals.optimizer = fromTally(e.optimizer());
    }
  }
  return out;
}

expected<void, string> appendRollup(const fs::path& path, const RolledJournal& rolled) {
  vf::journal::RollupFile file;
  vf::journal::Rollup& record = *file.add_records();
  record.set_journal(rolled.journal);
  record.set_format(ROLLUP_FORMAT);
  if (!record.mutable_params()->ParseFromString(rolled.params)) {
    return unexpected("malformed vf.journal.SampleParams");
  }
  for (const auto& [key, totals] : rolled.runs) {
    vf::journal::RollupEntry& e = *record.add_entries();
    e.set_day(key.day);
    e.set_window_size(key.windowSize);
    e.set_nav_only(key.navOnly);
    e.set_outcome(vf::journal::Sample::Outcome(key.outcome));
    e.set_runs(totals.runs);
    e.set_effort_weighted(totals.effort.weighted);
    e.set_effort_weights(totals.effort.weights);
    e.set_uniform_weighted(totals.uniform.weighted);
    e.set_uniform_weights(totals.uniform.weights);
    e.set_deadline_hit(totals.deadlineHit);
    toTally(*e.mutable_user(), totals.user);
    toTally(*e.mutable_optimizer(), totals.optimizer);
  }

  ofstream out(path, ios::binary | ios::app);
  out << file.SerializeAsString();
  out.flush();
  if (!out) return unexpected("could not append to " + path.string());
  return {};
}

}  // namespace Stats

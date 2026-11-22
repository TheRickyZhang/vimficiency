export module vf.Journal.Sampler;

import std.compat;

export namespace Journal {

struct SampleRun {
  int journals = 0;
  int evaluated = 0;
  int rolledUp = 0;
  // Empty unless the request could not be served.
  std::string error;
};

// Brings `<name>.samples` up to date beside every `<name>.vfj` in the request's
// directory, then rolls up the journals older than `keep_days`
// (`rollUpJournals`). `request` is a serialized `vf.journal.SampleRequest`
// (proto/journal.proto).
//
// A samples file is only ever appended to, so a run resumes where the last
// one stopped, and a journal that has grown is sampled from its new part on.
// The window being evaluated is named in `<name>.inprogress`: if the process
// dies on it, the next run records it as crashed and moves past it.
SampleRun sampleJournals(std::string_view request);

// Appends to `rollup.vfr` the runs of every journal in `dir` unmodified for
// `keepDays` whose samples cover all of it under `params` (a serialized,
// stamped `vf.journal.SampleParams`), then deletes the journal and its
// samples. Returns how many journals it deleted.
//
// A journal already in `rollup.vfr` is only deleted, so a run that died
// between the append and the deletion is finished, not counted twice.
std::expected<int, std::string> rollUpJournals(const std::filesystem::path& dir,
                                               std::string_view params, int keepDays);

// A journal run's window as its sample sliced it: `request` is a serialized
// `vf.stats.RunRequest`, the result a serialized `vf.stats.RunDetail`
// (proto/summary.proto). The window is cut again from the journal under the
// request's params, so it is found only while those match the sample's.
std::expected<std::string, std::string> runDetail(std::string_view request);

// Makes SIGTERM and SIGINT end the process without leaving `.inprogress`
// behind: being told to stop is not dying on a window. Neovim sends SIGTERM to
// a running sampler when it exits.
void stopQuietlyWhenTerminated();

}  // namespace Journal

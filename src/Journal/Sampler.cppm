export module vf.Journal.Sampler;

import std.compat;

export namespace Journal {

struct SampleRun {
  int journals = 0;
  int evaluated = 0;
  // Empty unless the request could not be served.
  std::string error;
};

// Brings `<name>.samples` up to date beside every `<name>.vfj` in the request's
// directory. `request` is a serialized `vf.journal.SampleRequest`
// (proto/journal.proto).
//
// A samples file is only ever appended to, so a run resumes where the last
// one stopped, and a journal that has grown is sampled from its new part on.
// The window being evaluated is named in `<name>.inprogress`: if the process
// dies on it, the next run records it as crashed and moves past it.
SampleRun sampleJournals(std::string_view request);

// Makes SIGTERM and SIGINT end the process without leaving `.inprogress`
// behind: being told to stop is not dying on a window. Neovim sends SIGTERM to
// a running sampler when it exits.
void stopQuietlyWhenTerminated();

}  // namespace Journal

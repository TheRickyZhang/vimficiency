export module vf.Stats.Summary;

import std.compat;

export namespace Stats {

// Everything `:Vimfy stats` shows. `request` is a serialized
// `vf.stats.SummaryRequest` and the result a serialized `vf.stats.Summary`
// (proto/summary.proto); `now` places the 30-day charts.
std::expected<std::string, std::string> summarize(std::string_view request, int64_t now);

// One page of the runs a summary reduces. `request` is a serialized
// `vf.stats.RunsRequest` and the result a serialized `vf.stats.RunList`.
std::expected<std::string, std::string> listRuns(std::string_view request);

}  // namespace Stats

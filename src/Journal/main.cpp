// vimfy_journal: reads a serialized vf.journal.SampleRequest on stdin and
// samples every journal it names (Sampler.cppm). Run by `:Vimfy stats`.
//
// A separate process because a failed CHECK inside a search aborts: that would
// take Neovim down with it, and here it costs one window.

import std.compat;
import vf.Journal.Sampler;

int main() {
  Journal::stopQuietlyWhenTerminated();
  try {
    const std::string request(std::istreambuf_iterator<char>(std::cin), {});
    const Journal::SampleRun run = Journal::sampleJournals(request);
    if (!run.error.empty()) {
      std::cerr << "vimfy_journal: " << run.error << "\n";
      return 1;
    }
    std::cout << run.journals << " journals, " << run.evaluated << " windows evaluated, "
              << run.rolledUp << " rolled up\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "vimfy_journal: " << e.what() << "\n";
    return 1;
  }
}

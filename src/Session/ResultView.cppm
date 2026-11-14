export module vf.Session.ResultView;

import std.compat;
export import vf.Interpreter.SequenceDisplay;

export {

namespace VF::SessionView {

struct Recommendation {
  std::string seq;
  double cost = 0.0;
};

struct Result {
  std::vector<std::string> lines;
  int startRow = 0;
  int startCol = 0;
  int endRow = 0;
  int endCol = 0;
  std::string userSeq;
  bool hasUserCost = false;
  double userCost = 0.0;
  std::string kind;
  std::vector<Recommendation> optimalResults;
};

std::string formatPosition(const Result& result);
std::string formatKindSuffix(const Result& result);
std::vector<std::string> formatBody(
    const Result& result,
    SequenceDisplay::Options displayOptions = {});
std::string formatMessage(
    std::string_view title,
    const Result& result,
    SequenceDisplay::Options displayOptions = {});
std::vector<std::string> formatSavedLines(
    std::string_view name,
    const Result& result,
    SequenceDisplay::Options displayOptions = {});

}  // namespace VF::SessionView

}  // export

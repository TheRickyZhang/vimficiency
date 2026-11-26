#include "LuaExports/Api.h"
#include "proto/common.pb.h"
#include "proto/display.pb.h"

import std.compat;
import vf.LuaExports.Common;
import vf.Interpreter.SequenceDisplay;
import vf.Session.ResultView;

using namespace std;
using namespace VF::LuaExports;

namespace {

Proto textMessage(string text) {
  vf::Text msg;
  msg.set_text(std::move(text));
  return proto(msg);
}

VF::SessionView::Result sessionResult(const vf::display::SessionResult& r) {
  VF::SessionView::Result result;
  for (const auto& line : r.lines()) result.lines.emplace_back(line);
  result.startRow = r.start_row();
  result.startCol = r.start_col();
  result.endRow = r.end_row();
  result.endCol = r.end_col();
  result.userSeq = r.user_seq();
  result.hasUserCost = true;
  result.userCost = r.user_cost();
  result.kind = r.kind();
  for (const auto& optimal : r.optimal_results()) {
    result.optimalResults.push_back({.seq = optimal.seq(), .cost = optimal.cost()});
  }
  return result;
}

}  // namespace

extern "C" {

VFByteSlice vf_sequence_display_lines(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::display::SequenceRequest>(
      storage, request, request_len, [](const vf::display::SequenceRequest& req) {
        vf::Lines msg;
        for (auto& line : VF::SequenceDisplay::lines(
                 req.seq(), {.tokenize = req.tokenize(), .sectionize = req.sectionize()})) {
          msg.add_lines(std::move(line));
        }
        return proto(msg);
      });
}

VFByteSlice vf_sequence_display_inline(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::display::SequenceRequest>(
      storage, request, request_len, [](const vf::display::SequenceRequest& req) {
        return textMessage(VF::SequenceDisplay::inlineText(
            req.seq(), {.tokenize = req.tokenize(), .sectionize = false}));
      });
}

VFByteSlice vf_sequence_display_key_notation(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::Text>(storage, request, request_len, [](const vf::Text& req) {
    return textMessage(VF::SequenceDisplay::displayKeyNotation(req.text()));
  });
}

VFByteSlice vf_sequence_display_typed_text(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::display::TypedTextRequest>(
      storage, request, request_len, [](const vf::display::TypedTextRequest& req) {
        return textMessage(req.key_notation()
            ? VF::SequenceDisplay::displayTypedText(req.text())
            : VF::SequenceDisplay::displayLiteralTypedText(req.text()));
      });
}

VFByteSlice vf_sequence_display_typed_chunks(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::display::TypedChunksRequest>(
      storage, request, request_len, [](const vf::display::TypedChunksRequest& req) {
        vector<VF::SequenceDisplay::TypedChunk> typed;
        typed.reserve(req.chunks_size());
        for (const auto& chunk : req.chunks()) {
          typed.push_back({.kind = chunk.kind(), .text = chunk.text()});
        }
        return textMessage(VF::SequenceDisplay::typedChunksInline(typed, req.tokenize()));
      });
}

VFByteSlice vf_format_result_message(const char* request, size_t request_len) {
  static string storage;
  return guardedRequest<vf::display::ResultMessageRequest>(
      storage, request, request_len, [](const vf::display::ResultMessageRequest& req) {
        return textMessage(VF::SessionView::formatMessage(
            req.title(),
            sessionResult(req.result()),
            {.tokenize = req.tokenize(), .sectionize = req.sectionize()}));
      });
}

}

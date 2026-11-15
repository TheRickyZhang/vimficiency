#include "LuaExports/Api.h"

import std.compat;
import vf.LuaExports.Common;
import vf.Interpreter.SequenceDisplay;
import vf.Session.ResultView;

using namespace std;
using namespace VF::LuaExports;

namespace {

VF::SessionView::Result sessionResult(const VFSessionResult& r) {
  VF::SessionView::Result result;
  result.lines.reserve(r.lines_count);
  for (size_t i = 0; i < r.lines_count; ++i) {
    result.lines.emplace_back(sliceView(r.lines[i]));
  }
  result.startRow = r.start_row;
  result.startCol = r.start_col;
  result.endRow = r.end_row;
  result.endCol = r.end_col;
  result.userSeq = sliceView(r.user_seq);
  result.hasUserCost = true;
  result.userCost = r.user_cost;
  result.kind = sliceView(r.kind);
  for (size_t i = 0; i < r.optimal_results_count; ++i) {
    result.optimalResults.push_back({
        .seq = string(sliceView(r.optimal_results[i].seq)),
        .cost = r.optimal_results[i].cost,
    });
  }
  return result;
}

}  // namespace

extern "C" {

VFByteSlice vf_sequence_display_lines(
    const char* seq,
    size_t seq_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(seq, seq_len, "seq")
        .transform([&](string_view seqText) {
          return packStrings(VF::SequenceDisplay::lines(
              seqText,
              {.tokenize = tokenize, .sectionize = sectionize}));
        });
  });
}

VFByteSlice vf_sequence_display_inline(
    const char* seq,
    size_t seq_len,
    bool tokenize) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(seq, seq_len, "seq")
        .transform([&](string_view seqText) {
          return packString(VF::SequenceDisplay::inlineText(
              seqText,
              {.tokenize = tokenize, .sectionize = false}));
        });
  });
}

VFByteSlice vf_sequence_display_key_notation(
    const char* text,
    size_t text_len) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(text, text_len, "text")
        .transform([](string_view textValue) {
          return packString(VF::SequenceDisplay::displayKeyNotation(textValue));
        });
  });
}

VFByteSlice vf_sequence_display_typed_text(
    const char* text,
    size_t text_len,
    bool key_notation) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(text, text_len, "text")
        .transform([&](string_view textValue) {
          return packString(key_notation
              ? VF::SequenceDisplay::displayTypedText(textValue)
              : VF::SequenceDisplay::displayLiteralTypedText(textValue));
        });
  });
}

VFByteSlice vf_sequence_display_typed_chunks(
    const VFTypedChunk* chunks,
    size_t chunks_count,
    bool tokenize) {
  static string result_storage;
  return guarded(result_storage, [&] {
    vector<VF::SequenceDisplay::TypedChunk> typed;
    typed.reserve(chunks_count);
    for (size_t i = 0; i < chunks_count; ++i) {
      typed.push_back({
          .kind = string(sliceView(chunks[i].kind)),
          .text = string(sliceView(chunks[i].text)),
      });
    }
    return packString(VF::SequenceDisplay::typedChunksInline(typed, tokenize));
  });
}

VFByteSlice vf_format_result_position(
    int start_row,
    int start_col,
    int end_row,
    int end_col) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return packString(VF::SessionView::formatPosition(VF::SessionView::Result{
        .startRow = start_row,
        .startCol = start_col,
        .endRow = end_row,
        .endCol = end_col,
    }));
  });
}

VFByteSlice vf_format_result_kind_suffix(
    const char* kind,
    size_t kind_len) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(kind, kind_len, "kind")
        .transform([](string_view kindText) {
          return packString(VF::SessionView::formatKindSuffix(
              VF::SessionView::Result{.kind = string(kindText)}));
        });
  });
}

VFByteSlice vf_format_result_message(
    const char* title,
    size_t title_len,
    const VFSessionResult* result,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return guarded(result_storage, [&] {
    return requiredBytes(title, title_len, "title")
        .transform([&](string_view titleText) {
          return packString(VF::SessionView::formatMessage(
              titleText,
              sessionResult(*result),
              {.tokenize = tokenize, .sectionize = sectionize}));
        });
  });
}

}

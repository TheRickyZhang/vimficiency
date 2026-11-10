#include "LuaExports/Api.h"

import std.compat;
import vf.LuaExports.Common;
import vf.Interpreter.SequenceDisplay;
import vf.Session.ResultView;
import vf.Utils.ParseNumber;

using namespace std;
namespace helpers = VF::LuaExports::helpers;
namespace payload = VF::LuaExports::payload;
using VF::LuaExports::Result;
using payload::encodeStringList;

namespace {

Result<int> parseIntField(string_view text, string_view name) {
  auto parsed = parseInt(text);
  if (!parsed) return helpers::unexpectedError("invalid " + string(name));
  return *parsed;
}

Result<double> parseDoubleField(string_view text, string_view name) {
  auto parsed = parseDouble(text);
  if (!parsed) return helpers::unexpectedError("invalid " + string(name));
  return *parsed;
}

Result<VF::SessionView::Result> decodeSessionResult(string_view encoded) {
  return payload::decodeLengthPrefixedStrings(encoded)
      .and_then([](const vector<string>& fields)
          -> Result<VF::SessionView::Result> {
        if (fields.size() != 10) {
          return helpers::unexpectedError(
              "invalid session result display payload");
        }

        VF::SessionView::Result result;
        auto lines = payload::decodeLengthPrefixedStrings(fields[0]);
        if (!lines) return unexpected(lines.error());
        result.lines = std::move(*lines);

        struct IntField { int* dst; size_t index; string_view name; };
        for (auto [dst, index, name] : {
                 IntField{&result.startRow, 1, "start_row"},
                 IntField{&result.startCol, 2, "start_col"},
                 IntField{&result.endRow, 3, "end_row"},
                 IntField{&result.endCol, 4, "end_col"},
             }) {
          auto value = parseIntField(fields[index], name);
          if (!value) return unexpected(value.error());
          *dst = *value;
        }
        result.userSeq = fields[5];

        if (fields[6] == "1") {
          auto userCost = parseDoubleField(fields[7], "user_cost");
          if (!userCost) return unexpected(userCost.error());
          result.hasUserCost = true;
          result.userCost = *userCost;
        } else if (fields[6] != "0") {
          return helpers::unexpectedError("invalid user_cost presence flag");
        }

        result.finishReason = fields[8];

        auto recommendationFields =
            payload::decodeLengthPrefixedStrings(fields[9]);
        if (!recommendationFields) {
          return unexpected(recommendationFields.error());
        }
        if (recommendationFields->size() % 2 != 0) {
          return helpers::unexpectedError("invalid recommendation payload");
        }
        for (size_t i = 0; i < recommendationFields->size(); i += 2) {
          auto cost = parseDoubleField((*recommendationFields)[i + 1],
                                       "recommendation cost");
          if (!cost) return unexpected(cost.error());
          result.optimalResults.push_back({
              .seq = (*recommendationFields)[i],
              .cost = *cost,
          });
        }
        return result;
      });
}

// Shared shape of the two labelled formatters: a label string plus an encoded
// session result, handed to `format(label, result, options)`.
template <typename Format>
Result<string> formatLabelled(
    const char* label,
    size_t labelLen,
    string_view labelName,
    const char* encodedResult,
    size_t encodedResultLen,
    bool tokenize,
    bool sectionize,
    Format format) {
  return helpers::requiredBytes(label, labelLen, labelName)
      .and_then([&](string_view labelText) {
        return helpers::requiredBytes(
            encodedResult,
            encodedResultLen,
            "encoded_result")
            .and_then(decodeSessionResult)
            .transform([&](const VF::SessionView::Result& result) {
              return format(
                  labelText,
                  result,
                  VF::SequenceDisplay::Options{
                      .tokenize = tokenize, .sectionize = sectionize});
            });
      });
}

}  // namespace

extern "C" {

VFByteSlice vf_sequence_display_lines(
    const char* seq,
    size_t seq_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .transform([&](string_view seqText) {
            return encodeStringList(VF::SequenceDisplay::lines(
                seqText,
                {.tokenize = tokenize, .sectionize = sectionize}));
          }));
}

VFByteSlice vf_sequence_display_inline(
    const char* seq,
    size_t seq_len,
    bool tokenize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(seq, seq_len, "seq")
          .transform([&](string_view seqText) {
            return VF::SequenceDisplay::inlineText(
                seqText,
                {.tokenize = tokenize, .sectionize = false});
          }));
}

VFByteSlice vf_sequence_display_key_notation(
    const char* text,
    size_t text_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(text, text_len, "text")
          .transform([](string_view textValue) {
            return VF::SequenceDisplay::displayKeyNotation(textValue);
          }));
}

VFByteSlice vf_sequence_display_typed_text(
    const char* text,
    size_t text_len,
    bool key_notation) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(text, text_len, "text")
          .transform([&](string_view textValue) {
            return key_notation
                ? VF::SequenceDisplay::displayTypedText(textValue)
                : VF::SequenceDisplay::displayLiteralTypedText(textValue);
          }));
}

VFByteSlice vf_sequence_display_typed_chunks(
    const char* encoded_chunks,
    size_t encoded_chunks_len,
    bool tokenize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(
          encoded_chunks,
          encoded_chunks_len,
          "encoded_chunks")
          .and_then(payload::decodeLengthPrefixedStrings)
          .and_then([&](const vector<string>& fields) -> Result<string> {
            if (fields.size() % 2 != 0) {
              return helpers::unexpectedError("invalid typed chunk payload");
            }

            vector<VF::SequenceDisplay::TypedChunk> chunks;
            chunks.reserve(fields.size() / 2);
            for (size_t i = 0; i < fields.size(); i += 2) {
              chunks.push_back({
                  .kind = fields[i],
                  .text = fields[i + 1],
              });
            }
            return VF::SequenceDisplay::typedChunksInline(chunks, tokenize);
          }));
}

VFByteSlice vf_format_result_position(
    int start_row,
    int start_col,
    int end_row,
    int end_col) {
  static string result_storage;
  result_storage = VF::SessionView::formatPosition(VF::SessionView::Result{
      .startRow = start_row,
      .startCol = start_col,
      .endRow = end_row,
      .endCol = end_col,
  });
  return helpers::byteSlice(result_storage);
}

VFByteSlice vf_format_result_reason_suffix(
    const char* reason,
    size_t reason_len) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(reason, reason_len, "finish_reason")
          .transform([](string_view reasonText) {
            return VF::SessionView::formatReasonSuffix(
                VF::SessionView::Result{.finishReason = string(reasonText)});
          }));
}

VFByteSlice vf_format_result_body(
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      helpers::requiredBytes(
          encoded_result,
          encoded_result_len,
          "encoded_result")
          .and_then(decodeSessionResult)
          .transform([&](const VF::SessionView::Result& result) {
            return encodeStringList(VF::SessionView::formatBody(
                result,
                {.tokenize = tokenize, .sectionize = sectionize}));
          }));
}

VFByteSlice vf_format_result_message(
    const char* title,
    size_t title_len,
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatLabelled(
          title, title_len, "title", encoded_result, encoded_result_len,
          tokenize, sectionize,
          [](string_view titleText, const VF::SessionView::Result& result,
             VF::SequenceDisplay::Options options) {
            return VF::SessionView::formatMessage(titleText, result, options);
          }));
}

VFByteSlice vf_format_saved_result_lines(
    const char* name,
    size_t name_len,
    const char* encoded_result,
    size_t encoded_result_len,
    bool tokenize,
    bool sectionize) {
  static string result_storage;
  return helpers::storeBytes(
      result_storage,
      formatLabelled(
          name, name_len, "name", encoded_result, encoded_result_len,
          tokenize, sectionize,
          [](string_view nameText, const VF::SessionView::Result& result,
             VF::SequenceDisplay::Options options) {
            return encodeStringList(
                VF::SessionView::formatSavedLines(nameText, result, options));
          }));
}

}

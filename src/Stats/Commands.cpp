module vf.Stats.Commands;

import std.compat;
import vf.Interpreter.SequenceDisplay;
import vf.Interpreter.SequenceParser;
import vf.Types.KeyNotation;

using namespace std;

namespace Stats {

namespace {

// Below this many uses by either side, a ratio is mostly noise.
constexpr int MIN_OCCURRENCES = 10;
constexpr size_t ROWS_SHOWN = 8;
// Shares within 5% of each other count as the same.
constexpr double DEADBAND = 0.05;

size_t countDigits(string_view text) {
  size_t i = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') i++;
  return i;
}

// fa, Fb, 2tc, re → f_, F_, 2t_, r_: the target character is arbitrary.
string collapseTarget(string_view token) {
  const size_t i = countDigits(token);
  if (i < token.size() && string_view("fFtTrR").contains(token[i])) {
    const string_view target = token.substr(i + 1);
    if (target.size() == 1 || (target.size() > 2 && target.front() == '<' && target.back() == '>')) {
      return string(token.substr(0, i + 1)) + "_";
    }
  }
  return string(token);
}

string displayTyped(string_view text) {
  string out;
  int stretch = 0;
  auto flush = [&] {
    if (stretch > 0) out += format("⟨{}⟩", stretch);
    stretch = 0;
  };
  for (const string_view key : splitKeys(text)) {
    if (isCharacterKey(key) && parseDisplayChar(key) != '\n') {
      stretch++;
      continue;
    }
    flush();
    out += VF::SequenceDisplay::displayKeyNotation(key);
  }
  flush();
  return out;
}

// `ce` and `cw` act alike on a word (`:h cw`), as do `cE` and `cW`.
string rowName(string_view command) {
  const size_t i = countDigits(command);
  const string_view rest = command.substr(i);
  if (rest == "ce" || rest == "cw") return string(command.substr(0, i)) + "ce|cw";
  if (rest == "cE" || rest == "cW") return string(command.substr(0, i)) + "cE|cW";
  return string(command);
}

int total(const CommandCounts& counts) {
  int sum = 0;
  for (const auto& [command, n] : counts.commands) sum += n;
  return sum;
}

struct Interned {
  vector<string> names;
  map<string, int, less<>> ids;
};

Interned& interned() {
  static Interned table;
  return table;
}

}  // namespace

void CommandCounts::merge(const CommandCounts& other) {
  for (const auto& [command, n] : other.commands) commands[command] += n;
  typedChars += other.typedChars;
}

void CommandCounts::merge(const RunCommands& other) {
  const vector<string>& names = interned().names;
  for (const auto& [id, n] : other.commands) commands[names[id]] += n;
  typedChars += other.typedChars;
}

bool RunCommands::uses(int command) const {
  const auto it = ranges::lower_bound(commands, command, {}, &pair<int, int>::first);
  return it != commands.end() && it->first == command;
}

RunCommands intern(const CommandCounts& counts) {
  Interned& table = interned();
  RunCommands out{.typedChars = counts.typedChars};
  for (const auto& [command, n] : counts.commands) {
    const auto [it, added] = table.ids.try_emplace(command, ssize(table.names));
    if (added) table.names.push_back(command);
    out.commands.emplace_back(it->second, n);
  }
  ranges::sort(out.commands);
  return out;
}

int internedId(string_view command) {
  const Interned& table = interned();
  const auto it = table.ids.find(command);
  return it == table.ids.end() ? -1 : it->second;
}

void countKeys(CommandCounts& counts, string_view keys, bool insertText) {
  const auto tokens = parseSequence(keys);
  if (!tokens) {
    counts.commands[string(keys)]++;
    return;
  }
  for (const TaggedToken& token : *tokens) {
    switch (token.kind) {
      case TokenKind::Escape:
        break;
      case TokenKind::TypedText:
        if (insertText) countInsertKeys(counts, token.token);
        break;
      default:
        counts.commands[collapseTarget(token.token)]++;
    }
  }
}

void countInsertKeys(CommandCounts& counts, string_view pressed) {
  for (const string_view key : splitKeys(pressed)) {
    if (isCharacterKey(key)) {
      counts.typedChars++;
    } else {
      counts.commands[string(key)]++;
    }
  }
}

string displayKeys(string_view keys) {
  const auto tokens = parseSequence(keys);
  if (!tokens) return VF::SequenceDisplay::displayKeyNotation(keys);
  string out;
  for (const TaggedToken& token : *tokens) {
    out += token.kind == TokenKind::TypedText ? displayTyped(token.token)
                                               : VF::SequenceDisplay::displayKeyNotation(token.token);
  }
  return out;
}

CommandComparison compareCommands(const CommandCounts& user, const CommandCounts& optimizer) {
  struct Tally {
    int user = 0;
    int optimizer = 0;
    map<string, int> userVariants;
    set<string> variants;
  };
  map<string, Tally> rows;
  for (const auto& [command, n] : user.commands) {
    Tally& row = rows[rowName(command)];
    row.user += n;
    row.userVariants[command] += n;
    row.variants.insert(command);
  }
  for (const auto& [command, n] : optimizer.commands) {
    Tally& row = rows[rowName(command)];
    row.optimizer += n;
    row.variants.insert(command);
  }

  const int userTotal = total(user);
  const int optimizerTotal = total(optimizer);
  CommandComparison out;
  for (const auto& [text, row] : rows) {
    if (row.user + row.optimizer < MIN_OCCURRENCES) continue;
    const double userShare = userTotal > 0 ? double(row.user) / userTotal : 0;
    const double optimizerShare = optimizerTotal > 0 ? double(row.optimizer) / optimizerTotal : 0;
    CommandRow entry{.text = text,
                     .user = row.user,
                     .optimizer = row.optimizer,
                     .variants = {row.variants.begin(), row.variants.end()}};
    entry.ratio = optimizerShare > 0 ? userShare / optimizerShare
                  : userShare > 0    ? numeric_limits<double>::infinity()
                                     : 0;
    if (text.contains('|')) {
      for (const auto& [variant, n] : row.userVariants) {
        if (!entry.split.empty()) entry.split += " · ";
        entry.split += variant + " " + to_string(n);
      }
    }
    if (entry.ratio > 1 + DEADBAND) {
      out.more.push_back(std::move(entry));
    } else if (entry.ratio < 1 - DEADBAND) {
      out.less.push_back(std::move(entry));
    }
  }

  auto furthest = [](bool above) {
    return [above](const CommandRow& a, const CommandRow& b) {
      if (a.ratio != b.ratio) return above ? a.ratio > b.ratio : a.ratio < b.ratio;
      return a.text < b.text;
    };
  };
  ranges::sort(out.more, furthest(true));
  ranges::sort(out.less, furthest(false));
  if (out.more.size() > ROWS_SHOWN) out.more.resize(ROWS_SHOWN);
  if (out.less.size() > ROWS_SHOWN) out.less.resize(ROWS_SHOWN);
  return out;
}

}  // namespace Stats

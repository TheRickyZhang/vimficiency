module;
#include <cassert>

module vf.Session.Snapshot;

import std.compat;

using namespace std;

string formatSnapshotParseError(const SnapshotParseError& error) {
  switch (error.kind) {
    case SnapshotParseErrorKind::Empty:
      return "Snapshot empty";
    case SnapshotParseErrorKind::BadHeader:
      return "bad header";
    case SnapshotParseErrorKind::UnsupportedVersion:
      return "unsupported version";
    case SnapshotParseErrorKind::MissingFiletype:
      return "No filetype";
    case SnapshotParseErrorKind::MissingBufferName:
      return "No buffer name";
    case SnapshotParseErrorKind::MissingCursorPosition:
      return "No row or col";
    case SnapshotParseErrorKind::InvalidCursorPosition:
      return "Bad row or col";
    case SnapshotParseErrorKind::MissingNavContext:
      return "No navContext";
    case SnapshotParseErrorKind::InvalidNavContext:
      return "Bad navContext";
    case SnapshotParseErrorKind::CannotRead:
      return "Cannot read snapshot file";
  }
  assert(false && "Unhandled SnapshotParseErrorKind");
  return "unknown snapshot parse error";
}

expected<Snapshot, SnapshotParseError> parseSnapshot(string_view bytes) {
  istringstream in{string(bytes)};
  string header;
  if(!getline(in, header)) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::Empty});
  }

  string magic;
  int version;
  {
    istringstream ss(header);
    if(! (ss >> magic >> version)) {
      return unexpected(SnapshotParseError{SnapshotParseErrorKind::BadHeader});
    }
  }
  if(magic != "vimficiency" || version != 1) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::UnsupportedVersion});
  }

  string filetype;
  if(!getline(in, filetype)) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::MissingFiletype});
  }

  string bufname;
  if(!getline(in, bufname)) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::MissingBufferName});
  }

  int row, col;
  string rowcol;
  if(!getline(in, rowcol)) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::MissingCursorPosition});
  }
  {
    istringstream ss(rowcol);
    if(!(ss >> row >> col)) {
      return unexpected(SnapshotParseError{SnapshotParseErrorKind::InvalidCursorPosition});
    }
  }

  int topRow, bottomRow, windowHeight, scrollAmount;
  string navContext;
  if(!getline(in, navContext)) {
    return unexpected(SnapshotParseError{SnapshotParseErrorKind::MissingNavContext});
  }
  {
    istringstream ss(navContext);
    if(!(ss >> topRow >> bottomRow >> windowHeight >> scrollAmount)) {
      return unexpected(SnapshotParseError{SnapshotParseErrorKind::InvalidNavContext});
    }
  }

  Lines lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }
  // Neovim buffers always have >=1 line; an empty content section is an empty
  // buffer, represented as a single empty line so downstream never sees 0 lines.
  if (lines.empty()) {
    lines.push_back("");
  }

  return Snapshot{std::move(bufname), std::move(filetype), row, col,
                  topRow, bottomRow, windowHeight, scrollAmount, std::move(lines)};
}

expected<Snapshot, SnapshotParseError> load_snapshot(const std::filesystem::path& path) {
  ifstream in(path);
  if(!in) return unexpected(SnapshotParseError{SnapshotParseErrorKind::CannotRead});

  ostringstream buffer;
  buffer << in.rdbuf();
  return parseSnapshot(buffer.str());
}

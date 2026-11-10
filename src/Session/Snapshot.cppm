export module vf.Session.Snapshot;

import std.compat;
export import vf.Types.Lines;

export {

struct Snapshot {
  std::string bufname;
  std::string filetype;
  int row;
  int col;
  int topRow;
  int bottomRow;
  int windowHeight;
  int scrollAmount;
  Lines lines;

  inline Snapshot(std::string bufname, std::string filetype, int row, int col,
           int topRow, int bottomRow, int windowHeight, int scrollAmount,
           Lines lines)
      : bufname(std::move(bufname)), filetype(std::move(filetype)), row(row),
        col(col), topRow(topRow), bottomRow(bottomRow),
        windowHeight(windowHeight), scrollAmount(scrollAmount),
        lines(std::move(lines)) {}
};

enum class SnapshotParseErrorKind {
  Empty,
  BadHeader,
  UnsupportedVersion,
  MissingFilename,
  MissingBufferName,
  MissingCursorPosition,
  InvalidCursorPosition,
  MissingNavContext,
  InvalidNavContext,
  CannotRead,
};

struct SnapshotParseError {
  SnapshotParseErrorKind kind;
};

std::string formatSnapshotParseError(const SnapshotParseError& error);
std::expected<Snapshot, SnapshotParseError> parseSnapshot(std::string_view bytes);
std::expected<Snapshot, SnapshotParseError> load_snapshot(const std::filesystem::path &path);

}  // export

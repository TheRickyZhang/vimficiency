export module vf.VimCore.VimOptions;

import std.compat;

export {

// VimOptions.h - Compile-time options for Vim/Neovim behavior differences,
// plus runtime-configurable shiftwidth.
//
// By default, vimficiency uses NEOVIM defaults (the modern sensible defaults).
// Define VIMFICIENCY_LEGACY_VIM to use traditional Vim defaults instead.
//
// Key differences:
//
// | Option       | Neovim (default) | Vim (LEGACY_VIM)          |
// |--------------|------------------|---------------------------|
// | startofline  | OFF              | ON                        |
// | joinspaces   | OFF              | ON                        |
// | autoindent   | ON               | OFF                       |
//
// startofline affects: gg, G, <C-d>, <C-u>, <C-f>, <C-b>, dd, H, M, L
//   - OFF: cursor preserves column position
//   - ON:  cursor moves to first non-blank character
//
// joinspaces affects: J command
//   - OFF: single space after .!? when joining
//   - ON:  two spaces after .!? when joining

namespace VimOptions {

constexpr bool startOfLine() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

constexpr bool joinSpaces() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

constexpr bool autoindent() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return false;
#else
    return true;
#endif
}

// Shiftwidth: number of spaces per indent level.
// Affects <BS> in autoindent context (deletes to previous shiftwidth boundary).
// Fallback for requests recorded before buffer-local options were captured.
inline std::atomic<int>& shiftwidthRef() {
    static std::atomic<int> sw{8};
    return sw;
}

inline int shiftwidth() {
    return shiftwidthRef().load(std::memory_order_relaxed);
}

} // namespace VimOptions

struct IndentOptions {
    bool autoindent = VimOptions::autoindent();
    bool expandtab = false;
    bool copyindent = false;
    int tabstop = 8;
    int shiftwidth = 8;

    inline int backspaceWidth() const { return shiftwidth == 0 ? tabstop : shiftwidth; }

    inline int columns(std::string_view indent) const {
        int col = 0;
        for (char c : indent) col += c == '\t' ? tabstop - col % tabstop : 1;
        return col;
    }

    // cc keeps the original bytes. o, O and <CR> recreate the displayed indent.
    inline std::string openedIndent(std::string_view line) const {
        if (!autoindent) return {};
        const auto indent = leadingIndent(line);
        if (copyindent || indent.empty()) return std::string(indent);
        const bool tabs = indent.find('\t') != indent.npos;
        if (!tabs && (expandtab || std::ssize(indent) < tabstop)) return std::string(indent);
        const int width = tabs ? columns(indent) : std::ssize(indent);
        if (expandtab) return std::string(width, ' ');
        return std::string(width / tabstop, '\t') + std::string(width % tabstop, ' ');
    }

    static inline std::string_view leadingIndent(std::string_view line) {
        return line.substr(0, line.find_first_not_of(" \t"));
    }
};

}  // export

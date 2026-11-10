export module vf.Types.Token;

import std.compat;

export {

struct Token final : std::string {
  using std::string::string;

  inline Token(const std::string& value) : std::string(value) {}
  inline Token(std::string&& value) : std::string(std::move(value)) {}
  inline Token(std::string_view value) : std::string(value) {}

  inline operator std::string_view() const {
    return std::string_view(data(), size());
  }
};

}  // export

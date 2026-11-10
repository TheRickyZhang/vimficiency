export module vf.Utils.CoutCapture;

import std.compat;

export {

// RAII for redirecting cout
class CoutCapture {
  std::ostringstream captured;
  std::streambuf* old_buf;
public:
  // rdbuf overloaded with fetch_set pattern here. 
  inline CoutCapture() : old_buf(std::cout.rdbuf(captured.rdbuf())) {}
  inline ~CoutCapture() { std::cout.rdbuf(old_buf); }
  inline std::string str() const {
    return captured.str();
  }
};

}  // export

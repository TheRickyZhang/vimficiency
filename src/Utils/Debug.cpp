module vf.Utils.Debug;

import std.compat;

std::ostringstream& dout() {
  thread_local std::ostringstream stream;
  return stream;
}

export module vf.Utils.HashCombine;

import std.compat;

export {

template <class T>
inline void hashCombine(size_t& h, const T& v) {
  h ^= std::hash<T>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
}

}  // export

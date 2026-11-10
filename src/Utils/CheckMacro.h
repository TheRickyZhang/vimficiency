#pragma once

// Textual on purpose: macros cannot be exported from a module. Users must also
// `import vf.Utils.Debug;` for `VF::detail::check_fail`.
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond))                                                               \
      ::VF::detail::check_fail(__FILE__, __LINE__, #cond, msg);                \
  } while (0)

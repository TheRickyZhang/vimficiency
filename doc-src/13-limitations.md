---
title: "Known limitations"
---

# Known limitations

- **Needs Neovim nightly (0.13-dev).** Capture runs off the `CmdAtom`
  event; `setup()` raises on older Neovim.

- **Screen-line motions** `gj`/`gk` are approximated as `j`/`k`. Exact on
  unwrapped lines; off by a little when wrapping is in play.

- **Multi-buffer edits** aren't supported within a single session. If you
  finish a session while a different buffer is focused, it is rejected.
  Transient window changes (floating pickers, temporary splits) are fine —
  the session stays alive and keys typed outside the session window are
  silently ignored.

- **Large regions** (more than `MAX_SEARCH_LINES`) are refused with an error;
  split the edit up or increase the limit if you really want this.

- **Long sessions** may hit the optimizer's search budget; expect diminishing
  suggestion quality beyond roughly a dozen lines of edits.

- **Mappings to Lua callbacks can't be audited as motion.** A key mapped to a
  Lua function that edits the buffer or moves the cursor produces an
  unreplayable capture event. By default (`lossy_mappings = "warn"`) the keys
  you typed are recorded in its place with a once-per-session notice; replay
  is faithful only when the function did what those keys do in plain Vim.
  `"allow"` records silently, and `"drop"` ends the session with a warning that
  names the mapping (see [Configuration](09-configuration.md)). Mappings whose
  RHS is a key sequence are fine — they are captured as the resolved
  post-mapping keys. Type the underlying keys directly when you want a specific
  motion audited.

- **Recall only records actions in normal file buffers** (`&buftype == ""`).
  Actions in help, quickfix, terminal, prompt, or scratch buffers are skipped —
  they aren't meaningful for nav or transform optimization, and recording them
  would cause recall queries to fail with buffer-mismatch when you return to
  your file. Input typed into an async floating prompt (e.g. `vim.ui.input`
  overridden by dressing.nvim) is filtered automatically.

---
title: "Known limitations"
---

# Known limitations

- **Needs Neovim nightly (0.13-dev).** Capture runs off the `CmdAtom`
  event; `setup()` raises on older Neovim.

- **Screen-line motions.** The optimizer approximates `gj`/`gk` as `j`/`k`.
  The recorded pane shows the cursor position Neovim observed.

- **Multi-buffer edits** aren't supported within a single session. If you
  finish a session while a different buffer is focused, it is rejected.
  Transient window changes (floating pickers, temporary splits) are fine —
  the session stays alive and keys typed outside the session window are
  silently ignored.

- **Large regions** include intermediate cursor visits, edited lines and padding.
  Regions beyond `MAX_SEARCH_LINES` are refused with an error;
  split the edit up or increase the limit if you really want this.

- **Long sessions** may hit the optimizer's search budget; expect diminishing
  suggestion quality beyond roughly a dozen lines of edits.

- **Mappings to Lua callbacks can't be audited as motion.** A key mapped to a
  Lua function that edits the buffer or moves the cursor produces an
  unreplayable capture event. By default (`lossy_mappings = "warn"`) the keys
  you typed label the action and a once-per-session notice appears. View
  shows the observed effect, including edits made through plugin APIs.
  `"allow"` records silently, and `"drop"` ends the session with a warning that
  names the mapping (see [Configuration](09-configuration.md)). Mappings whose
  RHS is a key sequence are fine — they are captured as the resolved
  post-mapping keys. Either way your cost counts the keys you pressed, not
  what they expanded to. The optimizer uses the captured start and goal.
  Type the underlying keys directly when you want a specific motion audited.

- **Some recorded keys are inaccurate** until Neovim reports them per command.
  Text a Lua Insert-mode mapping inserts (nvim-ts-autotag's `>`) is missing
  from the key label. View still shows the inserted text at the next observation.
  Your cost can be off by a key when commands arrive in one burst, or when a
  Vimscript plugin reads keys with `getchar()`.

- **Recall only records actions in normal file buffers** (`&buftype == ""`).
  Actions in help, quickfix, terminal, prompt, or scratch buffers are skipped —
  they aren't meaningful for nav or transform optimization, and recording them
  would cause recall queries to fail with buffer-mismatch when you return to
  your file. Input typed into an async floating prompt (e.g. `vim.ui.input`
  overridden by dressing.nvim) is filtered automatically.

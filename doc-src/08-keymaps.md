---
title: "Binding keys to Vimfy actions"
---

# Binding keys to Vimfy actions

Vimfy measures the keys you type, so a key that *invokes* Vimfy must
not itself count as motion. Bindings made through `vimfy.map()` or a
`<Plug>` map retract their own trigger keystroke from every active
recording when they fire. Two ways to bind, one ergonomic way that
doesn't work:

## 1. `vimfy.map()` (recommended)

The simplest path — one line per binding, handles all subcommands:

```lua
local vimfy = require('vimficiency')

vimfy.map('n', '<leader>vs', 'start a')
vimfy.map('n', '<leader>vf', 'finish a')
vimfy.map('n', '<leader>vq', 'save @ quick')
```

The string argument is parsed like `:Vimfy <args>`: first word is the
subcommand, the rest are forwarded verbatim. Any `:Vimfy ...` command
line works as a `vimfy.map` spec.

The fourth argument is the `opts` table passed through to
`vim.keymap.set` (`buffer`, `desc`, `silent`, ...). `silent` defaults
to `true`; `desc` defaults to `"Vimfy <spec>"`.

### Binding arbitrary Lua

Pass a function instead of a string when you need to branch or compose:

```lua
vimfy.map('n', 'Z', function()
  if vim.bo.buftype == "" then
    vim.cmd('Vimfy start a')
  end
end)
```

The callback is wrapped so that the `Z` keystroke is retracted before
your function runs; a plain `vim.keymap.set` to the same function would
record it as motion.

## 2. `<Plug>` maps

For users who prefer the Vim convention, each common action has a
`<Plug>` name. Bind with `remap = true` (or `nmap` in vimscript):

```vim
nmap <leader>vs <Plug>VimfyStartA
nmap <leader>vf <Plug>VimfyFinishA
```

```lua
vim.keymap.set('n', '<leader>vs', '<Plug>VimfyStartA', { remap = true })
```

**Available `<Plug>` names:**

| Name                                 | Action                               |
|--------------------------------------|--------------------------------------|
| `<Plug>VimfyStart{A,B,C,D,E}`        | `:Vimfy start <alias>`               |
| `<Plug>VimfyWatch{A,B,C,D,E}`        | `:Vimfy watch <alias>`               |
| `<Plug>VimfyFinish{A,B,C,D,E}`       | `:Vimfy finish <alias>`              |
| `<Plug>VimfyClose{A,B,C,D,E}`        | `:Vimfy close <alias>`               |
| `<Plug>VimfyPlay{A,B,C,D,E}`         | `:Vimfy play <alias>`                |
| `<Plug>VimfySuggest{On,Off,Toggle}`  | `:Vimfy suggest <on\|off\|toggle>`   |
| `<Plug>VimfyList`                    | `:Vimfy list`                        |
| `<Plug>VimfyConfig`                  | `:Vimfy config`                      |
| `<Plug>VimfyHelp`                    | `:Vimfy help`                        |

`<Plug>` covers the common cases; `vimfy.map()` covers everything
(including arbitrary args, e.g. `save @ quick`, which no `<Plug>`
name exposes directly).

## Direct `:Vimfy ...` typing

Works as-is. Cmdline input is never counted as motion.

## What doesn't work

A `nnoremap`/`nmap` whose RHS is a literal Ex command:

```vim
nnoremap X :Vimfy start a<CR>
nmap     Y <Cmd>Vimfy start a<CR>
```

The `X` or `Y` keystroke is delivered to Vimfy's tracker *before* the
mapping resolves — we can't retroactively uncount it. To fix: move the
binding into `vimfy.map()` or a `<Plug>` map.

At setup, Vimfy scans pre-existing mappings for this pattern and
emits a one-shot warning listing each offender. The scan can't see
mappings defined *after* setup (or Lua-callback RHS, or buffer-local
mappings added later) — if an LHS keeps showing up as motion, check
that the binding is routed through `vimfy.map()` / `<Plug>`.

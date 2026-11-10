---
title: "Binding keys to Vimfy actions"
---

# Binding keys to Vimfy actions

Vimfy measures the keys you type, but a key that *invokes* Vimfy is
never mistaken for motion. Capture runs off Neovim's per-command
`CmdAtom` event, which reports the resolved action: a binding that runs
a `:Vimfy` command (or any Lua that doesn't edit the buffer) produces an
inert event and is excluded automatically.

So there is no special binding API — use whatever Vim already gives you.

## 1. `vim.keymap.set` with a `<Cmd>` map (recommended)

Bind the LHS to the `:Vimfy` command directly:

```lua
vim.keymap.set('n', '<leader>vs', '<Cmd>Vimfy start a<CR>',   { silent = true })
vim.keymap.set('n', '<leader>vf', '<Cmd>Vimfy finish a<CR>',  { silent = true })
vim.keymap.set('n', '<leader>vq', '<Cmd>Vimfy save @ quick<CR>', { silent = true })
```

Everything after `Vimfy` is the subcommand and its arguments, exactly as
you'd type `:Vimfy ...`. `<Cmd>` runs it without leaving normal mode, and
the keypress is never recorded as motion.

In vimscript the equivalent is `nnoremap <leader>vs <Cmd>Vimfy start a<CR>`.

### Binding arbitrary Lua

Use a function RHS when you need to branch or compose:

```lua
vim.keymap.set('n', 'Z', function()
  if vim.bo.buftype == "" then
    vim.cmd('Vimfy start a')
  end
end)
```

As long as the callback doesn't edit the buffer, pressing `Z` is not
recorded as motion.

## 2. `<Plug>` maps

For users who prefer the Vim convention, each common action has a
`<Plug>` name that `setup()` registers for you. Bind with `remap = true`
(or `nmap` in vimscript):

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

`<Plug>` covers the common actions; a direct `<Cmd>Vimfy ...<CR>` map
covers everything else (including arbitrary args, e.g. `save @ quick`,
which no `<Plug>` name exposes).

## Direct `:Vimfy ...` typing and Ex-command maps

Both work as-is. Typing `:Vimfy ...` is never counted as motion, and
neither is a mapping whose RHS is a literal Ex command:

```vim
nnoremap X :Vimfy start a<CR>
```

Because the `CmdAtom` event reports the resolved command rather than the
raw keystroke, the `X` press is recognized as a non-editing action and
excluded.

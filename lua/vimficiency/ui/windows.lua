---@class (exact) VF.Ui.Windows
---@field DEFAULT_SIDE_PANE_WIDTH    integer
---@field configure_scratch_window   fun(win: integer, overrides?: table<string, any>)
---@field configure_side_pane        fun(win: integer, overrides?: table<string, any>)
---@field hide_cursor_while_active   fun(win: integer, buf: integer): fun()
---@field centered_popup_geometry    fun(content_width: integer, content_height: integer, opts?: { min_width?: integer, min_height?: integer }): integer, integer, integer, integer
---@field show_output                fun(title: string, text?: string, opts?: VF.Ui.OutputOpts): integer, integer
---@field DEFAULT_HELP_TAG           string
---@field open_help                  fun(tag: string)
---@field set_buffer_keymaps         fun(buf: integer, keymaps: VF.Keymap[])
---@field show_keymap_help           fun(title: string, keymaps: VF.Keymap[])
---@field with_standard_ui_keymaps   fun(keymaps: VF.Keymap[], opts: VF.StandardUiKeyOpts): VF.Keymap[]
local M = {}

local highlights = require("vimficiency.ui.highlights")
local util = require("vimficiency.util")
local v = vim.api

-- Shared scratch-window chrome
-- Every vimfy-owned window is a scratch UI pane, not a real buffer the
-- user edits. Row numbers, sign columns, fold columns, color columns, list
-- chars, spell underlines, and an inherited winbar all turn it into noise.
-- Without an explicit reset, each window inherits from the user's defaults
-- (`:set number relativenumber signcolumn=yes`, colorscheme winbar, etc.),
-- which is why panes used to look different depending on who opened them.
--
-- `configure_scratch_window` is the single source of truth. Call sites that
-- need exceptions (cursorline on a selectable list, winfixheight on a status
-- bar, a custom statusline/winhighlight, etc.) pass them as overrides — those
-- merge on top and also cover keys not in the default set.
---@type table<string, any>
local SCRATCH_WINDOW_DEFAULTS = {
  number         = false,
  relativenumber = false,
  cursorline     = false,
  cursorcolumn   = false,
  wrap           = false,
  signcolumn     = "no",
  foldcolumn     = "0",
  colorcolumn    = "",
  list           = false,
  spell          = false,
  winbar         = "",
}

M.DEFAULT_SIDE_PANE_WIDTH = 40

---Apply the shared scratch-window chrome, then any per-site overrides.
---@param win integer
---@param overrides table<string, any>?
function M.configure_scratch_window(win, overrides)
  local merged = vim.tbl_extend("force", SCRATCH_WINDOW_DEFAULTS, overrides or {})
  for name, value in pairs(merged) do
    v.nvim_set_option_value(name, value, { win = win })
  end
end

---Apply shared chrome for fixed left/right side panes.
---@param win integer
---@param overrides table<string, any>?
function M.configure_side_pane(win, overrides)
  overrides = vim.deepcopy(overrides or {})
  local width = overrides.width or M.DEFAULT_SIDE_PANE_WIDTH
  overrides.width = nil
  if overrides.winfixwidth == nil then overrides.winfixwidth = true end
  v.nvim_win_resize(win, width, -1)
  M.configure_scratch_window(win, overrides)
end

---Swap `guicursor` to a `blend = 100` highlight while `buf` is active in
---`win`, so a view that marks its own selection shows no cursor cell.
---Returns a `cleanup()` callback that restores the saved guicursor and
---tears down the autocmds; BufWipeout also fires it.
---@param win integer
---@param buf integer
---@return fun()
function M.hide_cursor_while_active(win, buf)
  local saved
  local closed = false
  local group = v.nvim_create_augroup("vimficiency_hidden_cursor_" .. buf, { clear = true })

  local function active_here()
    return not closed
      and v.nvim_win_is_valid(win)
      and v.nvim_get_current_win() == win
      and v.nvim_win_get_buf(win) == buf
  end

  local function activate()
    if closed or saved then return end
    saved = vim.go.guicursor
    -- Normal and visual only: the command line keeps its cursor.
    vim.go.guicursor = saved .. ",n-v:" .. highlights.SETTINGS_CURSOR_HIDDEN
  end

  local function deactivate()
    if not saved then return end
    vim.go.guicursor = saved
    saved = nil
  end

  local function cleanup()
    if closed then return end
    closed = true
    deactivate()
    v.nvim_del_augroup_by_id(group)
  end

  v.nvim_create_autocmd({ "BufEnter", "WinEnter" }, {
    group = group,
    buffer = buf,
    callback = function()
      if active_here() then activate() end
    end,
    desc = "vimfy: hide cursor",
  })
  v.nvim_create_autocmd({ "BufLeave", "WinLeave" }, {
    group = group,
    buffer = buf,
    callback = deactivate,
    desc = "vimfy: restore cursor",
  })
  v.nvim_create_autocmd("BufWipeout", {
    group = group,
    buffer = buf,
    callback = cleanup,
    desc = "vimfy: clean cursor hooks",
  })

  if active_here() then activate() end
  return cleanup
end

---@class VF.Keymap
---@field lhs string
---@field handler string|function
---@field desc string
---@field summary_hidden? boolean
---@field summary_group string?
---@field summary_desc string?
---@field mode string|string[]?
---@field nowait boolean?
---@field silent boolean?
---@field expr boolean?
---@field remap boolean?

---@class VF.StandardUiKeyOpts
---@field title string
---@field summary { lhs?: string, desc?: string }|boolean|nil
---@field docs { lhs?: string, tag?: string, desc?: string }|boolean|nil
---@field settings { lhs?: string, handler: string|function, desc?: string }?

M.DEFAULT_HELP_TAG = "vimficiency"

---@param tag string
function M.open_help(tag)
  local plugin_root = util.find_plugin_root()
  pcall(vim.cmd.helptags, plugin_root .. "/doc")

  local target = tag or M.DEFAULT_HELP_TAG
  local ok = pcall(vim.cmd.help, target)
  if ok then return end

  if target ~= M.DEFAULT_HELP_TAG then
    vim.cmd("help " .. M.DEFAULT_HELP_TAG)
    return
  end
  error("vimfy: failed to open help tag '" .. tostring(target) .. "'", 0)
end

---@param buf integer
---@param keymaps VF.Keymap[]
function M.set_buffer_keymaps(buf, keymaps)
  for _, m in ipairs(keymaps) do
    local rhs = m.handler
    if type(rhs) == "function" then rhs = util.entry(rhs) end
    vim.keymap.set(m.mode or "n", m.lhs, rhs, {
      buffer = buf,
      desc = m.desc,
      nowait = m.nowait ~= false,
      silent = m.silent ~= false,
      expr = m.expr,
      remap = m.remap,
    })
  end
end

---@param content_width integer
---@param content_height integer
---@param opts? { min_width?: integer, min_height?: integer }
---@return integer win_width
---@return integer win_height
---@return integer row
---@return integer col
function M.centered_popup_geometry(content_width, content_height, opts)
  opts = opts or {}
  local min_width = opts.min_width or 56
  local min_height = opts.min_height or 12
  local ui = v.nvim_list_uis()[1] or {
    width = vim.o.columns,
    height = vim.o.lines,
  }
  local win_width = math.min(
    math.max(content_width, min_width),
    math.max(min_width, ui.width - 4))
  local win_height = math.min(
    math.max(content_height, min_height),
    math.max(min_height, ui.height - 4))
  local row = math.max(0, math.floor((ui.height - win_height) / 2) - 1)
  local col = math.max(0, math.floor((ui.width - win_width) / 2))
  return win_width, win_height, row, col
end

---@param title string
---@param keymaps VF.Keymap[]
function M.show_keymap_help(title, keymaps)
  keymaps = vim.tbl_filter(function(map) return not map.summary_hidden end, keymaps)
  local lines = {}
  local width = vim.fn.strdisplaywidth(title)
  local rows = {}

  local i = 1
  while i <= #keymaps do
    local cur = keymaps[i]
    local row = {
      lhs = cur.lhs,
      desc = cur.summary_desc or cur.desc,
    }
    if cur.summary_group then
      local lhses = { cur.lhs }
      local j = i + 1
      while j <= #keymaps and keymaps[j].summary_group == cur.summary_group do
        lhses[#lhses + 1] = keymaps[j].lhs
        j = j + 1
      end
      if #lhses > 1 then
        row.lhs = table.concat(lhses, "/")
        row.desc = cur.summary_desc or row.desc
        i = j - 1
      end
    end
    rows[#rows + 1] = row
    i = i + 1
  end

  -- Left-column width = widest lhs, min 4 so very short lists don't look cramped.
  local lhs_w = 4
  for _, row in ipairs(rows) do
    lhs_w = math.max(lhs_w, vim.fn.strdisplaywidth(row.lhs))
  end

  for _, row in ipairs(rows) do
    local pad = lhs_w - vim.fn.strdisplaywidth(row.lhs)
    local line = "  " .. row.lhs .. string.rep(" ", pad) .. "  " .. row.desc
    lines[#lines + 1] = line
    width = math.max(width, vim.fn.strdisplaywidth(line))
  end

  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)
  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false
  vim.bo[buf].filetype = "vimficiency"

  local win_width, win_height, row, col =
    M.centered_popup_geometry(width + 4, #lines)

  local win = v.nvim_open_win(buf, true, {
    relative = "editor",
    row = row,
    col = col,
    width = win_width,
    height = win_height,
    style = "minimal",
    border = "rounded",
    title = " " .. title .. " ",
    title_pos = "center",
    noautocmd = true,
  })

  M.configure_scratch_window(win)

  local function close_popup()
    if v.nvim_win_is_valid(win) then
      v.nvim_win_close(win, true)
    end
  end

  M.set_buffer_keymaps(buf, {
    { lhs = "q", handler = close_popup, desc = "Close keymap summary", nowait = true },
    { lhs = "<Esc>", handler = close_popup, desc = "Close keymap summary", nowait = true },
    { lhs = "<CR>", handler = close_popup, desc = "Close keymap summary", nowait = true },
  })
end

---@param keymaps VF.Keymap[]
---@param opts VF.StandardUiKeyOpts
---@return VF.Keymap[]
function M.with_standard_ui_keymaps(keymaps, opts)
  local merged = vim.deepcopy(keymaps)
  local summary = opts.summary
  if summary == nil then summary = true end
  if summary then
    local summary_opts = type(summary) == "table" and summary or {}
    merged[#merged + 1] = {
      lhs = summary_opts.lhs or "?",
      handler = function() M.show_keymap_help(opts.title, merged) end,
      desc = summary_opts.desc or "Show keymap summary",
      nowait = true,
    }
  end
  if opts.settings then
    merged[#merged + 1] = {
      lhs = opts.settings.lhs or "gs",
      handler = opts.settings.handler,
      desc = opts.settings.desc or "Open settings",
      nowait = true,
    }
  end
  local docs = opts.docs
  if docs then
    local docs_opts = type(docs) == "table" and docs or {}
    merged[#merged + 1] = {
      lhs = docs_opts.lhs or "g?",
      handler = function() M.open_help(docs_opts.tag or M.DEFAULT_HELP_TAG) end,
      desc = docs_opts.desc or "Full docs",
      nowait = true,
    }
  end
  return merged
end

local OUTPUT_MAX_WIDTH = 72

---@class VF.Ui.OutputOpts
---@field ui_keys? VF.StandardUiKeyOpts
---@field help_tag? string
---@field help_title? string
---@field window? table<string, any>
---@field margin? integer  # Fill the editor, leaving this many cells outside the border.

---@param margin integer
---@return vim.api.keyset.win_config
local function inset_geometry(margin)
  local inset = 2 * (margin + 1)
  return {
    relative = "editor", row = margin, col = margin,
    width = math.max(1, vim.o.columns - inset),
    height = math.max(1, vim.o.lines - inset),
  }
end

---@param title string
---@param text? string
---@param opts? VF.Ui.OutputOpts
---@return integer buf
---@return integer win
function M.show_output(title, text, opts)
  opts = opts or {}
  local lines = vim.split(text or "", "\n", { plain = true })

  local buf = v.nvim_create_buf(false, true)
  v.nvim_buf_set_name(buf, ("vimficiency://%s"):format(title:gsub("%s+", "_")))
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false
  vim.bo[buf].filetype = "vimficiency"

  local margin = opts.margin
  local geometry
  if margin then
    geometry = inset_geometry(margin)
  else
    local width = vim.fn.strdisplaywidth(title)
    for _, line in ipairs(lines) do
      width = math.max(width, vim.fn.strdisplaywidth(line))
    end
    geometry = { relative = "editor", row = 0, col = 0, height = 1,
      width = M.centered_popup_geometry(math.min(width + 2, OUTPUT_MAX_WIDTH), 1) }
  end
  local win = v.nvim_open_win(buf, true, vim.tbl_extend("force", geometry, {
    style = "minimal",
    border = "rounded",
    title = " " .. title .. " ",
    title_pos = "center",
    noautocmd = true,
  }))
  M.configure_scratch_window(win, vim.tbl_extend("force",
    { wrap = true, linebreak = true, breakindent = true }, opts.window or {}))

  if margin then
    v.nvim_create_autocmd("VimResized", {
      buffer = buf,
      callback = function() v.nvim_win_set_config(win, inset_geometry(margin)) end,
    })
  else
    -- The height depends on how the lines wrap at this width.
    local _, height, row, col =
      M.centered_popup_geometry(geometry.width, v.nvim_win_text_height(win, {}).all)
    v.nvim_win_set_config(win, { relative = "editor", row = row, col = col, height = height })
  end

  -- `g?` opens help in a split under the float, which would stay on top of it.
  v.nvim_create_autocmd("WinLeave", {
    buffer = buf,
    once = true,
    callback = function()
      vim.schedule(function()
        if v.nvim_win_is_valid(win) then v.nvim_win_close(win, true) end
      end)
    end,
    desc = "vimfy: close output float",
  })

  local keymaps = {
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close", nowait = true, summary_group = "close" },
    { lhs = "<Esc>", handler = "<cmd>close<cr>", desc = "Close", nowait = true, summary_group = "close" },
  }
  local ui_keys = opts.ui_keys
  if not ui_keys and opts.help_tag then
    ui_keys = {
      title = opts.help_title or (title .. " Keys"),
      docs = true,
    }
  end
  if ui_keys then
    keymaps = M.with_standard_ui_keymaps(keymaps, ui_keys)
  end
  M.set_buffer_keymaps(buf, keymaps)

  return buf, win
end

return M

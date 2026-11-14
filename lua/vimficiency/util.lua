---@class (exact) VF.Util
--- Windows and popups
---@field DEFAULT_SIDE_PANE_WIDTH    integer
---@field configure_scratch_window   fun(win: integer, overrides?: table<string, any>)
---@field configure_side_pane        fun(win: integer, overrides?: table<string, any>)
---@field centered_popup_geometry    fun(content_width: integer, content_height: integer, opts?: { min_width?: integer, min_height?: integer }): integer, integer, integer, integer
---@field show_output                fun(title: string, text?: string, opts?: { ui_keys?: VF.StandardUiKeyOpts, help_tag?: string, help_title?: string }): integer, integer
--- Keymaps and help
---@field DEFAULT_HELP_TAG           string
---@field open_help                  fun(tag: string)
---@field set_buffer_keymaps         fun(buf: integer, keymaps: VF.Keymap[])
---@field entry                      fun(fn: function): function
---@field show_keymap_help           fun(title: string, keymaps: VF.Keymap[])
---@field with_standard_ui_keymaps   fun(keymaps: VF.Keymap[], opts: VF.StandardUiKeyOpts): VF.Keymap[]
--- Filesystem
---@field basename                   fun(path: string): string
---@field find_plugin_root           fun(): string
--- Session ids and buffer state
---@field new_id                     fun(buf: integer): string
---@field capture_state              fun(buf: integer, win: integer): VF.BufferState
---@field check_state_inconsistencies fun(start_state: VF.BufferState, end_state: VF.BufferState)
---@field compute_search_region      fun(start_row: integer, end_row: integer, start_lines: string[], end_lines: string[], padding: integer): integer, integer
local M = {}

local v  = vim.api
local uv = vim.uv
local fs = vim.fs

local ffi_session = require("vimficiency.ffi.session")

--------------------------------------------------------------------------------
-- Shared scratch-window chrome
--------------------------------------------------------------------------------
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

--------------------------------------------------------------------------------
-- Types
--------------------------------------------------------------------------------

---@class VF.BufferState
---@field bufname string       # original buffer name (full path)
---@field filetype string      # buffer filetype
---@field row integer          # 0-indexed cursor row
---@field col integer          # 0-indexed cursor column
---@field top_row integer      # top visible row
---@field bottom_row integer   # bottom visible row
---@field window_height integer # Ctrl-F, B distance
---@field scroll_amount integer # Ctrl-D, U distance (may differ from window_height/2)
---@field lines string[]       # buffer lines at capture time

---@class VF.Keymap
---@field lhs string
---@field handler string|function
---@field desc string
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

M.basename = fs.basename or function(p)
  return p:match("([^/\\]+)$") or p
end

M.DEFAULT_HELP_TAG = "vimficiency"

local function sanitize_name(s)
  return (s:gsub("[^%w%._%-]", "_"))
end

function M.find_plugin_root()
    -- This file lives at `<plugin_root>/lua/vimficiency/util.lua`.
    local source = debug.getinfo(1, "S").source
    if source:sub(1, 1) == "@" then
        source = source:sub(2)  -- remove leading @
    end
    return vim.fn.fnamemodify(source, ":h:h:h")
end

---@param tag string
function M.open_help(tag)
  local plugin_root = M.find_plugin_root()
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

--- The error boundary for every callback Neovim invokes on our behalf (autocmd,
--- timer, keymap, `vim.ui.input`): the counterpart of `init.cmdline_entry` for
--- `:Vimfy`. Returns nothing, since an autocmd callback returning true would
--- delete the autocmd.
---@param fn function
---@return function
function M.entry(fn)
  return function(...)
    local ok, err = pcall(fn, ...)
    if not ok then vim.notify(tostring(err), vim.log.levels.ERROR) end
  end
end

---@param buf integer
---@param keymaps VF.Keymap[]
function M.set_buffer_keymaps(buf, keymaps)
  for _, m in ipairs(keymaps) do
    local rhs = m.handler
    if type(rhs) == "function" then rhs = M.entry(rhs) end
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

---@param title string
---@param text? string
---@param opts? { ui_keys?: VF.StandardUiKeyOpts, help_tag?: string, help_title?: string }
---@return integer buf
---@return integer win
function M.show_output(title, text, opts)
  opts = opts or {}
  local lines = { title }
  if text and text ~= "" then
    lines[#lines + 1] = ""
    vim.list_extend(lines, vim.split(text, "\n", { plain = true }))
  end

  vim.cmd("botright new") -- create split + window
  local win = v.nvim_get_current_win()

  local buf = v.nvim_create_buf(false, true)
  v.nvim_win_set_buf(win, buf)

  v.nvim_buf_set_name(buf, ("vimficiency://%s"):format(title:gsub("%s+", "_")))
  v.nvim_buf_set_lines(buf, 0, -1, false, lines)

  vim.bo[buf].buftype = "nofile"
  vim.bo[buf].bufhidden = "wipe"
  vim.bo[buf].swapfile = false
  vim.bo[buf].modifiable = false
  vim.bo[buf].filetype = "vimficiency"

  M.configure_scratch_window(win)

  local keymaps = {
    { lhs = "q", handler = "<cmd>close<cr>", desc = "Close vimfy view", nowait = true },
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

-- Wall-clock milliseconds plus a per-ms sequence number.
local last_ms = -1
local seq = 0
local SEQ_BITS = 12
local SEQ_MAX = (2 ^ SEQ_BITS) - 1

local function now_ms()
  local sec, usec = uv.gettimeofday()
  local ms = sec * 1000 + math.floor(usec / 1000)
  return ms, sec
end

local function wait_next_ms(prev_ms)
  local ms, sec = now_ms()
  while ms == prev_ms do
    ms, sec = now_ms()
  end
  return ms, sec
end

function M.new_id(buf)
  local name = v.nvim_buf_get_name(buf)
  local base = sanitize_name(
    (name ~= "" and M.basename(name)) or "NoName"
  )

  local ms, sec = now_ms()

  -- Clamp if the clock goes backwards.
  if ms < last_ms then
    ms = last_ms
    sec = math.floor(ms / 1000)
  end

  if ms == last_ms then
    if seq < SEQ_MAX then
      seq = seq + 1
    else
      ms, sec = wait_next_ms(last_ms)
      last_ms = ms
      seq = 0
    end
  else
    last_ms = ms
    seq = 0
  end

  local wall = os.date("%Y%m%d-%H%M%S", sec)
    .. string.format("-%03d", ms % 1000)

  return string.format("%s__%s__%d__%04d__b%d", base, wall, ms, seq, buf)
end

---@return VF.BufferState
function M.capture_state(buf, win)
  local cursor = v.nvim_win_get_cursor(win)
  ---@type VF.BufferState
  return {
    bufname = v.nvim_buf_get_name(buf),
    filetype = vim.bo[buf].filetype,
    row = cursor[1] - 1,
    col = cursor[2],
    top_row = vim.fn.line("w0"),
    bottom_row = vim.fn.line("w$"),
    window_height = v.nvim_win_get_height(win),
    scroll_amount = v.nvim_get_option_value("scroll", { win = win }),
    lines = v.nvim_buf_get_lines(buf, 0, -1, false),
  }
end

function M.check_state_inconsistencies(start_state, end_state)
  if end_state.scroll_amount ~= start_state.scroll_amount then
    vim.notify("scroll amount changed during session")
  end
  if end_state.window_height ~= start_state.window_height then
    vim.notify("window height changed during session")
  end
end

--- Compute the search region that covers cursor positions and changed lines
--- Returns (region_start, region_end) as 0-indexed line numbers
---@param start_row integer 0-indexed start cursor row
---@param end_row integer 0-indexed end cursor row
---@param start_lines string[] Buffer lines at session start
---@param end_lines string[] Buffer lines at session end
---@param padding integer Number of lines to add above/below
---@return integer region_start 0-indexed first line of region
---@return integer region_end 0-indexed last line of region
function M.compute_search_region(start_row, end_row, start_lines, end_lines, padding)
  return ffi_session.compute_search_region(
    start_lines,
    end_lines,
    start_row,
    end_row,
    padding
  )
end

return M

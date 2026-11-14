-- tests/lua/_helpers.lua
-- Shared fixtures for the Lua test suite. Filename is underscore-prefixed
-- so the runner's glob skips it.

local M = {}

--- Create a scratch buffer, fill it, make it current, return its id.
---@param lines string[]|nil  Defaults to { "x" }.
---@return integer buf
function M.new_buf(lines)
  local buf = vim.api.nvim_create_buf(true, false)
  vim.api.nvim_buf_set_lines(buf, 0, -1, false, lines or { "x" })
  vim.api.nvim_set_current_buf(buf)
  return buf
end

---@param lines string[]|nil
---@param fn fun(buf: integer)
function M.with_buf(lines, fn)
  local buf = M.new_buf(lines)
  local ok, err = pcall(fn, buf)
  if vim.api.nvim_buf_is_valid(buf) then
    pcall(vim.api.nvim_buf_delete, buf, { force = true })
  end
  if not ok then error(err, 2) end
end

---@return string tmp_dir
function M.use_temp_data_home()
  local tmp = vim.fn.tempname()
  vim.fn.mkdir(tmp, "p")
  vim.env.XDG_DATA_HOME = tmp
  return tmp
end

---@param list table
---@param value any
---@return boolean
function M.list_contains(list, value)
  for _, item in ipairs(list) do
    if item == value then return true end
  end
  return false
end

---@return string
function M.project_root()
  local this_file = debug.getinfo(1, "S").source:sub(2)
  return vim.fn.fnamemodify(this_file, ":p:h:h:h")
end

---@param rel string
---@return string
function M.data_path(rel)
  return M.project_root() .. "/data/VimficiencyFiles/" .. rel
end

local json_cache = {}

---@param rel string
---@return table
function M.read_data_json(rel)
  if json_cache[rel] then return vim.deepcopy(json_cache[rel]) end
  local path = M.data_path(rel)
  local text = table.concat(vim.fn.readfile(path), "\n")
  local decoded = vim.json.decode(text)
  json_cache[rel] = decoded
  return vim.deepcopy(decoded)
end

--- Synchronously feed keys as if typed by the user. Modes:
---   t = "handle as typed" (not from a mapping).
---   x = "execute typed-ahead synchronously" — flushes autocmds before
---       returning.
--- Termcodes (`<Esc>`, `<CR>`, `<Space>`, `<lt>`) are translated.
---
--- KNOWN QUIRK: when the fed keys leave vim in insert mode mid-sequence
--- (e.g. just the structural part of `c{motion}` without trailing
--- content + `<Esc>`), `x` mode triggers an *implicit* InsertLeave on
--- return. This doesn't happen in real interactive use, but headless
--- tests that try to observe a "now-in-Insert" intermediate state will
--- see it immediately leave again. Workaround: feed full sequences
--- ending in normal mode (`structural + typed + <Esc>`) as one unit.
---@param keys string
function M.feed(keys)
  local termkeys = vim.api.nvim_replace_termcodes(keys, true, true, true)
  vim.api.nvim_feedkeys(termkeys, "xt", false)
end

--- Like `feed`, but with the `m` flag — vim's mapping resolution runs on
--- the fed keys. Only use this when the test specifically exercises
--- mapping behavior; otherwise `feed` is the right default to avoid
--- ambient mappings affecting the test.
---@param keys string
function M.feed_with_remap(keys)
  local termkeys = vim.api.nvim_replace_termcodes(keys, true, true, true)
  vim.api.nvim_feedkeys(termkeys, "mxt", false)
end

--- Install one or more monkey-patches, run `fn`, then restore — even on
--- error. Each patch is a `{table, key, value}` triple. Errors surface
--- from `fn` after restore, with preserved traceback.
---@param patches table<integer, table>  Array of { tbl, key, value }.
---@param fn fun()
function M.with_patch(patches, fn)
  local saved = {}
  for i, p in ipairs(patches) do
    saved[i] = { tbl = p[1], key = p[2], prev = p[1][p[2]] }
    p[1][p[2]] = p[3]
  end
  local ok, err = pcall(fn)
  for i = #saved, 1, -1 do
    saved[i].tbl[saved[i].key] = saved[i].prev
  end
  if not ok then error(err, 2) end
end

--- A complete VF.Session.Result. `disk.validate` is the schema oracle, so a
--- fixture that drifts from production fails here, not in the code under test.
---@param overrides table|nil
---@return VF.Session.Result
function M.fake_result(overrides)
  local r = {
    start_row = 0, start_col = 0, end_row = 0, end_col = 0,
    lines = { "x" },
    goal_lines = { "x" },
    has_lines_above = false,
    has_lines_below = false,
    prefix = "",
    suffix = "",
    diffs = require("vimficiency.session.disk").empty_array(),
    window_height = 20,
    scroll_amount = 10,
    user_seq = "x",
    user_cost = 1.0,
    optimal_results = { { seq = "y", cost = 1.0 } },
    key_count = 1,
    finished_wall_epoch = os.time(),
    kind = "mark",
  }
  for k, val in pairs(overrides or {}) do r[k] = val end
  local err = require("vimficiency.session.disk").validate(r)
  assert(err == nil, "fake_result does not satisfy disk.validate: " .. tostring(err))
  return r
end

---@param user_seq string|nil
---@param overrides table|nil
---@return VF.Session.Result
function M.finished_result(user_seq, overrides)
  local seq = user_seq or "abc"
  return M.fake_result(vim.tbl_deep_extend("force", {
    lines = { "hello" },
    user_seq = seq,
    optimal_results = { { seq = seq, cost = 1.0 } },
    key_count = 0,
    finished_wall_epoch = 0,
  }, overrides or {}))
end

--- Config the way `setup()` would install it, so tests never hand-build the shape.
--- `raw` is the user block (nil = no block, the default-gate shape); `patch` is
--- applied on top of the normalized result.
---@param raw table|nil
---@param patch table|nil
---@return table|false
function M.auto_suggest_config(raw, patch)
  local config = require("vimficiency.config")
  local validate = require("vimficiency.config.validate")
  local cfg = validate.normalize_auto_suggest(raw, config._defaults.auto_suggest)
  for k, val in pairs(patch or {}) do cfg[k] = val end
  return cfg
end

---@param raw table
---@return table|false
function M.watch_config(raw)
  return require("vimficiency.config.validate").normalize_watch(raw)
end

---@param name string
---@param overrides table|nil
---@return table
function M.result_scenario(name, overrides)
  local scenarios = M.read_data_json("lua_test_scenarios.json")
  local scenario = scenarios[name]
  if not scenario then error("unknown result scenario: " .. name, 2) end
  return M.fake_result(vim.tbl_deep_extend("force", scenario, overrides or {}))
end

--- Run `fn` with `vim.notify` and `util.show_output` stubbed to no-op.
---@param fn fun()
function M.silence_notify(fn)
  local util = require("vimficiency.util")
  M.with_patch({
    { vim, "notify", function(...) end },
    { util, "show_output", function(...) end },
  }, fn)
end

--- Seed a recall record in the store. `backdate_s` backdates
--- `started_mono_ns` so `Ns` queries can resolve the record deterministically.
---@param opts { buf?: integer, lines?: string[], id?: string, backdate_s?: number }|nil
---@return table rec, integer buf, string id
function M.seed_recall(opts)
  opts = opts or {}
  local session_store = require("vimficiency.session.store")
  local util = require("vimficiency.util")
  local buf = opts.buf or M.new_buf(opts.lines or { "x" })
  local win = vim.api.nvim_get_current_win()
  local id = opts.id or util.new_id(buf)
  vim.api.nvim_win_set_buf(win, buf)
  local rec = session_store.new_active_session(
    id, nil, win, buf, util.capture_state(buf, win), "recall")
  if opts.backdate_s then
    rec.started_mono_ns = vim.uv.hrtime() - opts.backdate_s * 1e9
  end
  session_store.store_recall(rec)
  return rec, buf, id
end

--- Seed a manual (mark) session under `alias` on the current buffer/window.
---@param alias string
---@param id string
---@return table rec
function M.seed_named(alias, id)
  local session_store = require("vimficiency.session.store")
  local util = require("vimficiency.util")
  local buf = vim.api.nvim_get_current_buf()
  local win = vim.api.nvim_get_current_win()
  local rec = session_store.new_active_session(
    id, nil, win, buf, util.capture_state(buf, win), "mark")
  session_store.store_named(alias, rec)
  return rec
end

return M

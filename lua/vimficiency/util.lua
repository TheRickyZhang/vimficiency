---@class (exact) VF.Util
---@field entry                      fun(fn: function): function
---@field basename                   fun(path: string): string
---@field find_plugin_root           fun(): string
---@field new_id                     fun(buf: integer): string
local M = {}

local v = vim.api
local uv = vim.uv
local fs = vim.fs

M.basename = fs.basename or function(p)
  return p:match("([^/\\]+)$") or p
end

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

return M

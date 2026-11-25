-- Item navigation for a rendered buffer. The view lays out items, blocks of
-- its text that can be acted on; one is always current, highlighted whole
-- with the cursor cell hidden. h/j/k/l move to the neighbouring item that
-- way, <CR> and <Esc> act on the current one. The view owns the items and
-- what the keys do; any other cursor motion lands on the nearest item.

---@class (exact) VF.Ui.ItemNavModule
---@field attach fun(buf: integer, opts: VF.Ui.ItemNavOpts): VF.Ui.ItemNav
local M = {}

local v = vim.api
local highlights = require("vimficiency.ui.highlights")
local ui_windows = require("vimficiency.ui.windows")

local ns = v.nvim_create_namespace("vimfy_item_nav")

--- Rows are 0-indexed. A view's items extend this with what they act on.
---@class VF.Ui.NavItem
---@field id string          # the same across redraws
---@field row integer        # first line
---@field last_row integer   # inclusive
---@field left? integer      # display columns [left, right); nil: the whole width
---@field right? integer
---@field group? string      # a move stays among the items sharing it; `leave` takes one out
---@field cursor { [1]: integer, [2]: integer }   # row and byte column the hidden cursor rests at
---@field spans? { [1]: integer, [2]: integer, [3]: integer }[]   # row and byte columns [begin, end) to highlight; nil: its lines, across the window

---@class VF.Ui.ItemNavOpts
---@field win fun(): integer
---@field items fun(): VF.Ui.NavItem[]   # where several tie for a move, the first wins
---@field enter? fun(item: VF.Ui.NavItem)
---@field escape? fun(item: VF.Ui.NavItem)
---@field focused? fun(item: VF.Ui.NavItem)   # the current item changed, or was redrawn
---@field adjust? fun(item: VF.Ui.NavItem, offset: integer): boolean   # consumes a horizontal move
---@field leave? fun(item: VF.Ui.NavItem, key: "h"|"j"|"k"|"l"): string|nil   # the id a move with nothing that way in the item's group goes to

---@class VF.Ui.ItemNav
---@field keymaps VF.Keymap[]
---@field current fun(): VF.Ui.NavItem|nil
---@field focus fun(id: string)
---@field sync fun()   # after the view redraws: keeps the current item, by id, else the nearest

---@param item VF.Ui.NavItem
---@return integer left
---@return integer right
local function extent(item)
  return item.left or 0, item.right or math.huge
end

--- How far `item` lies from `from` in direction `key`, as the rank a move
--- compares (smaller is nearer), or nil when it is not that way.
---@param from VF.Ui.NavItem
---@param item VF.Ui.NavItem
---@param key "h"|"j"|"k"|"l"
---@return integer|nil along
---@return integer|nil across
local function distance(from, item, key)
  if item.group ~= from.group then return nil end
  local from_left, from_right = extent(from)
  local left, right = extent(item)
  if key == "h" or key == "l" then
    if item.row > from.last_row or item.last_row < from.row then return nil end
    if key == "l" and left >= from_right then return left - from_right, 0 end
    if key == "h" and right <= from_left then return from_left - right, 0 end
    return nil
  end
  local across = math.max(0, left - from_right, from_left - right)
  if key == "j" and item.row > from.last_row then return item.row - from.last_row, across end
  if key == "k" and item.last_row < from.row then return from.row - item.last_row, across end
  return nil
end

---@param buf integer
---@param opts VF.Ui.ItemNavOpts
---@return VF.Ui.ItemNav
function M.attach(buf, opts)
  ---@type string|nil
  local current_id

  ---@return VF.Ui.NavItem|nil
  local function current()
    for _, item in ipairs(opts.items()) do
      if item.id == current_id then return item end
    end
  end

  ---@param item VF.Ui.NavItem
  local function set_current(item)
    current_id = item.id
    v.nvim_win_set_cursor(opts.win(), { item.cursor[1] + 1, item.cursor[2] })
    v.nvim_buf_clear_namespace(buf, ns, 0, -1)
    for _, span in ipairs(item.spans or {}) do
      v.nvim_buf_set_extmark(buf, ns, span[1], span[2], {
        end_col = span[3], hl_group = highlights.NAV_CURRENT, priority = 200,
      })
    end
    if not item.spans then
      -- To the window's edge; the last line has no following row to end at.
      v.nvim_buf_set_extmark(buf, ns, item.row, 0, {
        end_row = item.last_row + 1, end_col = 0, strict = false,
        hl_group = highlights.NAV_CURRENT, hl_eol = true, priority = 200,
      })
    end
    if opts.focused then opts.focused(item) end
  end

  --- The item the cursor is in, else the one nearest its row.
  ---@return VF.Ui.NavItem|nil
  local function item_at_cursor()
    local win = opts.win()
    local row = v.nvim_win_get_cursor(win)[1] - 1
    local col = v.nvim_win_call(win, function() return vim.fn.virtcol(".") end) - 1
    local nearest, nearest_rows
    for _, item in ipairs(opts.items()) do
      local left, right = extent(item)
      local rows = math.max(0, item.row - row, row - item.last_row)
      if rows == 0 and col >= left and col < right then return item end
      if not nearest or rows < nearest_rows then nearest, nearest_rows = item, rows end
    end
    return nearest
  end

  local function sync()
    local item = current() or item_at_cursor()
    if item then
      set_current(item)
    else
      current_id = nil
      v.nvim_buf_clear_namespace(buf, ns, 0, -1)
    end
  end

  --- The current item, once whatever moved the cursor since has been followed.
  ---@return VF.Ui.NavItem|nil
  local function follow_cursor()
    local item = item_at_cursor()
    if item and item.id ~= current_id then set_current(item) end
    return item
  end

  ---@param key "h"|"j"|"k"|"l"
  local function move(key)
    local item = follow_cursor()
    if not item then return end
    local count = vim.v.count1
    if opts.adjust and (key == "h" or key == "l") then
      if opts.adjust(item, key == "h" and -count or count) then return end
    end
    for _ = 1, count do
      local best, best_along, best_across
      for _, other in ipairs(opts.items()) do
        local along, across = distance(item, other, key)
        if along and (not best or along < best_along or (along == best_along and across < best_across)) then
          best, best_along, best_across = other, along, across
        end
      end
      if not best and opts.leave then
        local id = opts.leave(item, key)
        for _, other in ipairs(opts.items()) do
          if other.id == id then best = other end
        end
      end
      if not best then break end
      item = best
    end
    if item.id ~= current_id then set_current(item) end
  end

  ---@param act? fun(item: VF.Ui.NavItem)
  ---@return fun()
  local function on_current(act)
    return function()
      local item = follow_cursor()
      if item and act then act(item) end
    end
  end

  local left = opts.adjust and "Move left or select the previous option" or "The item to the left"
  local right = opts.adjust and "Move right or select the next option" or "The item to the right"
  ---@type VF.Keymap[]
  local keymaps = {
    { lhs = "h", desc = left, handler = function() move("h") end },
    { lhs = "j", desc = "The item below", handler = function() move("j") end },
    { lhs = "k", desc = "The item above", handler = function() move("k") end },
    { lhs = "l", desc = right, handler = function() move("l") end },
    { lhs = "<Left>", desc = left, handler = function() move("h") end },
    { lhs = "<Down>", desc = "The item below", handler = function() move("j") end },
    { lhs = "<Up>", desc = "The item above", handler = function() move("k") end },
    { lhs = "<Right>", desc = right, handler = function() move("l") end },
  }
  if opts.enter then
    keymaps[#keymaps + 1] = { lhs = "<CR>", desc = "Open the current item", handler = on_current(opts.enter) }
  end
  if opts.escape then
    keymaps[#keymaps + 1] = { lhs = "<Esc>", desc = "Back out of the current item", handler = on_current(opts.escape) }
  end

  -- The cursor is hidden, so wherever another motion or the mouse puts it,
  -- the item there becomes current. set_current's own move lands inside the
  -- item it set, which ends the round.
  v.nvim_create_autocmd("CursorMoved", {
    buffer = buf,
    callback = function() follow_cursor() end,
    desc = "vimfy: the item under the cursor becomes current",
  })
  ui_windows.hide_cursor_while_active(opts.win(), buf)

  return {
    keymaps = keymaps,
    current = follow_cursor,
    focus = function(id)
      current_id = id
      sync()
    end,
    sync = sync,
  }
end

return M

-- Neovim's key names are a fixed table plus modifier prefixes. The fixture
-- records all of them with whether the effort model prices each
-- (`SequenceToKeysTest.EveryNeovimKeyNameIsClassified`), so a name Neovim adds
-- fails here instead of going unpriced unseen.

local FIXTURE = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":h:h")
  .. "/Unit/Misc/fixtures/neovim_key_names.tsv"

local K_SPECIAL, KS_MODIFIER = "\128", 252

---@return table<string, true>
local function neovim_key_names()
  local names = {}
  for byte = 1, 127 do
    local shown = vim.fn.keytrans(string.char(byte))
    if #shown > 1 then names[shown] = true end
  end
  for first = 1, 255 do
    if first ~= KS_MODIFIER then
      for second = 1, 255 do
        local shown = vim.fn.keytrans(K_SPECIAL .. string.char(first, second))
        -- Skips raw terminal codes (`<t_xx>`) and single characters in brackets.
        if shown:match("^<%a[%w%-]+>$") then names[shown] = true end
      end
    end
  end
  for mask = 1, 255 do
    local shown = vim.fn.keytrans(K_SPECIAL .. string.char(KS_MODIFIER, mask) .. "x")
    if shown:match("^<.+%-x>$") then names[shown] = true end
  end
  return names
end

test("key names: Neovim writes exactly the names the effort fixture classifies", function()
  local recorded = {}
  for _, line in ipairs(vim.fn.readfile(FIXTURE)) do
    recorded[vim.split(line, "\t")[1]] = true
  end
  local current = neovim_key_names()
  local unrecorded, gone = {}, {}
  for name in pairs(current) do
    if not recorded[name] then unrecorded[#unrecorded + 1] = name end
  end
  for name in pairs(recorded) do
    if not current[name] then gone[#gone + 1] = name end
  end
  table.sort(unrecorded)
  table.sort(gone)
  assert_true(#unrecorded == 0 and #gone == 0, string.format(
    "add to %s: %s; remove: %s", FIXTURE, table.concat(unrecorded, " "), table.concat(gone, " ")))
end)

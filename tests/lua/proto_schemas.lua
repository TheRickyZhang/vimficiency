local ffi_core = require("vimficiency.ffi.core")
local ffi_versions = require("vimficiency.ffi.versions")

-- lua-protobuf keeps its loaded schema in the Lua registry, so this second
-- module table sees the descriptor ffi.core loaded.
local pb = assert(package.loadlib(ffi_core.lib_path, "luaopen_pb"))()

---@param name string
---@return string
local function bare(name)
  return (name:gsub("^%.", ""))
end

--- Every message and enum reachable from `root`, in full-name order, with
--- fields and values by number; names, numbers, types, labels and oneofs only.
---@param root string
---@return string
local function canonical_schema(root)
  local blocks, pending = {}, { root }
  while #pending > 0 do
    local name = table.remove(pending)
    if not blocks[name] then
      local _, _, kind = pb.type(name)
      local entries = {}
      for field, number, field_type, _, label, oneof in pb.fields(name) do
        if kind == "enum" then
          entries[#entries + 1] = { number, string.format("%d %s", number, field) }
        else
          entries[#entries + 1] = { number, string.format("%d %s %s %s%s",
            number, field, bare(field_type), label, oneof and " " .. oneof or "") }
          if pb.type(field_type) then
            pending[#pending + 1] = bare(field_type)
          end
        end
      end
      table.sort(entries, function(a, b)
        if a[1] ~= b[1] then return a[1] < b[1] end
        return a[2] < b[2]
      end)
      local lines = { kind .. " " .. name }
      for _, entry in ipairs(entries) do
        lines[#lines + 1] = "  " .. entry[2]
      end
      blocks[name] = table.concat(lines, "\n")
    end
  end
  local names = vim.tbl_keys(blocks)
  table.sort(names)
  local text = {}
  for _, name in ipairs(names) do
    text[#text + 1] = blocks[name]
  end
  return table.concat(text, "\n")
end

local FORMATS = {
  { key = "journal", root = "vf.journal.File" },
  { key = "samples", root = "vf.journal.SampleFile" },
  { key = "rollup", root = "vf.journal.RollupFile" },
  { key = "session_log", root = "vf.stats.SessionLog" },
}

for _, format in ipairs(FORMATS) do
  test("proto schema: " .. format.root .. " matches the recorded " .. format.key .. " signature", function()
    local computed = vim.fn.sha256(canonical_schema(format.root)):sub(1, 16)
    local recorded = ffi_versions.versions()[format.key .. "_schema"]
    local format_const = format.key:upper() .. "_FORMAT"
    local schema_const = format.key:upper() .. "_SCHEMA"
    assert_true(computed == recorded, string.format(
      "%s schema (%s) signature is %s, but %s records %q: a field changed meaning " ..
      "or an incompatible change → bump %s in src/Versions.cppm; a purely additive " ..
      "field → only update %s",
      format.key, format.root, computed, schema_const, recorded, format_const, schema_const))
  end)
end

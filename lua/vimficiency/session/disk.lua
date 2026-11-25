local alias_mod = require("vimficiency.session.alias")

local M = {}

local EMPTY_ARRAY_MT = { __jsontype = "array" }

function M.empty_array()
  return setmetatable({}, EMPTY_ARRAY_MT)
end

function M.save_dir()
  return vim.fn.stdpath("data") .. "/vimficiency/saved"
end

---@param value any
---@param level integer|nil
---@return string
local function encode_pretty(value, level)
  if type(value) ~= "table" then
    return vim.json.encode(value)
  end
  level = level or 0
  local pad = string.rep("  ", level)
  local inner = string.rep("  ", level + 1)
  if next(value) == nil then
    local mt = getmetatable(value)
    if mt and mt.__jsontype == "array" then return "[]" end
    if not vim.islist(value) then return "{}" end
    return "[]"
  end
  if vim.islist(value) then
    local parts = {}
    for _, item in ipairs(value) do
      parts[#parts + 1] = inner .. encode_pretty(item, level + 1)
    end
    return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
  end
  local keys = {}
  for k in pairs(value) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for _, k in ipairs(keys) do
    parts[#parts + 1] = inner
      .. vim.json.encode(tostring(k))
      .. ": "
      .. encode_pretty(value[k], level + 1)
  end
  return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
end

M.FORMAT_VERSION = 2

---@param filename string
---@param result VF.Session.Result
---@return string path
function M.save(filename, result)
  local save_dir = M.save_dir()
  vim.fn.mkdir(save_dir, "p")
  local path = save_dir .. "/" .. filename .. ".json"
  local versioned = vim.tbl_extend("error", result, { version = result.recording and M.FORMAT_VERSION or 1 })
  local lines = vim.split(encode_pretty(versioned), "\n")
  local ok, err = pcall(vim.fn.writefile, lines, path)
  if not ok then error(tostring(err), 0) end
  return path
end

local REQUIRED_FIELDS = {
  { key = "lines",               kind = "table" },
  { key = "goal_lines",          kind = "table" },
  { key = "start_row",           kind = "number" },
  { key = "start_col",           kind = "number" },
  { key = "end_row",             kind = "number" },
  { key = "end_col",             kind = "number" },
  { key = "has_lines_above",     kind = "boolean" },
  { key = "has_lines_below",     kind = "boolean" },
  { key = "prefix",              kind = "string" },
  { key = "suffix",              kind = "string" },
  { key = "diffs",               kind = "table" },
  { key = "window_height",       kind = "number" },
  { key = "scroll_amount",       kind = "number" },
  { key = "user_seq",            kind = "string" },
  { key = "user_cost",           kind = "number" },
  { key = "optimal_results",     kind = "table" },
  { key = "key_count",           kind = "number" },
  { key = "finished_wall_epoch", kind = "number" },
  { key = "kind",                kind = "string" },
}

local OPTIONAL_FIELDS = {
  { key = "indent", kind = "table" },
  { key = "commands",      kind = "table" },
  { key = "user_typed",    kind = "string" },
  { key = "user_typed_insert", kind = "string" },
  { key = "analyze_ms",    kind = "number" },
  { key = "optimizer_overrides", kind = "string" },
  { key = "results_calculated",  kind = "number" },
}

local function index(n)
  return type(n) == "number" and n >= 0 and n % 1 == 0
end

local function valid_lines(lines)
  if type(lines) ~= "table" or not vim.islist(lines) then return false end
  for _, line in ipairs(lines) do
    if type(line) ~= "string" or line:find("\n", 1, true) then return false end
  end
  return true
end

local function valid_position(frame, height)
  return index(frame.row) and frame.row < height and index(frame.col) and type(frame.mode) == "string"
end

local function valid_recording(recording, height)
  if type(recording) ~= "table"
      or type(recording.steps) ~= "table" or not vim.islist(recording.steps) then return false end
  if not valid_position(recording, height) then return false end
  for _, step in ipairs(recording.steps) do
    if type(step) ~= "table" or type(step.keys) ~= "string"
        or type(step.deltas) ~= "table" or not vim.islist(step.deltas) then return false end
    for _, delta in ipairs(step.deltas) do
      if type(delta) ~= "table" or not index(delta.first) or not index(delta.old_count)
          or delta.first + delta.old_count > height or not valid_lines(delta.lines) then return false end
      height = height + #delta.lines - delta.old_count
    end
    if not valid_position(step, height) then return false end
  end
  return true
end

---@param data any
---@return string|nil err
function M.validate(data)
  if type(data) ~= "table" then
    return "top-level must be a JSON object"
  end
  for _, f in ipairs(REQUIRED_FIELDS) do
    if type(data[f.key]) ~= f.kind then
      return string.format("missing or non-%s '%s' field", f.kind, f.key)
    end
  end
  for _, f in ipairs(OPTIONAL_FIELDS) do
    if data[f.key] ~= nil and type(data[f.key]) ~= f.kind then
      return string.format("non-%s '%s' field", f.kind, f.key)
    end
  end
  local indent = data.indent
  if indent and (type(indent.autoindent) ~= "boolean" or type(indent.expandtab) ~= "boolean"
      or type(indent.copyindent) ~= "boolean" or not index(indent.shiftwidth)
      or not index(indent.tabstop) or indent.tabstop < 1 or indent.tabstop > 9999) then
    return "invalid 'indent' field"
  end
  if #data.lines == 0 then
    return "empty 'lines' field"
  end
  if data.recording ~= nil and not valid_recording(data.recording, #data.lines) then
    return "invalid 'recording' field"
  end
  for i, r in ipairs(data.optimal_results) do
    if type(r) ~= "table" then
      return string.format("optimal_results[%d] is not an object", i)
    end
    if type(r.seq) ~= "string" then
      return string.format("optimal_results[%d] missing or non-string 'seq'", i)
    end
    if type(r.cost) ~= "number" then
      return string.format("optimal_results[%d] missing or non-numeric 'cost'", i)
    end
  end
  return nil
end

---@param json_str string
---@return VF.Session.Result|nil data
---@return string|nil error
function M.decode(json_str)
  local ok, data = pcall(vim.json.decode, json_str)
  if not ok then
    return nil, "Failed to parse JSON: " .. tostring(data)
  end
  if type(data) ~= "table" then
    return nil, "Malformed result file: top-level must be a JSON object"
  end
  if data.version ~= M.FORMAT_VERSION and data.version ~= 1 then
    return nil, string.format(
      "Unsupported result file format version %s (supported versions are 1 and %d)",
      data.version == nil and "<none>" or tostring(data.version), M.FORMAT_VERSION)
  end
  if data.version == 1 then
    data.recording = nil
  elseif data.recording == nil then
    return nil, "Malformed result file: missing 'recording' field"
  end
  data.version = nil
  local schema_err = M.validate(data)
  if schema_err then
    return nil, "Malformed result file: " .. schema_err
  end
  return data, nil
end

---@param filename string
---@return VF.Session.Result|nil data
---@return string|nil error
---@return boolean is_missing
function M.load(filename)
  local path = M.save_dir() .. "/" .. filename .. ".json"
  if vim.fn.filereadable(path) == 0 then
    return nil, "File not found: " .. path, true
  end
  local data, err = M.decode(table.concat(vim.fn.readfile(path), "\n"))
  return data, err, false
end

---@param filename string
---@param result VF.Session.Result
---@return string path
function M.write_with_overwrite_warn(filename, result)
  local dest_path = M.save_dir() .. "/" .. filename .. ".json"
  local existed = vim.fn.filereadable(dest_path) == 1
  local path = M.save(filename, result)
  if existed then
    vim.notify("vimfy: overwrote existing saved result [" .. filename .. "]",
      vim.log.levels.WARN)
  end
  return path
end

---@return string[]
function M.list()
  local save_dir = M.save_dir()
  if vim.fn.isdirectory(save_dir) == 0 then
    return {}
  end
  local files = vim.fn.glob(save_dir .. "/*.json", false, true)
  local filenames = {}
  for _, path in ipairs(files) do
    table.insert(filenames, vim.fn.fnamemodify(path, ":t:r"))
  end
  return filenames
end

---@param filename string
---@return string path
function M.remove(filename)
  local path = M.save_dir() .. "/" .. filename .. ".json"
  if vim.fn.filereadable(path) == 0 then
    error("No saved result '" .. filename .. "' at " .. path, 0)
  end
  if vim.fn.delete(path) ~= 0 then
    error("vimfy rm failed for " .. path, 0)
  end
  return path
end

---@param old_filename string
---@param new_filename string
function M.rename(old_filename, new_filename)
  if not alias_mod.is_valid_filename(old_filename) then
    error("Invalid source filename '" .. tostring(old_filename) .. "'.", 0)
  end
  if not alias_mod.is_valid_filename(new_filename) then
    error("Invalid target filename '" .. tostring(new_filename) .. "'.", 0)
  end
  if old_filename == new_filename then
    error("Source and target filenames are identical.", 0)
  end
  local src = M.save_dir() .. "/" .. old_filename .. ".json"
  local dst = M.save_dir() .. "/" .. new_filename .. ".json"
  if vim.fn.filereadable(src) == 0 then
    error("No saved result '" .. old_filename .. "' at " .. src, 0)
  end
  if vim.fn.filereadable(dst) == 1 then
    error("Target '" .. new_filename .. "' already exists.", 0)
  end
  local ok, err = vim.uv.fs_rename(src, dst)
  if not ok then
    error("rename failed: " .. tostring(err), 0)
  end

end

---@param src_filename string
---@param dst_filename string
function M.duplicate(src_filename, dst_filename)
  if not alias_mod.is_valid_filename(src_filename) then
    error("Invalid source filename '" .. tostring(src_filename) .. "'.", 0)
  end
  if not alias_mod.is_valid_filename(dst_filename) then
    error("Invalid target filename '" .. tostring(dst_filename) .. "'.", 0)
  end
  if src_filename == dst_filename then
    error("Source and target filenames are identical.", 0)
  end
  local src = M.save_dir() .. "/" .. src_filename .. ".json"
  local dst = M.save_dir() .. "/" .. dst_filename .. ".json"
  if vim.fn.filereadable(src) == 0 then
    error("No saved result '" .. src_filename .. "' at " .. src, 0)
  end
  if vim.fn.filereadable(dst) == 1 then
    error("Target '" .. dst_filename .. "' already exists.", 0)
  end
  local ok, err = vim.uv.fs_copyfile(src, dst, nil)
  if not ok then
    error("copy failed: " .. tostring(err), 0)
  end

end

return M

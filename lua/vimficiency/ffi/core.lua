local ffi = require("ffi")

local M = {}

local function find_plugin_root()
  local source = debug.getinfo(1, "S").source
  if source:sub(1, 1) == "@" then
    source = source:sub(2)
  end
  return vim.fn.fnamemodify(source, ":h:h:h:h")
end

local function read_ffi_api_def()
  local path = find_plugin_root() .. "/src/LuaExports/Api.def"
  local f, err = io.open(path, "rb")
  if not f then
    error("vimfy: failed to read FFI API declarations at " .. path .. ": " .. tostring(err), 0)
  end
  local text = f:read("*all")
  f:close()
  return text
end

local api_def_text = read_ffi_api_def()
ffi.cdef(api_def_text)

---@return string
local function platform_lib_filename()
  local os_name = jit and jit.os or ""
  if os_name == "OSX" then
    return "libvimficiency.dylib"
  elseif os_name == "Windows" then
    return "vimficiency.dll"
  else
    return "libvimficiency.so"
  end
end

M.lib_filename = platform_lib_filename()

local function load_lib()
  local root = find_plugin_root()
  local lib_filename = M.lib_filename
  local paths = {}
  if type(vim.env.VIMFICIENCY_LIB_PATH) == "string"
      and vim.env.VIMFICIENCY_LIB_PATH ~= "" then
    table.insert(paths, vim.env.VIMFICIENCY_LIB_PATH)
  end
  if type(_G.__vimficiency_reload_lib_path) == "string" then
    table.insert(paths, _G.__vimficiency_reload_lib_path)
  end
  table.insert(paths, root .. "/build/" .. lib_filename)
  -- The bare filename, not "vimficiency": `package.loadlib` reuses this path
  -- and, unlike `ffi.load`, does not add the lib prefix or the extension.
  table.insert(paths, lib_filename)

  local expected_hash = vim.fn.sha256(api_def_text)

  for _, path in ipairs(paths) do
    local ok, lib = pcall(ffi.load, path)
    if ok then
      local got_hash = ffi.string(lib.vf_abi_hash())
      if got_hash ~= expected_hash then
        error(string.format(
          "vimficiency: ABI mismatch — loaded %s reports Api.def hash %s, " ..
          "but the Lua bindings were authored against %s. The shared library " ..
          "is stale. Rebuild with `cmake --build %s/build` (or `:Lazy build " ..
          "vimficiency` / your plugin manager's equivalent).",
          path, got_hash, expected_hash, root), 0)
      end
      M.lib_path = path
      return lib
    end
  end

  error(string.format(
    "vimficiency: could not load %s (tried: %s). Build it with " ..
    "`./scripts/install.sh` in the plugin directory, or `:Lazy build " ..
    "vimficiency` / your plugin manager's equivalent.",
    lib_filename, table.concat(paths, ", ")), 0)
end

M.lib = load_lib()

-- lua-protobuf is compiled into the library, and so is the schema it encodes
-- and decodes with, so neither can drift from the C++ on the other side.
local function load_pb()
  local pb = assert(package.loadlib(M.lib_path, "luaopen_pb"))()
  local descriptor = M.lib.vf_proto_descriptor()
  assert(pb.load(ffi.string(descriptor.data, tonumber(descriptor.len))))
  pb.option("decode_default_array")
  return pb
end

local pb = load_pb()

-- `pb.encode` silently drops a key the message does not have, so a misspelled
-- field would reach C++ as its default.
local function check_fields(root, message, value, path)
  for key, item in pairs(value) do
    local name, _, field_type, _, label = pb.field(message, tostring(key))
    if not name then
      error(string.format("vimfy: unknown field '%s%s' in %s", path, tostring(key), root), 0)
    end
    local _, _, kind = pb.type(field_type)
    if kind == "message" and type(item) == "table" then
      if label == "repeated" then
        for i, element in ipairs(item) do
          check_fields(root, field_type, element, string.format("%s%s[%d].", path, key, i))
        end
      else
        check_fields(root, field_type, item, path .. key .. ".")
      end
    end
  end
end

---@param message string fully qualified, e.g. "vf.search.AnalyzeRequest"
---@param name string
---@return boolean
function M.has_field(message, name)
  return pb.field(message, name) ~= nil
end

--- Raises on a field the message does not have or a value of the wrong type.
---@param message string
---@param value table
---@return string
function M.encode(message, value)
  check_fields(message, message, value, "")
  return pb.encode(message, value)
end

--- Decodes a `vf.Envelope` (C++ `storeResult` / `storePending`) and the
--- `message` inside it. Returns the C++ error instead of raising it, and
--- nothing while an async job is still pending.
---@param slice VF.C.ByteSlice
---@param message string
---@return table|nil value
---@return string|nil err
function M.decode(slice, message)
  local e = assert(pb.decode("vf.Envelope", ffi.string(slice.data, tonumber(slice.len))))
  if e.result == "error" then return nil, e.error end
  if e.result == "pending" then return nil end
  return assert(pb.decode(message, e.value))
end

--- `decode` that raises the C++ error.
---@param slice VF.C.ByteSlice
---@param message string
---@return any
function M.unwrap(slice, message)
  local value, err = M.decode(slice, message)
  if err then error(err, 0) end
  return value
end

--- Calls an export that takes one serialized `request_message`.
---@param fn fun(request: string, request_len: integer): VF.C.ByteSlice
---@param request_message string
---@param request table
---@param result_message string
---@return any
function M.call(fn, request_message, request, result_message)
  local bytes = M.encode(request_message, request)
  return M.unwrap(fn(bytes, #bytes), result_message)
end

local function enum_table(names)
  local t = {}
  for i, name in ipairs(names) do
    t[name] = i - 1
  end
  return t
end

local enums = M.unwrap(M.lib.vf_enums(), "vf.config.Enums")
M.Key = enum_table(enums.keys)
M.Finger = enum_table(enums.fingers)
M.Hand = enum_table(enums.hands)
M.CountClass = enum_table(enums.count_classes)

function M.version_string()
  return ffi.string(M.lib.vf_version_string())
end

function M.debug_config()
  return M.unwrap(M.lib.vf_debug_config(), "vf.Text").text
end

return M

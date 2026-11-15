-- Protobuf viability spike. Decodes the same Explore state through msgpack and
-- through protobuf (pb.c compiled into libvimficiency) and compares them.
--
--   VIMFICIENCY_LIB_PATH=$PWD/build-spike/libvimficiency.so \
--     nvim --headless -u NONE -l spike/protobuf/spike.lua build-spike/generated/proto/explore.desc

local root = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h:h:h")
vim.opt.rtp:prepend(root)

local ffi = require("ffi")
local core = require("vimficiency.ffi.core")
local explore = require("vimficiency.ffi.explore")

ffi.cdef([[
VFByteSlice vf_spike_explore_state_pb(int view_id);
VFByteSlice vf_spike_apply_result_pb(bool rejected);
]])

local function bytes(slice)
  return ffi.string(slice.data, tonumber(slice.len))
end

local function show(label, value)
  print(label .. ": " .. vim.inspect(value, { newline = " ", indent = "" }))
end

-- 1. The native Lua module, opened from the plugin's own shared library.
local open, load_err = package.loadlib(core.lib_path, "luaopen_pb")
assert(open, "package.loadlib failed: " .. tostring(load_err))
local pb = open()
print("pb module loaded from " .. core.lib_path)

local desc = assert(io.open(arg[1], "rb")):read("*a")
assert(pb.load(desc))

-- 2. A real Explore view.
local job = explore.explore_start_async({
  initial_lines = { "foo bar baz" },
  goal_lines = { "foo qux baz" },
  start_row = 0, start_col = 0,
  end_row = 0, end_col = 6,
  has_lines_above = false, has_lines_below = false,
  window_height = 40, scroll_amount = 20,
  user_seq = "wcwqux<Esc>",
  optimizer_overrides = "",
}, 5000)
local view
assert(vim.wait(10000, function()
  view = explore.explore_poll(job)
  return view ~= nil
end, 5), "explore job did not finish")

-- 3. Same state, both encodings.
local mp_bytes = bytes(core.lib.vf_explore_state(view))
local pb_bytes = bytes(core.lib.vf_spike_explore_state_pb(view))
local via_mp = vim.mpack.decode(mp_bytes).value

for _, opt in ipairs({ "auto_default_values", "use_default_values", "no_default_values" }) do
  pb.option(opt)
  local via_pb = pb.decode("vf.spike.State", pb_bytes)
  if opt == "auto_default_values" then
    assert(vim.deep_equal(via_mp, via_pb), "protobuf decode differs from msgpack")
  end
  print(("option %-20s deep_equal(msgpack, protobuf) = %s"):format(opt, tostring(vim.deep_equal(via_mp, via_pb))))
  show("  protobuf", via_pb)
end
show("  msgpack ", via_mp)
print(("wire size: msgpack %d bytes (with envelope), protobuf %d bytes"):format(#mp_bytes, #pb_bytes))

-- 4. oneof.
pb.option("auto_default_values")
show("oneof applied ", pb.decode("vf.spike.ApplyResult", bytes(core.lib.vf_spike_apply_result_pb(false))))
show("oneof rejected", pb.decode("vf.spike.ApplyResult", bytes(core.lib.vf_spike_apply_result_pb(true))))

-- 5. Lua-side mistakes on encode.
local function try_encode(label, value)
  local ok, res = pcall(pb.encode, "vf.spike.Position", value)
  print(("encode %-22s -> %s"):format(label, ok and ("ok, " .. #res .. " bytes") or ("error: " .. tostring(res))))
end
try_encode("valid", { row = 3, col = 4 })
try_encode("misspelled key", { row = 3, colx = 4 })
try_encode("wrong type", { row = "three", col = 4 })
try_encode("missing field", { row = 3 })

-- 6. Decode cost.
local N = 200000
local t0 = vim.uv.hrtime()
for _ = 1, N do vim.mpack.decode(mp_bytes) end
local t1 = vim.uv.hrtime()
for _ = 1, N do pb.decode("vf.spike.State", pb_bytes) end
local t2 = vim.uv.hrtime()
print(("decode: msgpack %.2f us, protobuf %.2f us per call"):format((t1 - t0) / N / 1000, (t2 - t1) / N / 1000))

explore.explore_destroy(view)

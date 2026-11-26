-- tests/lua/session/workspace_storage.lua
-- The three places a finished result lives: the workspace (by alias), the saved
-- sessions (written at finish, under a generated filename) and the pins (copies
-- under a filename the user chose). Covers session/store.lua's save at finish,
-- session/disk.lua's two shelves and session/archive.lua's pin / unpin / rm
-- and its alias-or-filename resolution.

local h = require("_helpers")
local archive = require("vimficiency.session.archive")
local config = require("vimficiency.config")
local disk = require("vimficiency.session.disk")
local session_store = require("vimficiency.session.store")

local EPOCH = os.time({ year = 2026, month = 11, day = 26, hour = 3, min = 14, sec = 1 })
local STAMP = "20261126-031401"

local seeded = 0

--- Finishes a mark session under `alias` at EPOCH.
---@param alias string
---@param user_seq string|nil
---@param overrides table|nil
---@return string id
local function finish(alias, user_seq, overrides)
  seeded = seeded + 1
  local id = "test-storage-" .. seeded
  h.seed_named(alias, id)
  session_store.convert_to_finished(id, h.finished_result(user_seq,
    vim.tbl_extend("force", { finished_wall_epoch = EPOCH }, overrides or {})))
  return id
end

---@param fn fun(data_home: string)
local function in_temp_home(fn)
  local data_home = h.use_temp_data_home()
  h.silence_notify(function() fn(data_home) end)
end

test("storage: a finished session is saved under its alias and finish time", function()
  in_temp_home(function()
    local id = finish("stora", "dw")
    local filename = "stora-" .. STAMP
    assert_eq(session_store.get_saved_filename("stora"), filename)
    assert_eq(disk.list("sessions"), { filename })
    assert_eq(assert(disk.load("sessions", filename)).user_seq, "dw")
    assert_eq(disk.list("pinned"), {}, "saving pins nothing")
    session_store.remove(id)
  end)
end)

test("storage: sessions finished in the same second take the next free filename", function()
  in_temp_home(function()
    finish("storb", "first")
    finish("storb", "second")
    local id = finish("storb", "third")
    assert_eq(session_store.get_saved_filename("storb"), "storb-" .. STAMP .. "-3")
    assert_eq(assert(disk.load("sessions", "storb-" .. STAMP)).user_seq, "first")
    assert_eq(assert(disk.load("sessions", "storb-" .. STAMP .. "-2")).user_seq, "second")
    session_store.remove(id)
  end)
end)

test("storage: recall and suggest sessions are named by their type", function()
  in_temp_home(function()
    local _, _, recalled = h.seed_recall({ backdate_s = 20 })
    session_store.convert_to_finished(recalled, h.finished_result("x", { finished_wall_epoch = EPOCH }))
    assert_eq(session_store.get_saved_filename("@"), "recall-" .. STAMP)

    local _, _, suggested = h.seed_recall({ backdate_s = 10 })
    session_store.convert_to_finished(suggested,
      h.finished_result("x", { finished_wall_epoch = EPOCH }), "suggest_idle")
    assert_eq(session_store.get_saved_filename("@"), "suggest-" .. STAMP)
    session_store.remove(recalled)
    session_store.remove(suggested)
  end)
end)

test("storage: with sessions.save off nothing is saved and pin needs a filename", function()
  in_temp_home(function()
    h.with_patch({ { config.sessions, "save", false } }, function()
      local id = finish("storoff", "dw")
      assert_eq(session_store.get_saved_filename("storoff"), nil)
      assert_eq(disk.list("sessions"), {})
      assert_error(function() archive.pin("storoff") end, "pin storoff <filename>")
      archive.pin("storoff", "kept")
      assert_eq(assert(disk.load("pinned", "kept")).user_seq, "dw")
      session_store.remove(id)
    end)
  end)
end)

test("storage: a session that cannot be saved still finishes", function()
  in_temp_home(function()
    local id
    h.with_patch({ { vim.uv, "fs_open", function() return nil, "EACCES: permission denied", "EACCES" end } },
      function() id = finish("storfail", "dw") end)
    assert_true(session_store.has_result("storfail"))
    assert_eq(session_store.get_saved_filename("storfail"), nil)
    assert_eq(disk.list("sessions"), {})
    vim.wait(20)
    session_store.remove(id)
  end)
end)

test("storage: pin copies a finished session under its saved filename, or the one given", function()
  in_temp_home(function()
    local id = finish("storp", "dw")
    archive.pin("storp")
    archive.pin("storp", "important")
    local pins = disk.list("pinned")
    table.sort(pins)
    assert_eq(pins, { "important", "storp-" .. STAMP })
    assert_eq(assert(disk.load("pinned", "important")).user_seq, "dw")
    assert_true(session_store.has_result("storp"), "the workspace copy stays")
    assert_eq(disk.list("sessions"), { "storp-" .. STAMP }, "the saved session stays")
    session_store.remove(id)
  end)
end)

test("storage: pin replaces a pin of the same filename", function()
  in_temp_home(function()
    local first = finish("storx", "v1")
    local second = finish("story", "v2")
    archive.pin("storx", "shared")
    archive.pin("story", "shared")
    assert_eq(assert(disk.load("pinned", "shared")).user_seq, "v2")
    archive.pin("story")
    archive.pin("story")
    assert_eq(#disk.list("pinned"), 2, "pinning twice keeps one pin")
    session_store.remove(first)
    session_store.remove(second)
  end)
end)

test("storage: pin refuses a session that is still active, even over a pin of that name", function()
  in_temp_home(function()
    disk.write_pin("storact", h.finished_result("old"))
    local rec = h.seed_named("storact", "test-storage-active")
    assert_error(function() archive.pin("storact", "copy") end, "still active")
    assert_eq(disk.list("pinned"), { "storact" })
    session_store.remove(rec.id)
  end)
end)

test("storage: pin by filename reads the saved session once its alias is gone", function()
  in_temp_home(function()
    local id = finish("storq", "dw")
    local filename = assert(session_store.get_saved_filename("storq"))
    session_store.remove(id)

    archive.pin(filename, "later")
    archive.pin(filename)
    archive.pin("later", "again")
    local pins = disk.list("pinned")
    table.sort(pins)
    assert_eq(pins, { "again", "later", filename })
    assert_eq(assert(disk.load("pinned", "again")).user_seq, "dw")
  end)
end)

test("storage: a handle is the live alias, then a pin, then a saved session", function()
  in_temp_home(function()
    local id = finish("storr", "memory")
    local filename = assert(session_store.get_saved_filename("storr"))
    disk.write_pin("storr", h.finished_result("pin"))
    assert_eq(archive.resolve_result("storr").user_seq, "memory")
    session_store.remove(id)
    assert_eq(archive.resolve_result("storr").user_seq, "pin")

    assert_eq(archive.resolve_result(filename).user_seq, "memory")
    disk.write_pin(filename, h.finished_result("pinned over"))
    assert_eq(archive.resolve_result(filename).user_seq, "pinned over")

    assert_error(function() archive.resolve_result("stornowhere") end,
      "No finished session, pin or saved session")
  end)
end)

test("storage: unpin and rm each delete from their own shelf", function()
  in_temp_home(function()
    local id = finish("stors", "dw")
    local filename = assert(session_store.get_saved_filename("stors"))
    archive.pin("stors")
    archive.pin("stors", "important")

    archive.unpin(filename)
    assert_eq(disk.list("pinned"), { "important" })
    assert_eq(disk.list("sessions"), { filename }, "unpin leaves the saved session")

    assert_error(function() archive.rm("important") end, "No saved session")
    archive.rm(filename)
    assert_eq(disk.list("sessions"), {})
    assert_eq(disk.list("pinned"), { "important" }, "rm leaves the pin")
    session_store.remove(id)
  end)
end)

test("storage: named saves from before pins become the pins", function()
  in_temp_home(function(data_home)
    local root = data_home .. "/nvim/vimficiency"
    vim.fn.mkdir(root .. "/saved", "p")
    local old = vim.tbl_extend("error", h.finished_result("legacy"), { version = 1 })
    vim.fn.writefile({ vim.json.encode(old) }, root .. "/saved/old.json")

    local id = finish("storm", "dw")
    assert_eq(vim.fn.isdirectory(root .. "/saved"), 1, "saving a session leaves the named saves alone")

    assert_eq(disk.list("pinned"), { "old" })
    assert_eq(vim.fn.isdirectory(root .. "/saved"), 0)
    assert_eq(archive.resolve_result("old").user_seq, "legacy")
    session_store.remove(id)
  end)
end)

test("storage: diffs and the command list survive the disk round-trip", function()
  in_temp_home(function()
    local commands = {
      { keys = "dw", kind = "operator" },
      { keys = "j", kind = "motion" },
    }
    local diffs = {
      { init = { begin_row = 0, begin_col = 1, end_row = 0, end_col = 4 },
        goal = { begin_row = 0, begin_col = 1, end_row = 0, end_col = 5 } },
    }
    local id = finish("stord", "dwj", { commands = commands, diffs = diffs })
    local loaded = assert(disk.load("sessions", assert(session_store.get_saved_filename("stord"))))
    assert_eq(loaded.commands, commands)
    assert_eq(loaded.diffs, diffs)
    assert_eq(loaded.prefix, "")
    session_store.remove(id)
  end)
end)

test("storage: a duplicated alias pins the same saved session", function()
  in_temp_home(function()
    local id = finish("storu", "dw")
    archive.duplicate_active("storu", "storucopy")
    assert_eq(session_store.get_saved_filename("storucopy"), "storu-" .. STAMP)
    assert_eq(disk.list("sessions"), { "storu-" .. STAMP }, "a duplicate is not saved again")
    session_store.remove(id)
    session_store.remove(assert(session_store.get_id("storucopy")))
  end)
end)

test("storage: add_finished rejects a non-named alias", function()
  assert_error(function() session_store.add_finished("3s", h.finished_result("abc")) end,
    "not a named alias")
end)

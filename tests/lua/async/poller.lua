local poller = require("vimficiency.async.poller")
local h      = require("_helpers")

local function never() error("must not run", 0) end

test("poller: resolves once with the first non-nil value", function()
  local polls, ready = 0, {}
  local handle = poller.start(
    function()
      polls = polls + 1
      if polls == 3 then return "done" end
    end,
    never,
    function(value) ready[#ready + 1] = value end,
    never)

  assert_true(vim.wait(1000, function() return #ready > 0 end, 5), "poller did not resolve")
  assert_true(not vim.wait(100, function() return polls > 3 end, 5), "polling must stop once resolved")
  poller.cancel(handle)

  assert_eq(vim.inspect(ready), vim.inspect({ "done" }))
end)

test("poller: a failed poll runs on_error once, then surfaces the error", function()
  local polls, errors, notices = 0, 0, {}
  h.with_patch({
    { vim, "notify", function(msg, level) notices[#notices + 1] = { msg = msg, level = level } end },
  }, function()
    local handle = poller.start(
      function()
        polls = polls + 1
        error("boom", 0)
      end,
      never,
      never,
      function() errors = errors + 1 end)

    assert_true(vim.wait(1000, function() return #notices > 0 end, 5), "error was not surfaced")
    assert_true(not vim.wait(100, function() return polls > 1 end, 5), "polling must stop after a failure")
    poller.cancel(handle)
  end)

  assert_eq(errors, 1)
  assert_eq(vim.inspect(notices), vim.inspect({ { msg = "boom", level = vim.log.levels.ERROR } }))
end)

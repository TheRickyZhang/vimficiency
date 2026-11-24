local helpers         = require("_helpers")
local explore_helpers = require("explore._helpers")
local explore         = require("vimficiency.explore")
local ffi_explore     = require("vimficiency.ffi.explore")
local registry        = require("vimficiency.explore.runtime.registry")

test("explore: a failed compute closes the pending tab and surfaces the error", function()
  local result = explore_helpers.fake_result()
  local real_cancel = ffi_explore.explore_cancel

  explore_helpers.with_notify_capture(function(notices)
    helpers.with_patch({
      { ffi_explore, "explore_poll", function(job_id)
        real_cancel(job_id)
        error("boom", 0)
      end },
    }, function()
      helpers.new_buf(result.lines)
      explore.open("failed", result)
      local tab = vim.api.nvim_get_current_tabpage()

      assert_true(
        vim.wait(1000, function() return not vim.api.nvim_tabpage_is_valid(tab) end, 5),
        "pending explore tab should close")
      assert_true(not registry.has_any(), "the pending view should be unregistered")
    end)

    local last = notices[#notices]
    assert_eq(last.msg, "boom")
    assert_eq(last.level, vim.log.levels.ERROR)
  end)

  vim.cmd("silent! tabonly")
end)

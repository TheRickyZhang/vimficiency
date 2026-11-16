local ffi_lib = require("vimficiency.ffi")

-- Ids start at 1, so 0 is never live.
test("ffi.explore: an id that is not live raises", function()
  assert_error(function() ffi_lib.explore_poll(0) end, "job_id")
  assert_error(function() ffi_lib.explore_state(0) end, "view_id")
  assert_error(function() ffi_lib.explore_recommendations(0, 5, "", "effort") end, "view_id")
end)

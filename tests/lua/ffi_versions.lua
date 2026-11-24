local ffi_core = require("vimficiency.ffi.core")
local ffi_versions = require("vimficiency.ffi.versions")

test("ffi.versions: every version and signature crosses, under the library's release", function()
  local v = ffi_versions.versions()
  assert_eq(v.release, ffi_core.version_string())
  for _, field in ipairs({ "journal_format", "samples_format", "rollup_format",
      "session_log_format", "scoring_version" }) do
    assert_true(type(v[field]) == "number" and v[field] >= 1, field)
  end
  for _, field in ipairs({ "journal_schema", "samples_schema", "rollup_schema",
      "session_log_schema", "scoring_signature" }) do
    assert_eq(type(v[field]), "string", field)
  end
end)

-- M.complete interpolates the typed prefix into `^`-anchored Lua patterns, so
-- pattern-magic characters must be escaped or completion raises "malformed
-- pattern" inside the completion callback.

local commands = require("vimficiency.commands")

test("commands.complete tolerates pattern-magic arg_lead", function()
  for _, lead in ipairs({ "(", ")", "[", "%", "-", "+", ".", "*", "?" }) do
    local cmd_line = "Vimfy " .. lead
    local ok, result = pcall(commands.complete, lead, cmd_line, #cmd_line)
    assert_true(ok, "complete must not error on arg_lead '" .. lead .. "': " .. tostring(result))
  end
end)

test("commands.complete still prefix-matches subcommands", function()
  local matches = commands.complete("st", "Vimfy st", #"Vimfy st")
  local has_start = false
  for _, name in ipairs(matches) do
    if name == "start" or name == "stats" or name == "store" then has_start = true end
  end
  assert_true(has_start, "literal prefix 'st' should still match start/stats/store")
end)

test("commands.complete offers candidates only at declared argument positions", function()
  local first = "Vimfy suggest t"
  assert_eq(vim.inspect(commands.complete("t", first, #first)), vim.inspect({ "toggle" }))

  local second = "Vimfy suggest on "
  assert_eq(#commands.complete("", second, #second), 0)
end)

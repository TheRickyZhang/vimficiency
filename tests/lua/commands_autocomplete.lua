-- `:Vimfy` completion interpolates the typed prefix into `^`-anchored Lua
-- patterns, so pattern-magic characters must be escaped or completion raises
-- "malformed pattern" inside the callback. Driven through the real user command.

require("vimficiency").setup({})

test(":Vimfy completion tolerates pattern-magic arg_lead", function()
  for _, lead in ipairs({ "(", ")", "[", "%", "-", "+", ".", "*", "?" }) do
    local ok, result = pcall(vim.fn.getcompletion, "Vimfy " .. lead, "cmdline")
    assert_true(ok, "completion must not error on arg_lead '" .. lead .. "': " .. tostring(result))
  end
end)

test(":Vimfy completion offers candidates only at declared argument positions", function()
  assert_eq(vim.inspect(vim.fn.getcompletion("Vimfy suggest t", "cmdline")), vim.inspect({ "toggle" }))
  assert_eq(#vim.fn.getcompletion("Vimfy suggest on ", "cmdline"), 0)
end)

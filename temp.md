## Explore your editing with `:Vimfy stats`

Open `:Vimfy stats` to see how much typing effort you could save across your finished sessions. Follow your efficiency over time, then
select a chart entry to see the runs behind it.

To include everyday edits between sessions, enable `journal = { enabled = true }` in your setup. Vimficiency scores runs of 4, 8, and 16
commands in the background. Select a recent journal run and press `<CR>` to replay your edit alongside the optimizer’s suggestion.

Journaling is off by default because it stores buffer text locally, unencrypted.

-- qwe.luacov: a line-coverage hook, and its lcov output.
local luacov = require("qwe.luacov")

local function eq(want, got, what)
  if want ~= got then error(string.format("%s: want %q, got %q", what, tostring(want), tostring(got)), 2) end
end

-- A chunk with a known layout: line 3 is inside a branch that never runs.
local src = table.concat({
  "local x = ...",      -- 1
  "if x then",          -- 2
  "  x = x + 1",        -- 3
  "else",               -- 4
  "  x = 0",            -- 5
  "end",                -- 6
  "return x",           -- 7
}, "\n")

-- The hook counts each line that runs, per chunk name, and only while started.
do
  local chunk = assert(loadstring(src, "=sample"))
  local cov = luacov.new()
  cov:start()
  chunk(nil)
  cov:stop()
  chunk(5) -- after stop: not counted
  local hits = cov:hits().sample
  eq(1, hits[1], "line 1 ran once")
  eq(1, hits[2], "line 2 ran once")
  eq(nil, hits[3], "the untaken branch")
  eq(1, hits[5], "the taken branch")
  eq(1, hits[7], "line 7 ran once")
end

-- executable_lines: every line that holds code, nested functions included; the
-- lines of `else`/`end` hold none. (A function's closing line is where its
-- closure is created, so the `end` of a nested function counts.)
do
  local nested = table.concat({
    "local x = ...",         -- 1
    "local function g()",    -- 2 (no code of its own)
    "  return x",            -- 3
    "end",                   -- 4 (the closure is created here)
    "return g()",            -- 5
  }, "\n")
  eq("1,3,4,5", table.concat(luacov.executable_lines(assert(loadstring(nested, "=n"))), ","), "nested function")
  eq("1,2,3,5,7", table.concat(luacov.executable_lines(assert(loadstring(src, "=s"))), ","), "branches")
end

-- lcov: one record per file, a DA line for every executable line (0 when it
-- never ran), and the LF/LH totals. Merges with the C report unchanged.
eq(table.concat({
  "SF:src/a.lua",
  "DA:1,2",
  "DA:3,0",
  "DA:4,1",
  "LF:3",
  "LH:2",
  "end_of_record",
  "SF:src/b.lua",
  "LF:0",
  "LH:0",
  "end_of_record",
  "",
}, "\n"), luacov.lcov({
  { path = "src/a.lua", lines = { 1, 3, 4 }, hits = { [1] = 2, [4] = 1, [9] = 5 } },
  { path = "src/b.lua", lines = {}, hits = {} },
}), "lcov text")

-- records/write: hits by chunk name become one record per embedded module, its
-- executable lines taken from the module's loaded chunk; write puts the lcov in
-- <dir>/luacov-<id>.dat, where bazel coverage merges every .dat it finds.
do
  package.preload["sample.mod"] = assert(loadstring(src, "=sample.mod"))
  local cov = luacov.new()
  cov:start()
  package.preload["sample.mod"](nil)
  cov:stop()
  local modules = { { name = "sample.mod", path = "src/sample.lua" }, { name = "unloaded", path = "src/unloaded.lua" } }
  local recs = cov:records(modules)
  eq(1, #recs, "a module that was never loaded has no record")
  eq("src/sample.lua", recs[1].path, "path")

  local dir = assert(os.getenv("TEST_TMPDIR"), "TEST_TMPDIR")
  local file = cov:write(dir, "42", modules)
  eq(dir .. "/luacov-42.dat", file, "file name")
  local f = assert(io.open(file))
  local text = f:read("*a")
  f:close()
  eq(luacov.lcov({ { path = "src/sample.lua", lines = { 1, 2, 3, 5, 7 }, hits = { [1] = 1, [2] = 1, [5] = 1, [7] = 1 } } }), text, "file content")
end

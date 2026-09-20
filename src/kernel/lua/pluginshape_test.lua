-- qwe.pluginshape: what a plugin file may contain, decided from its source.
local shape = require("qwe.pluginshape")

local function eq(want, got, what)
  if want ~= got then error(string.format("%s: want %q, got %q", what, tostring(want), tostring(got)), 2) end
end

local function problems(src) return shape.check(src) end

local function has(list, text)
  for _, p in ipairs(list) do
    if p.message:find(text, 1, true) then return p end
  end
end

-- The usual shapes pass: a module table with function fields, local helpers,
-- constants, an inert require, and the return.
eq(0, #problems([[
local M = {}
local LIMIT = 10
local names = { "a", "b", n = LIMIT * 2 }
local q = require("qwe.exec")
local function helper(x) return x .. "!" end
function M.check(_with, _ctx) return helper(names[1]) == q end
M.apply = function(_with, _ctx) end
return M
]]), "a normal plugin")

eq(0, #problems("local function a() end\nlocal function b() end\nreturn { check = a, apply = b }\n"), "a returned constructor")
eq(0, #problems("local M = {}\nfunction M.argv(w, c) return c.backend:command(w.run) end\nreturn M\n"), "run-like")

-- Top-level code is refused, with its position.
local p = has(problems("local M = {}\nprint('loaded')\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "call")
eq(2, p.line, "line of a top-level call")
eq(1, p.col, "column of a top-level call")
assert(has(problems("local M = {}\nlocal f = io.open('x', 'w')\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "call"), "a call in a local's value")
assert(has(problems("local M = {}\nM.t = os.time()\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "call"), "a call in a field's value")
assert(has(problems("local M = {}\nfor i = 1, 3 do end\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "loop"), "a loop")
assert(has(problems("local M = {}\nif true then end\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "if"), "an if")
assert(has(problems("local M = {}\ndo end\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "do-block"), "a do-block")
assert(has(problems("local M = {}\nlocal s = ('x'):rep(3)\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "method call"), "an Invoke")
-- a require of a file qwe does not provide would run that file's top level
assert(has(problems("local M = {}\nlocal x = require('some.project.file')\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "call"), "a foreign require")
-- state at the top: a global-ish assignment or a rebind
assert(has(problems("local M = {}\nlocal n = 0\nn = n + 1\nfunction M.check() end\nfunction M.apply() end\nreturn M\n"), "only M.name"), "a rebind")

-- check and apply must both exist, as functions.
assert(has(problems("local M = {}\nfunction M.check() end\nreturn M\n"), "must export check and apply"), "no apply")
assert(has(problems("local M = {}\nfunction M.apply() end\nreturn M\n"), "must export check and apply"), "no check")
assert(has(problems("local M = {}\nM.check = 1\nM.apply = 2\nreturn M\n"), "must export check and apply"), "not functions")
assert(has(problems("local M = {}\nfunction M.check() end\nfunction M.apply() end\n"), "must export"), "no return")
assert(has(problems("return {}\n"), "must export"), "empty module")
print("ok   pluginshape")

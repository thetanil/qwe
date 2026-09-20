-- The shape of a step plugin's plugin.lua, checked from its source without
-- running any of it.
--
-- A plugin file is declarations and one return. Loading it must not be able to
-- change anything, so the top level may hold only:
--   * local declarations whose values are inert: literals, function literals,
--     table constructors of inert values, reads of names and fields, operators,
--     and require of a module qwe itself provides
--   * assignments of an inert value to a field of a local (M.x = ..., which is
--     also what `function M.check() ... end` is)
--   * one final `return` of the module table
-- Anything else (a call, a loop, an if, a do-block) is a statement that would
-- run when the file loads, and is refused. Work belongs in check and apply.
--
-- The module must export check and apply as functions, or argv (a run-like
-- plugin); each is a function literal or a local function.
local parser = require("luacheck.parser")
local decoder = require("luacheck.decoder")

local M = {}

local INERT_LEAF = { Nil = true, True = true, False = true, Number = true, String = true, Id = true, Function = true }

-- Whether require("name") is of a module qwe provides (embedded, so ours).
local function provided(name)
  return package.preload[name] ~= nil
end

local function inert(node)
  local tag = node.tag
  if INERT_LEAF[tag] then return true end
  if tag == "Paren" then return inert(node[1]) end
  if tag == "Index" then return inert(node[1]) and inert(node[2]) end
  if tag == "Op" then
    for i = 2, #node do
      if not inert(node[i]) then return false end
    end
    return true
  end
  if tag == "Table" then
    for _, item in ipairs(node) do
      if item.tag == "Pair" then
        if not (inert(item[1]) and inert(item[2])) then return false end
      elseif not inert(item) then
        return false
      end
    end
    return true
  end
  if tag == "Call" then
    local callee, arg = node[1], node[2]
    return #node == 2 and callee.tag == "Id" and callee[1] == "require" and arg.tag == "String"
      and provided(arg[1])
  end
  return false
end

local WHY = {
  Call = "a call would run when the file loads",
  Invoke = "a method call would run when the file loads",
  If = "an if would run when the file loads",
  While = "a loop would run when the file loads",
  Repeat = "a loop would run when the file loads",
  Fornum = "a loop would run when the file loads",
  Forin = "a loop would run when the file loads",
  Do = "a do-block would run when the file loads",
}

-- Analyses source text. Returns a list of { line, col, message }: the top-level
-- code the plugin must not have, and what it fails to export.
function M.check(src)
  local problems = {}
  local offsets = {}
  local ok, ast = pcall(parser.parse, decoder.decode(src), offsets, {})
  if not ok then return problems end -- luacheck reports syntax errors
  local function add(node, message)
    problems[#problems + 1] = {
      line = node.line,
      col = node.offset - (offsets[node.line] or node.offset) + 1,
      message = message,
    }
  end

  local functions = {} -- local name -> true, when bound to a function
  local tables = {} -- local name -> { field -> value node }, for module tables
  local returned

  local function is_function(node)
    return node.tag == "Function" or (node.tag == "Id" and functions[node[1]] == true)
  end

  for i, stmt in ipairs(ast) do
    local tag = stmt.tag
    if tag == "Local" or tag == "Localrec" then
      local names, values = stmt[1], stmt[2] or {}
      for k, value in ipairs(values) do
        local good = inert(value)
        if not good then
          add(value, "top-level code is not allowed in a plugin: " .. (WHY[value.tag] or "this value is computed when the file loads")
            .. "; put it in check or apply, or in a function they call")
        elseif names[k] then
          local name = names[k][1]
          if value.tag == "Function" then functions[name] = true end
          if value.tag == "Table" then
            local fields = {}
            for _, item in ipairs(value) do
              if item.tag == "Pair" and item[1].tag == "String" then fields[item[1][1]] = item[2] end
            end
            tables[name] = fields
          end
        end
      end
    elseif tag == "Set" then
      local target, value = stmt[1][1], stmt[2][1]
      if #stmt[1] == 1 and target.tag == "Index" and target[1].tag == "Id" and target[2].tag == "String" then
        if inert(value) then
          local t = tables[target[1][1]]
          if t then t[target[2][1]] = value end
        else
          add(value, "top-level code is not allowed in a plugin: this value is computed when the file loads; "
            .. "put it in check or apply, or in a function they call")
        end
      else
        add(stmt, "top-level code is not allowed in a plugin: only M.name = value assignments; a plugin has no state of its own")
      end
    elseif tag == "Return" then
      if i ~= #ast or #stmt ~= 1 then
        add(stmt, "the plugin must end with a single return of its module table")
      else
        returned = stmt[1]
      end
    else
      add(stmt, "top-level code is not allowed in a plugin: " .. (WHY[tag] or "this statement would run when the file loads")
        .. "; put it in check or apply, or in a function they call")
    end
  end

  local exports
  if returned and returned.tag == "Table" then
    exports = {}
    for _, item in ipairs(returned) do
      if item.tag == "Pair" and item[1].tag == "String" then exports[item[1][1]] = item[2] end
    end
  elseif returned and returned.tag == "Id" then
    exports = tables[returned[1]]
  end
  local function exported(name) return exports and exports[name] and is_function(exports[name]) end
  if not (exported("argv") or (exported("check") and exported("apply"))) then
    problems[#problems + 1] = {
      message = "the plugin must export check and apply functions, or argv (a run-like plugin)",
    }
  end
  return problems
end

return M

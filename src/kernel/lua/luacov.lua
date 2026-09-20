-- qwe.luacov: line coverage for the Lua qwe ships, from debug.sethook.
local M = {}
local Cov = {}
Cov.__index = Cov

function M.new()
  return setmetatable({ counts = {} }, Cov)
end

function Cov:start()
  local counts = self.counts
  debug.sethook(function(_, line)
    local src = debug.getinfo(2, "S").short_src
    local lines = counts[src]
    if not lines then
      lines = {}
      counts[src] = lines
    end
    lines[line] = (lines[line] or 0) + 1
  end, "l")
end

function Cov:stop()
  debug.sethook()
end

-- { [chunk name] = { [line] = times run } }
function Cov:hits()
  return self.counts
end

-- One { path, lines, hits } record per module (of { name, path }) that has a chunk in package.preload.
function Cov:records(modules)
  local out = {}
  for _, m in ipairs(modules) do
    local chunk = package.preload[m.name]
    if chunk then
      out[#out + 1] = { path = m.path, lines = M.executable_lines(chunk), hits = self.counts[m.name] or {} }
    end
  end
  return out
end

-- Writes the lcov for modules to <dir>/luacov-<id>.dat and returns that path.
function Cov:write(dir, id, modules)
  local path = string.format("%s/luacov-%s.dat", dir, id)
  local f = assert(io.open(path, "w"))
  f:write(M.lcov(self:records(modules)))
  f:close()
  return path
end

local util = require("jit.util")

local function collect(fn, seen)
  local info = util.funcinfo(fn)
  for pc = 1, info.bytecodes - 1 do
    seen[util.funcinfo(fn, pc).currentline] = true
  end
  for n = -1, -info.gcconsts, -1 do
    local k = util.funck(fn, n)
    if type(k) == "proto" then collect(k, seen) end
  end
end

-- The sorted lines of a loaded chunk that hold bytecode, nested functions included.
function M.executable_lines(chunk)
  local seen, lines = {}, {}
  collect(chunk, seen)
  for line in pairs(seen) do lines[#lines + 1] = line end
  table.sort(lines)
  return lines
end

-- Records { path, lines (executable, sorted), hits } as lcov text.
function M.lcov(records)
  local out = {}
  for _, r in ipairs(records) do
    local hit = 0
    out[#out + 1] = "SF:" .. r.path
    for _, line in ipairs(r.lines) do
      local n = r.hits[line] or 0
      if n > 0 then hit = hit + 1 end
      out[#out + 1] = string.format("DA:%d,%d", line, n)
    end
    out[#out + 1] = "LF:" .. #r.lines
    out[#out + 1] = "LH:" .. hit
    out[#out + 1] = "end_of_record"
  end
  out[#out + 1] = ""
  return table.concat(out, "\n")
end

local active

-- Starts counting for the life of the state and writes <dir>/luacov-<pid>.dat when it closes.
-- modules is a list of { name, path }.
function M.enable(dir, modules)
  local cov = M.new()
  active = { cov = cov, dir = dir, modules = modules }
  cov:start()
  local sentinel = newproxy(true)
  getmetatable(sentinel).__gc = M.flush
  active.sentinel = sentinel
end

-- Writes what enable has counted so far. Called at close, and by a forked child before it execs.
function M.flush()
  if not active then return end
  active.cov:stop()
  active.cov:write(active.dir, require("qwe.exec").getpid(), active.modules)
  active.cov:start()
end

return M

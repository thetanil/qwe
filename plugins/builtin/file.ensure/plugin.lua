-- The built-in file.ensure step plugin: a file has the given content and mode.
-- Everything goes through the job's backend. The content travels on stdin,
-- never in a command line.
local M = {}

local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- "644" or "0644" -> "0644"; nil if it is not an octal mode.
local function normalize_mode(text)
  if not text:match("^[0-7][0-7][0-7]?[0-7]?$") then return nil end
  return string.format("%04o", tonumber(text, 8))
end

local function wanted_mode(with)
  if with.mode == nil then return nil end
  local mode = normalize_mode(with.mode)
  if not mode then error("file.ensure: mode must be octal, like 0644: " .. with.mode, 0) end
  return mode
end

-- The file as it is: nil if it does not exist, otherwise { mode, content }.
local function inspect(with, ctx)
  local path = quote(with.path)
  local stat = ctx.backend:run("stat -c %a -- " .. path)
  if stat.code ~= 0 then return nil end
  local mode = normalize_mode(stat.stdout:gsub("%s+$", ""))
  if not mode then error("file.ensure: cannot read the mode of " .. with.path, 0) end
  local state = { mode = mode }
  if with.content ~= nil then
    local cat = ctx.backend:run("cat -- " .. path)
    if cat.code ~= 0 then error("file.ensure: cannot read " .. with.path .. ": " .. cat.stderr, 0) end
    state.content = cat.stdout
  end
  return state
end

function M.check(with, ctx)
  local want_mode = wanted_mode(with)
  local state = inspect(with, ctx)
  if not state then return true end
  local needs = (with.content ~= nil and state.content ~= with.content)
    or (want_mode ~= nil and state.mode ~= want_mode)
  return needs, { mode = state.mode }
end

function M.apply(with, ctx)
  local want_mode = wanted_mode(with)
  local state = inspect(with, ctx)
  local path = quote(with.path)
  if not state or (with.content ~= nil and state.content ~= with.content) then
    local write = ctx.backend:run("cat > " .. path, with.content or "")
    if write.code ~= 0 then error("file.ensure: cannot write " .. with.path .. ": " .. write.stderr, 0) end
  end
  if want_mode ~= nil and (not state or state.mode ~= want_mode) then
    local chmod = ctx.backend:run("chmod " .. want_mode .. " " .. path)
    if chmod.code ~= 0 then error("file.ensure: cannot chmod " .. with.path .. ": " .. chmod.stderr, 0) end
  end
end

return M

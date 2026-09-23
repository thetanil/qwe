-- The built-in file.read step plugin: reads a file on the job's target, through the
-- execution backend (so it works over ssh too), and reports it as outputs. It changes
-- nothing: all the work is done in check, which runs once; apply is never called.
local M = {}

local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- The OS reason from a failed command's stderr, with shell noise (like "sh: 1: cannot
-- open x: ") stripped: the text after the last ": ", trimmed. Falls back to the whole
-- trimmed stderr if there is no ": ".
local function clean_reason(stderr)
  local trimmed = stderr:gsub("^%s+", ""):gsub("%s+$", "")
  return trimmed:match(".*: (.*)$") or trimmed
end

-- "600" -> "0600"; the raw text unchanged if it is not an octal number.
local function octal_mode(raw)
  local n = tonumber(raw, 8)
  if not n then return raw end
  return string.format("%04o", n)
end

local MISSING = { exists = false, content = "", sha256 = "", mode = "", owner = "" }

function M.check(with, ctx)
  local path = quote(with.path)
  local stat = ctx.backend:run("stat -c '%a %U' -- " .. path)
  if stat.code ~= 0 then return false, MISSING end
  local mode, owner = stat.stdout:match("^(%S+)%s+(%S+)")

  local cat = ctx.backend:run("cat -- " .. path)
  if cat.code ~= 0 then error("file.read: cannot read " .. with.path .. ": " .. clean_reason(cat.stderr), 0) end

  local sha = ctx.backend:run("sha256sum -- " .. path)
  if sha.code ~= 0 then error("file.read: cannot read " .. with.path .. ": " .. clean_reason(sha.stderr), 0) end
  local hash = sha.stdout:match("^(%x+)")

  return false, {
    exists = true,
    content = cat.stdout,
    sha256 = hash,
    mode = octal_mode(mode),
    owner = owner,
  }
end

function M.apply() end

return M

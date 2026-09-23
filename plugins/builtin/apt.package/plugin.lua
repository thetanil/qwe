-- The built-in apt.package step plugin: a Debian/Ubuntu package present or absent on the
-- target. It never escalates by itself: apt-get needs root, so the step needs
-- `become: true` like any other command this backend runs; without it, apply fails with a
-- clean "are you root?" message instead of silently doing nothing.
local M = {}

local function quote(s)
  return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function last_line(text)
  local trimmed = text:gsub("%s+$", "")
  return trimmed:match("([^\n]*)$") or trimmed
end

-- The version from `apt list --installed`'s "name/suite version arch [...]" line, or nil
-- if the package is not installed. The leading "Listing..." line (and, on stderr, apt's
-- "does not have a stable CLI" warning) never matches this, so they are ignored for free.
local function installed_version(stdout)
  for line in stdout:gmatch("[^\n]+") do
    local version = line:match("^%S+/%S+%s+(%S+)")
    if version then return version end
  end
  return nil
end

local function apply_error(name, verb, stderr)
  if stderr:find("Unable to locate package", 1, true) then
    return "apt.package: no package " .. name
  end
  local last = last_line(stderr)
  if last:find("are you root?", 1, true) then
    return "apt.package: cannot " .. verb .. " " .. name .. ": are you root? (set become: true)"
  end
  return "apt.package: cannot " .. verb .. " " .. name .. ": " .. last
end

function M.check(with, ctx)
  local list = ctx.backend:run("LC_ALL=C apt list --installed " .. quote(with.name))
  local version = installed_version(list.stdout)
  local want_present = with.state ~= "absent"
  local needs = (version ~= nil) ~= want_present
  return needs, { version = version or "" }
end

function M.apply(with, ctx)
  local name = quote(with.name)
  if with.state == "absent" then
    local r = ctx.backend:run("apt-get remove -y " .. name)
    if r.code ~= 0 then error(apply_error(with.name, "remove", r.stderr), 0) end
  else
    local r = ctx.backend:run("DEBIAN_FRONTEND=noninteractive apt-get install -y " .. name)
    if r.code ~= 0 then error(apply_error(with.name, "install", r.stderr), 0) end
  end
end

return M

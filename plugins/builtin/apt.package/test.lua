local case, eq = ...
local recording = require("backend.recording")
local checkapply = require("qwe.checkapply")
local plugin = require("qwe.plugins").builtin_module("apt.package")

local NAME = "hello"
local LIST = "LC_ALL=C apt list --installed '" .. NAME .. "'"
local INSTALL = "DEBIAN_FRONTEND=noninteractive apt-get install -y '" .. NAME .. "'"
local REMOVE = "apt-get remove -y '" .. NAME .. "'"

local NOT_INSTALLED = "Listing...\n"
local INSTALLED = "Listing...\nhello/now 2.10-3build1 amd64 [installed,local]\n"
local WARNING = "\nWARNING: apt does not have a stable CLI interface. Use with caution in scripts.\n\n"

local function run(script, with)
  local rec = recording.new(script)
  local result = checkapply.run(plugin, with, { backend = rec })
  rec:done()
  return result, rec
end

case("four_states", function()
  -- present, not installed: installs.
  local result = run({
    { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
    { expect = INSTALL },
    { expect = LIST, stdout = INSTALLED, stderr = WARNING },
  }, { name = NAME })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
  eq("2.10-3build1", result.outputs.version, "version")

  -- present, already installed: nothing.
  result = run({
    { expect = LIST, stdout = INSTALLED, stderr = WARNING },
  }, { name = NAME })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
  eq("2.10-3build1", result.outputs.version, "version")

  -- absent, installed: removes.
  result = run({
    { expect = LIST, stdout = INSTALLED, stderr = WARNING },
    { expect = REMOVE },
    { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
  }, { name = NAME, state = "absent" })
  eq("ok", result.status, "status")
  eq(true, result.changed, "changed")
  eq("", result.outputs.version, "version")

  -- absent, not installed: nothing.
  result = run({
    { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
  }, { name = NAME, state = "absent" })
  eq("ok", result.status, "status")
  eq(false, result.changed, "changed")
  eq("", result.outputs.version, "version")
end)

case("errors", function()
  local function install_fails(stderr)
    local rec = recording.new({
      { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
      { expect = INSTALL, code = 100, stderr = stderr },
    })
    local ok, msg = pcall(checkapply.run, plugin, { name = NAME }, { backend = rec })
    eq(false, ok, "pcall ok for stderr " .. string.format("%q", stderr))
    rec:done()
    return msg
  end

  eq("apt.package: no package " .. NAME,
    install_fails("E: Unable to locate package " .. NAME .. "\n"),
    "unknown package")

  eq("apt.package: cannot install " .. NAME .. ": are you root? (set become: true)",
    install_fails("E: Could not open lock file /var/lib/dpkg/lock-frontend - open (13: Permission denied)\n"
      .. "E: Unable to acquire the dpkg frontend lock (/var/lib/dpkg/lock-frontend), are you root?\n"),
    "missing root")

  eq("apt.package: cannot install " .. NAME .. ": E: Broken packages",
    install_fails("Reading package lists...\nE: Broken packages\n"),
    "otherwise: the last line of stderr")

  -- not-converged: apply "succeeds" (exit 0) but the second check still finds no work done.
  local rec = recording.new({
    { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
    { expect = INSTALL },
    { expect = LIST, stdout = NOT_INSTALLED, stderr = WARNING },
  })
  local result = checkapply.run(plugin, { name = NAME }, { backend = rec })
  eq("failed", result.status, "status")
  eq("not-converged", result.reason, "reason")
  eq(true, result.changed, "changed")
  rec:done()

  -- remove uses "remove" in its message too.
  local remove_rec = recording.new({
    { expect = LIST, stdout = INSTALLED, stderr = WARNING },
    { expect = REMOVE, code = 100, stderr = "E: some apt-get failure\n" },
  })
  local ok, msg = pcall(checkapply.run, plugin, { name = NAME, state = "absent" }, { backend = remove_rec })
  eq(false, ok, "pcall ok")
  eq("apt.package: cannot remove " .. NAME .. ": E: some apt-get failure", msg, "remove failure message")
  remove_rec:done()
end)

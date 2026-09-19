-- The ssh backend's translation is a pure function of its inputs.
local case, eq = ...
local ssh = require("backend.ssh")
local exec = require("qwe.exec")

case("translation_golden", function()
  local backend = ssh.new({ host = "box", sock = "/tmp/qwe-1000/42.%C", env = { A = "1", B = "it's" } })
  local argv, stdin = backend:command("echo $A; echo 'x'", "input\n")
  local want = {
    "ssh", "-S", "/tmp/qwe-1000/42.%C", "-o", "ControlMaster=no", "-o", "BatchMode=yes", "-o", "ProxyCommand=false", "box", "--",
    "sh -c '" .. exec.bootstrap:gsub("'", "'\\''") .. "' sh 'echo $A; echo '\\''x'\\'''",
  }
  eq(#want, #argv, "argv length")
  for i, w in ipairs(want) do eq(w, argv[i], "argv[" .. i .. "]") end
  eq("A=1\nB=it's\n\ninput\n", stdin, "stdin: the preamble, then the command's own")
end)

case("command_never_puts_env_in_argv", function()
  local backend = ssh.new({ host = "box", sock = "/s", env = { SECRET = "hunter2" } })
  local argv = backend:command("true")
  for _, a in ipairs(argv) do eq(nil, a:find("hunter2", 1, true), "argv word") end
end)

case("master_argv_is_fixed", function()
  local argv = ssh.master_argv("box", "/s", "/l")
  eq("ssh -M -N -f -S /s -o BatchMode=yes -o ConnectTimeout=10 -o LogLevel=ERROR -E /l box",
    table.concat(argv, " "), "master argv")
end)

case("sock_path_is_short", function()
  local path = ssh.sock_path(1000, 4194304)
  eq("/tmp/qwe-1000/4194304.%C", path, "path")
  -- ssh appends "." and 16 characters while it binds; %C expands to 40
  assert(#path - 2 + 40 + 17 < 108, "socket path too long")
end)

-- the remote command string for a become: value, up to the bootstrap
local function remote_prefix(become)
  local argv = ssh.command_argv("box", "/s", "true", become)
  local remote = argv[#argv]
  return (remote:match("^(.-) ?sh %-c '") or "<no match>")
end

case("become_translation", function()
  eq("", remote_prefix(nil), "absent")
  eq("", remote_prefix(false), "false")
  eq("sudo -n", remote_prefix(true), "true")
  eq("sudo -n -u runner", remote_prefix("runner"), "a name")
  eq("sudo -n -u '#1001'", remote_prefix(1001), "a uid")
  -- 0 is uid 0, not false
  eq("sudo -n -u '#0'", remote_prefix(0), "uid 0")
  -- the same words on the local backend, as separate argv entries
  local become = require("qwe.become")
  eq("sudo -n -u #1001", table.concat(become.prefix(1001), " "), "local uid")
  eq(0, #become.prefix(false), "local false")
  eq(0, #become.prefix(nil), "local absent")
end)

case("become_wraps_preamble", function()
  -- sudo comes first and the preamble shell (sh -c <bootstrap>) is its command,
  -- so the env is read inside sudo, after env_reset
  local backend = ssh.new({ host = "box", sock = "/s", env = { A = "1" }, become = "runner" })
  local argv, stdin = backend:command("echo $A")
  local remote = argv[#argv]
  eq("sudo -n -u runner sh -c '", remote:sub(1, 25), "the remote command starts with sudo, then the bootstrap shell")
  assert(remote:find("' sh 'echo $A'", 1, true), "the script is the shell's $1")
  eq("A=1\n\n", stdin, "the preamble is still stdin")
  -- and the local backend
  local argv_local = require("backend.local").new({ env = {}, become = true }):command("true")
  eq("sudo -n sh -c", table.concat(argv_local, " ", 1, 4), "local argv")
end)

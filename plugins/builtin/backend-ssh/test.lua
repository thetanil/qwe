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

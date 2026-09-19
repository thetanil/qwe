# Plugins run only on the operator host. Targets need only `sh`, and devices are never targets

qwe runs on the operator's PC. The machines it manages, such as a Raspberry Pi controller in a datacenter behind an SSH jumphost, have no qwe and no Lua, and must not need them. Unlike Ansible, which copies its modules to each remote host and runs them there, a qwe plugin only builds commands. It runs on the operator host and sends commands through the job's execution backend (`local`, `ssh`, later `docker`). Devices under test (automotive hardware, often QNX on a read-only filesystem) are never targets at all. They're acted on only by commands the controller runs (switch PoE, netboot, fastboot, serial).

## Consequences

- Nothing about qwe is deployed to targets. Upgrading qwe never touches the lab.
- Anything a plugin needs from a target has to be expressed as shell commands and their output.
- A plugin may also do work on the operator host itself (for example GitHub API calls), because that's where it runs. See ADR-0003 for how steps choose where their commands run.

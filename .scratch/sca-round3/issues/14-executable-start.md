# 14: `__executable_start`

Status: ready-for-agent
Category: enhancement
Type: task

## What

`src/kernel/oom_shim.c:76` reads the linker-defined symbol `__executable_start`
so that `QWE_OOM_SITE_LOG` can print call sites as offsets into the executable
(for `addr2line -i -e <test binary>`). It is one of the five names in the
reserved-identifier allow list. It is test-only code (`oom_shim` is `testonly`),
but a name on an exception list is a name the assessor reads.

## Fix

Compute the load base without naming a reserved symbol. In a static glibc binary
`dl_iterate_phdr` reports the main program's `dlpi_addr` and headers; the
executable's base is the address of its first `PT_LOAD` segment
(`dlpi_addr + phdr[i].p_vaddr` for the lowest). `getauxval(AT_PHDR)` and
`AT_PHENT` give the program headers directly and are simpler. Print
`(uintptr_t)return_address - base` the same way as now.

Do not use `/proc/self/maps`: the harness runs in forked children with the
allocator armed, and the failing call is by definition one where `fopen`,
`malloc` and stdio may not be trusted.

## Acceptance criteria

- [ ] `__executable_start` is gone from the source. `manual: grep -rn __executable_start src tools`
- [ ] `oom_shim_test`'s site-log case still produces offsets that
      `addr2line` resolves to the failing call. `unit: src/kernel/oom_shim_test.c`
      (extend the case to check the printed offset against a known function's
      offset, so the base is asserted and not only "some number")
- [ ] It is removed from `AllowedIdentifiers` in `.clang-tidy` and the gate exits
      0. `manual: run.sh`
- [ ] `bazel test //...` and the coverage check are green.

## Comments

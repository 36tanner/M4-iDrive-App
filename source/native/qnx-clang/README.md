# QNX 6.5 workers with clang

An experiment in building the ID6 diagnostic workers without the QNX SDP: stock clang 18 and lld, LLVM's
compiler-rt builtins for soft-float, headers written for this project (`include/`), and our own start-up
code (`crt0.S`). The empty stand-in libraries only give the linker QNX's library names; the head unit's
`libc.so.3`, `libm.so.2` and `libsocket.so.3` are used at run time.

```bash
sh native/qnx-clang/build.sh
```

It writes `out/boostdeck-collector-qnx` (the v0.7.6 BoostDeck worker, unmodified) and
`out/boostdeck-collector-s55` (the same with `S55_ABSOLUTE_OIL`: oil pressure shown as gauge pressure).
The alert chime is stubbed (QNX audio structures are not modelled).

## What was checked offline

Against the stock v0.7.6 `boostdeck-collector-qnx` (built with QNX GCC 4.4.2, not stripped):

- ELF shape: interpreter `/usr/lib/ldqnx.so.2`, `NEEDED` libsocket.so.3, libm.so.2, libc.so.3 in that
  order, base 0x100000, two `LOAD` segments, SysV hash, e_flags 0x05000002, the same `QNX stack` note.
- Start-up: `_init_libc(argc, argv, envp, auxv, cleanup)`, `_preinit_array`, `atexit(_fini)`,
  `_fini_array`, `_init`, `_init_array`, `errno = 0`, `exit(main(...))`.
- Soft-float ABI, ARM state, ARMv7-A, no hardware divide, as the stock binary.
- Constants at calls into QNX libraries (`verify_calls.py`): `CLOCK_MONOTONIC` 2, `SOL_SOCKET` 0xffff,
  `SO_ERROR` 0x1007, `SO_BROADCAST` 0x20, `IPPROTO_TCP`/`TCP_NODELAY`, `F_SETFL` 4 with `O_NONBLOCK` 0x80,
  SIGHUP/SIGINT/SIGALRM/SIGTERM/SIGPIPE, `SIG_IGN` 1, `RTLD_NOW|RTLD_GLOBAL` 0x102 and `RTLD_LOCAL` 0x200.
- Struct layouts at the same call sites: BSD `sockaddr_in` (family at offset 1, port at 2) and the
  16-byte `struct sigaction` with the handler first.

## Not yet known

Whether `ldqnx.so.2` and libc accept the result on the head unit. Run it by hand before installing it:
it is a separate process, so a failure cannot affect iDrive.

# C* v0.12 iOS fix

The original v0.12 used `system()` to invoke clang while creating `.cso` objects.
Apple marks `system()` unavailable on iOS, so the compiler itself could not be built with the iPhoneOS SDK.

This revision replaces `system()` with `posix_spawnp()` + `waitpid()`.
It launches `clang` directly, preserves the inherited environment/PATH, and avoids shell quoting.

Build on iOS/jailbroken iPhone:

```sh
cd /var/mobile/Documents/cstar-test
clang -std=c11 -Wall -Wextra -Wpedantic src/cstar.c -o cstar
./cstar tests/module_v12.cppo
```

Expected:

```text
[C* Compiler v0.12] compiled successfully, output.c generated
C* module links: tests/math.cso
```

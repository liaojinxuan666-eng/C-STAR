# C* v0.16 — SIMD bridge foundation

C* continues to follow the original principles:

- 极致的性能
- 优秀的兼容
- 极致的简易

## What this stage provides

This is a first SIMD integration step, **not yet a C* vector type or compiler intrinsic syntax**.
It adds a small native ABI bridge for common 128-bit integer-lane operations, using ARM NEON
intrinsics when compiled for AArch64/NEON and a scalar fallback on other targets:

- 4 × u32 add/sub/xor/compare-equal
- 8 × u16 add
- 16 × u8 add (wrapping lane arithmetic)

The C* program calls ordinary external functions with pointers, so the interface uses the
existing C ABI and introduces no VM, garbage collector, or hidden runtime. The bridge allows
us to validate semantics and target-specific code on iPhone while the compiler's direct
vector representation is designed separately.

## Important limitation

Each operation currently crosses a C function boundary. This is appropriate for validating
ABI, lane semantics, and backend selection, but is **not the final high-performance interface**
for a CPU emulator's per-instruction hot path. The next stage should add C* vector types and
lower intrinsic expressions directly into the generated C translation unit (or inline compiler
intrinsics), eliminating these helper calls.

No performance advantage over plain C/C++ is claimed by this package. Measure generated code
and benchmark before making such claims.

## Add to the existing repository

Copy the contents into the matching paths in the C-STAR repository:

- `examples/simd128.cppo`
- `run_v016.sh`
- `include/cstar_simd128.h`
- `runtime/cstar_simd128.c`

Keep your existing `src/cstar.c`. This stage does not require replacing the compiler.

## Run on the existing iPhone setup

From `/var/mobile/Documents/cstar-test`, after extracting/copying the archive contents into the project root:

```sh
sh examples/run_v016.sh
```

The runner detects either `examples/` (the GitHub layout) or `tests/` (your local test layout), builds binaries into `.cstar-test-bin/`, and does not use `/tmp`. The included SIMD example can remain under `examples/` even when your older tests live under `tests/`.
Expected new test output:

```text
simd128: ok
```

The script's final PASS line is only expected after all previous regression tests and the new
SIMD test pass. On AArch64, the runtime chooses the NEON implementation at compile time; on
other architectures it uses the scalar fallback.

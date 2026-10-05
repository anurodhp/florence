# Plan: the real JIT (later)

Status: not started. The build today runs JavaScriptCore's LLInt (assembly interpreter, no JIT, -O2); measured against the C
interpreter it gained only about 5 % on the page benchmark (`tests/pages/bench.html`, `docs/HANDOFF.md` has the numbers), and about
half of a page load is not JavaScript at all. The JIT is the one change that can make script several times faster, and the
riskiest, so it is its own project.

## Why it is not just a switch

JSC's JIT writes machine code into memory and then runs it. On Darwin WebKit does that with `MAP_JIT` memory and, on Apple
silicon, `pthread_jit_write_protect_np` (APRR/fast permission switching, `ENABLE(FAST_JIT_PERMISSIONS)`). The Pi's Cortex-A53 has
neither, and xnu-7195 was never asked to run a JIT outside Apple's entitlement scheme. The kernel is ours, so each of these can
be made to work, but each has to be proven before the WebKit rebuild (about an hour a try).

## Steps, in order, each with a pass/fail test

1. **Executable memory, kernel side (30 minutes).** A small C test on the Pi: `mmap(PROT_READ|PROT_WRITE|PROT_EXEC, MAP_ANON|MAP_PRIVATE)`
   with and without `MAP_JIT`; write a `ret` instruction sequence; `sys_icache_invalidate` (already in `libflocompat`); call it.
   Also the two-step form WebKit uses without fast permissions: map RW, write, `mprotect` to RX, call. Pass: both run. If the
   kernel refuses (code-signing enforcement, `vm_map_enter` wire/exec checks), the fix is in xnu (the iokit port's rule: a fork,
   not a patch).
2. **The libSystem surface JSC's JIT needs (1 hour).** `pthread_jit_write_protect_np`, `pthread_jit_write_protect_supported_np`
   (answer 0), `os_thread_self_restrict_rwx_*` if referenced, `mach_vm_*` the allocator uses. Provide them in `libflocompat`
   first (as the other shims), then DARW tickets so they move into libSystem.
3. **WebKit options (`config/webkit-options.cmake`).** `ENABLE_JIT ON`, `ENABLE_DFG_JIT ON`, `ENABLE_FTL_JIT OFF` (needs B3/LLVM and
   a lot of RAM), WebAssembly stays OFF, `ENABLE_C_LOOP OFF`, sampling profiler OFF. Check that `ENABLE_JIT_CAGE`/fast-permissions
   are not selected for this CPU. Build in its own directory (`FL_WK_DIR=webkit-cross-jit`), as the LLInt build was.
4. **First run with the JIT forced off at run time** (`JSC_useJIT=false`): must equal today's LLInt numbers. That separates "the JIT
   build works" from "the JIT works".
5. **Baseline JIT only** (`JSC_useDFGJIT=false`), then DFG. Run `tests/pages/bench.html` and the smoke test each time; watch RSS
   (the JIT's code memory is the cost on a 1 GB machine: cap it with `JSC_jitMemoryReservationSize` and watch the helper's RSS).
6. **Decide on the numbers.** Keep the JIT on by default only if the benchmark wins clearly and RSS stays acceptable; otherwise ship
   it off with `JSC_useJIT` documented as the switch, or leave it out of the image.

## Risks and how they show up

* The kernel panics or kills the process on an exec mapping: step 1 finds it in minutes, not after a rebuild.
* Instruction-cache coherence on the A53 (stale code runs): the `sys_icache_invalidate` in `flo_runtime.c` is the architected
  sequence; a crash in freshly generated code points here first.
* Memory: baseline + DFG code and the compile threads cost RAM and, with one core, steal time from the page. Measure both.
* The web process gets a second thread for compilation; the single-core kernel (DAR SMP ticket) makes that a tradeoff.

## What would change the plan

SMP on the Pi (cores 1-3) makes the JIT's compile threads free and is worth doing first. A GPU path (see HANDOFF.md, "GL") does not
help JavaScript at all.

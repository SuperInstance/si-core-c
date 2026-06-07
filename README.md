# si-core-c

General-purpose C library for the **SuperInstance** unified runtime — constraint-aware AI with conservation laws, spectral methods, capability discovery, and agent state machines.

## Why C?

- **Zero-overhead abstractions** for performance-critical inference paths
- **Portable** — works anywhere a C99 compiler exists (embedded, WASM, HPC)
- **Deterministic** — no GC pauses, no hidden allocations in hot paths
- **Interop-friendly** — trivial FFI to Python, Rust, Go, Node, etc.

## Building

```bash
make clean && make all      # Build the static library objects
make test                   # Compile and run the test suite
make memcheck               # Run tests under valgrind
make clean                  # Remove build artifacts
```

**Requirements:** GCC or Clang with C99 support, `-lm` (math library).

## API Overview

### Conservation Budget

Track resource allocation with conservation-law semantics. A total capacity `C` is split into productive (`gamma`) and waste (`eta`) portions.

```c
SiBudget *b = budget_create(100.0);
budget_allocate(b, 70.0, 20.0);          // gamma=70, eta=20, free=10
budget_transfer(b, 0, 1, 5.0);           // move 5 from gamma to eta
BudgetReport r = budget_audit(b);        // utilization, waste ratio, violations
budget_free(b);
```

### Spectral Ranker

Power-iteration eigenvalue decomposition for ranking and condition-number estimation.

```c
double matrix[] = {2,1,0, 1,2,1, 0,1,2};
double ranks[3];
spectral_rank(matrix, 3, ranks);         // eigenvalue magnitudes
double cond = spectral_condition(matrix, 3); // condition number
```

### Capability Discovery

Parse simple TOML files (sections, key-value pairs) to describe what a component provides.

```c
Capability *cap = capability_load("CAPABILITY.toml");
bool has = capability_provides(cap, "streaming"); // true/false
capability_free(cap);
```

### Agent State Machine

Five-state lifecycle: `IDLE → THINKING → EXECUTING → LEARNING → IDLE` with `ERROR` recovery.

```c
SiAgent *a = agent_create(budget, caps);
agent_transition(a, AGENT_THINKING);
AgentAction act = agent_decide(a, "classify this input");
agent_free(a);
```

### Computational Cell

A computation unit with an associated budget, handler function, and dependency list.

```c
SiCell *cell = cell_create("embed", budget, my_handler);
cell_add_dep(cell, "tokenizer");
SiError err = cell_execute(cell, input, in_len, output, &out_len);
cell_free(cell);
```

### Utilities

```c
printf("si-core version: %s\n", si_version()); // "0.1.0"
si_init();
// ... use the library ...
si_shutdown();
```

## Performance Notes

- **Spectral methods**: O(n² × iterations) per eigenvalue via deflated power iteration. Suitable for matrices up to ~1000×1000. For larger problems, consider LAPACK integration.
- **Budget operations**: O(1) — just double-precision arithmetic.
- **Capability parsing**: Linear in file size, single-pass.
- **No global state** except `si_init()`/`si_shutdown()` idempotent flag.
- **No dynamic allocation in hot paths** except capability parsing and cell dependency registration.

## Project Structure

```
si_core.h          Public header (all types and function declarations)
si_core.c          Full implementation
tests/test_core.c  Comprehensive test suite
Makefile           Build system
README.md          This file
```

## License

MIT

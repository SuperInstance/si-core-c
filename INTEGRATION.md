# INTEGRATION.md — si-core-c

Cross-language integration guide for the **SuperInstance C runtime library** (`si-core-c`).
This document shows the same conservation budget operation in all 7 supported languages,
how this library connects to the broader SuperInstance ecosystem, and FFI binding patterns.

---

## Table of Contents

1. [Same Operation in 7 Languages](#1-same-operation-in-7-languages)
2. [Cross-Repo Integration](#2-cross-repo-integration)
3. [FFI Bindings](#3-ffi-bindings)

---

## 1. Same Operation in 7 Languages

The canonical operation: **create a conservation budget of C=1000, allocate gamma=600 and eta=400, verify the invariant γ+η=C, then transfer 50 from gamma to eta.**

### C (si-core-c — this repo)

```c
#include "si_core.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    si_init();

    /* Create budget with total capacity C = 1000 */
    SiBudget *budget = budget_create(1000.0);
    assert(budget != NULL);

    /* Allocate gamma (productive) and eta (waste) */
    SiError err = budget_allocate(budget, 600.0, 400.0);
    assert(err == SI_OK);

    /* Audit: verify γ + η == C */
    BudgetReport rpt = budget_audit(budget);
    assert(rpt.violation == 0);
    assert(rpt.gamma + rpt.eta == rpt.total_budget);
    printf("gamma=%.1f eta=%.1f total=%.1f utilization=%.2f%%\n",
           rpt.gamma, rpt.eta, rpt.total_budget, rpt.utilization * 100.0);

    /* Transfer 50 from gamma (0) to eta (1) */
    err = budget_transfer(budget, /*from=*/0, /*to=*/1, 50.0);
    assert(err == SI_OK);

    /* Re-audit */
    rpt = budget_audit(budget);
    assert(rpt.gamma == 550.0);
    assert(rpt.eta   == 450.0);
    assert(rpt.gamma + rpt.eta == rpt.total_budget);
    printf("After transfer: gamma=%.1f eta=%.1f total=%.1f\n",
           rpt.gamma, rpt.eta, rpt.total_budget);

    budget_free(budget);
    si_shutdown();
    return 0;
}
```

### Rust (conservation-law-rs — reference implementation)

```rust
use conservation_law::ConservationBudget;

fn main() {
    // Create budget with total C = 1000
    let mut budget = ConservationBudget::new(1000.0);

    // Allocate gamma and eta
    budget.allocate(600.0, 400.0).expect("allocation failed");

    // Verify invariant: γ + η == C
    let audit = budget.audit();
    assert!((audit.gamma + audit.eta - audit.total).abs() < 1e-10);
    println!("gamma={} eta={} total={}", audit.gamma, audit.eta, audit.total);

    // Transfer 50 from gamma to eta
    budget.transfer("gamma", "eta", 50.0).expect("transfer failed");

    let audit = budget.audit();
    assert!((audit.gamma - 550.0).abs() < 1e-10);
    assert!((audit.eta - 450.0).abs() < 1e-10);
    println!("After transfer: gamma={} eta={}", audit.gamma, audit.eta);
}
```

### Python (si-runtime-python)

```python
from si_runtime import Budget, AgentBudget, audit

# Create budget with total C = 1000
budget = Budget(total=1000.0, gamma=600.0, eta=400.0)

# Verify invariant: gamma + eta == total
assert abs(budget.gamma + budget.eta - budget.total) < 1e-9
print(f"gamma={budget.gamma} eta={budget.eta} total={budget.total}")

# Transfer is done via AgentBudget
agent_a = AgentBudget(id="agent-a", budget=budget)
agent_b = AgentBudget(id="agent-b", budget=Budget(total=1000.0, gamma=0.0, eta=1000.0))

# Transfer modifies agent allocations while preserving invariants
agent_a.allocate(600.0)
transfer(agent_a, agent_b, 50.0)
print(f"agent_a remaining={agent_a.remaining}")
```

### TypeScript (si-runtime-js)

```typescript
import { ConservationBudget } from 'si-runtime-js';

// Create budget with total C = 1000
const budget = new ConservationBudget(1000);

// Allocate gamma and eta
budget.allocate(600, 400);

// Audit: verify γ + η == C
const report = budget.audit();
console.log(`gamma=${report.gamma} eta=${report.eta} total=${report.C}`);
// Invariant check is built into allocate() — throws if violated

// Transfer 50 from gamma to eta
budget.transfer('gamma', 'eta', 50);

const after = budget.audit();
console.log(`After transfer: gamma=${after.gamma} eta=${after.eta}`);
```

### Zig (si-runtime-zig)

```zig
const std = @import("std");
const conservation = @import("conservation.zig");

pub fn main() !void {
    // Create budget with total C = 1000
    var budget = conservation.ConservationBudget.init(1000.0);

    // Allocate gamma and eta
    try budget.allocate(600.0, 400.0);

    // Audit: verify γ + η == C
    const report = try budget.audit();
    std.debug.print("gamma={d:.1} eta={d:.1} total={d:.1}\n",
        .{ report.gamma, report.eta, report.total });

    // Transfer 50 from gamma to eta
    try budget.transfer(true, 50.0); // from_gamma=true

    const after = try budget.audit();
    std.debug.print("After transfer: gamma={d:.1} eta={d:.1}\n",
        .{ after.gamma, after.eta });
}
```

### Go (si-runtime-go)

```go
package main

import (
    "fmt"
    siruntime "github.com/SuperInstance/si-runtime-go"
)

func main() {
    // Create budget with total C = 1000
    budget := siruntime.NewBudget(1000)

    // Allocate gamma and eta (must sum to total)
    err := budget.Allocate(600, 400)
    if err != nil {
        panic(err)
    }

    // Verify invariant: gamma + eta == total
    fmt.Printf("gamma=%.1f eta=%.1f total=%.1f remaining=%.1f\n",
        budget.Gamma, budget.Eta, budget.Total, budget.Remaining())

    // Transfer 50 from eta to gamma (Go Transfer moves eta→gamma)
    err = budget.Transfer(50)
    if err != nil {
        panic(err)
    }

    fmt.Printf("After transfer: gamma=%.1f eta=%.1f total=%.1f\n",
        budget.Gamma, budget.Eta, budget.Total)
}
```

### WASM (si-runtime-wasm — from JavaScript)

```javascript
import init, { Budget } from 'si-runtime-wasm';

async function run() {
    await init();

    // Create budget with total C = 1000
    const budget = new Budget(1000);

    // Allocate from free pool
    budget.allocate(300);

    // Transfer between gamma and eta
    budget.transfer_gamma_to_eta(50);
    budget.transfer_eta_to_gamma(25);

    // Verify conservation invariant
    const valid = budget.audit();
    console.log(`Audit passed: ${valid}`);
    console.log(`gamma=${budget.gamma()} eta=${budget.eta()} ` +
                `allocated=${budget.allocated()} total=${budget.total()}`);
}
```

---

## 2. Cross-Repo Integration

### conservation-law-rs (Mathematical Foundation)

`si-core-c` is a faithful C port of the Rust `conservation-law-rs` crate. The mathematical
invariants (γ + η = C) are defined there and enforced identically here. Any changes to the
conservation law semantics must originate in `conservation-law-rs` and be ported to this C
implementation.

**Connection points:**
- `budget_create()` ↔ `ConservationBudget::new()`
- `budget_allocate()` ↔ `ConservationBudget::allocate()`
- `budget_audit()` ↔ `ConservationBudget::audit()`
- `spectral_rank()` ↔ `spectral::power_iteration()`

### spectral-fleet-rs (Fleet Ranking)

`spectral-fleet-rs` uses conservation budgets for agent ranking and fleet rebalancing. The C
runtime provides the same spectral ranking capability via `spectral_rank()` and
`spectral_top_k()`, allowing C-based agents to participate in fleet-wide ranking without
requiring a Rust dependency.

**Connection points:**
- `spectral_rank()` provides eigenvector centrality for fleet agents
- `spectral_condition()` exposes condition number for fleet health monitoring

### si-cli (CLI Discovery)

`si-cli` can discover C-based agents by loading their shared libraries at runtime. The C ABI
is the bridge — `si-cli` loads `libsi_core.so` / `si_core.dll` and calls `agent_create()`,
`agent_decide()`, etc. to interact with C agents from the CLI.

**Connection points:**
- `agent_create()` / `agent_decide()` — CLI instantiates C agents
- `capability_load()` — CLI discovers agent capabilities from TOML files
- `budget_to_json()` — CLI serializes budget state for display

### si-fleet-api (REST API Layer)

The REST API layer (`si-fleet-api`) can serve C-based agent data by exposing the JSON
serialization of C budgets and audit reports. The `budget_to_json()` function produces
API-compatible JSON directly.

**Connection points:**
- `budget_to_json()` → REST `GET /agents/:id/budget`
- `budget_audit()` → REST `GET /fleet/audit`
- `spectral_rank()` → REST `POST /fleet/rank` (matrix payload)

### Supabase Fleet Registry (Data Backend)

Agent registrations and budget snapshots can be persisted to Supabase. The C runtime
doesn't directly connect to Supabase — instead, `si-fleet-api` serializes C budget/agent
state to JSON and stores it via the Supabase client.

**Connection points:**
- `budget_to_json()` output is stored in `agent_budgets` table
- `BudgetReport` fields map to Supabase columns (`gamma`, `eta`, `total`, `violation`)
- `capability_load()` data is stored in `capabilities` table

### sunset-ecosystem (Fleet Coordination)

`sunset-ecosystem` coordinates multi-fleet operations. C-based agents participate by
exposing their conservation budgets and spectral rankings through the C ABI, which
`sunset-ecosystem` calls via its Rust FFI layer.

**Connection points:**
- `budget_transfer()` enables cross-agent budget movement during rebalancing
- `agent_transition()` / `agent_decide()` for fleet-level state machine coordination
- `spectral_rank()` for fleet-wide agent ranking

---

## 3. FFI Bindings

### Calling si-core-c from Rust

```rust
use std::os::raw::c_double;
use std::ffi::CString;

extern "C" {
    fn si_init();
    fn budget_create(c: c_double) -> *mut std::ffi::c_void;
    fn budget_allocate(b: *mut std::ffi::c_void, gamma: c_double, eta: c_double) -> i32;
    fn budget_free(b: *mut std::ffi::c_void);
    fn si_shutdown();
}

fn main() {
    unsafe {
        si_init();
        let budget = budget_create(1000.0);
        let err = budget_allocate(budget, 600.0, 400.0);
        assert_eq!(err, 0); // SI_OK
        budget_free(budget);
        si_shutdown();
    }
}
```

### Calling si-core-c from Python (via ctypes)

```python
import ctypes
from pathlib import Path

lib = ctypes.CDLL(str(Path("libsi_core.so")))

lib.si_init()
lib.budget_create.restype = ctypes.c_void_p
lib.budget_create.argtypes = [ctypes.c_double]

budget = lib.budget_create(1000.0)

lib.budget_allocate.restype = ctypes.c_int
lib.budget_allocate.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double]
err = lib.budget_allocate(budget, 600.0, 400.0)
assert err == 0

lib.budget_free(budget)
lib.si_shutdown()
```

### Calling si-core-c from Node.js (via ffi-napi)

```javascript
const ffi = require('ffi-napi');
const ref = require('ref-napi');

const lib = ffi.Library('./libsi_core', {
    'si_init':          ['void',  []],
    'budget_create':    ['pointer', ['double']],
    'budget_allocate':  ['int',   ['pointer', 'double', 'double']],
    'budget_audit':     ['void',  ['pointer', 'pointer']], // writes BudgetReport
    'budget_free':      ['void',  ['pointer']],
    'si_shutdown':      ['void',  []],
});

lib.si_init();
const budget = lib.budget_create(1000.0);
const err = lib.budget_allocate(budget, 600.0, 400.0);
console.log('allocate result:', err);
lib.budget_free(budget);
lib.si_shutdown();
```

### Calling si-core-c from Zig

```zig
const std = @import("std");

extern fn si_init() void;
extern fn budget_create(C: f64) ?*anyopaque;
extern fn budget_allocate(b: ?*anyopaque, gamma: f64, eta: f64) c_int;
extern fn budget_free(b: ?*anyopaque) void;
extern fn si_shutdown() void;

pub fn call_c_runtime() !void {
    si_init();
    const budget = budget_create(1000.0) orelse return error.CreationFailed;
    defer budget_free(budget);

    const err = budget_allocate(budget, 600.0, 400.0);
    if (err != 0) return error.AllocationFailed;
    si_shutdown();
}
```

### Calling si-core-c from Go (via cgo)

```go
package main

/*
#cgo LDFLAGS: -lsi_core
#include "si_core.h"
*/
import "C"
import "fmt"

func main() {
    C.si_init()
    budget := C.budget_create(C.double(1000.0))
    err := C.budget_allocate(budget, C.double(600.0), C.double(400.0))
    fmt.Printf("allocate result: %d\n", int(err))
    C.budget_free(budget)
    C.si_shutdown()
}
```

### Calling si-core-c from WASM

WASM runs in a sandbox and cannot directly load shared libraries. Instead, compile
`si-core-c` to WASM via Emscripten (`emcc si_core.c -o si_core.js`), then import from JS:

```javascript
// After emcc compilation
const Module = require('./si_core.js');

Module().then(mod => {
    mod._si_init();
    const budget = mod._budget_create(1000.0);
    const err = mod._budget_allocate(budget, 600.0, 400.0);
    console.log('result:', err);
    mod._budget_free(budget);
    mod._si_shutdown();
});
```

### C ABI Export Summary

| Function | Signature | Description |
|---|---|---|
| `si_init` | `void si_init(void)` | Initialize library |
| `si_version` | `const char* si_version(void)` | Get version string |
| `budget_create` | `SiBudget* budget_create(double C)` | Create conservation budget |
| `budget_allocate` | `SiError budget_allocate(SiBudget*, double gamma, double eta)` | Set γ and η |
| `budget_transfer` | `SiError budget_transfer(SiBudget*, int from, int to, double amount)` | Move between γ↔η |
| `budget_audit` | `BudgetReport budget_audit(const SiBudget*)` | Verify invariant |
| `budget_to_json` | `SiError budget_to_json(const SiBudget*, char* buf, size_t len)` | Serialize to JSON |
| `budget_free` | `void budget_free(SiBudget*)` | Destroy budget |
| `spectral_rank` | `SiError spectral_rank(const double* mat, int n, double* out)` | Eigenvector ranking |
| `spectral_top_k` | `SiError spectral_top_k(const double* mat, int n, int k, int* out)` | Top-k ranked indices |
| `agent_create` | `SiAgent* agent_create(SiBudget*, Capability*)` | Create agent |
| `agent_transition` | `SiError agent_transition(SiAgent*, AgentState)` | State transition |
| `agent_decide` | `AgentAction agent_decide(SiAgent*, const char* task)` | Decide action |
| `cell_create` | `SiCell* cell_create(const char*, SiBudget*, CellHandler)` | Create computation cell |
| `cell_execute` | `SiError cell_execute(SiCell*, const void*, size_t, void*, size_t*)` | Execute cell |

---

## Integration Test Matrix

| From → To | C | Rust | Python | TypeScript | Zig | Go | WASM |
|---|---|---|---|---|---|---|---|
| **C** | ✅ native | cdylib | ctypes | ffi-napi | `@cImport` | cgo | emscripten |
| **Rust** | extern "C" | ✅ native | PyO3 | wasm-bindgen | C ABI | C ABI | wasm-bindgen |
| **Python** | ctypes | PyO3 | ✅ native | N/A | C ABI | cgo | N/A |
| **TypeScript** | ffi-napi | wasm-bindgen | N/A | ✅ native | N/A | N/A | wasm API |
| **Zig** | `@cImport` | C ABI | C ABI | N/A | ✅ native | C ABI | N/A |
| **Go** | cgo | C ABI | C ABI | N/A | C ABI | ✅ native | N/A |
| **WASM** | emscripten | wasm-bindgen | N/A | JS import | N/A | N/A | ✅ native |

All cross-language calls flow through the **C ABI** as the universal bridge.

---

*Generated for SuperInstance cross-language integration — si-core-c v0.1.0*

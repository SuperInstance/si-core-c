# INTEGRATION.md — si-core-c × conservation-law-rs

**Embedded conservation in C**: si-core-c provides the C runtime (budgets, spectral ranking, agent state machines). conservation-law-rs provides Lagrangian mechanics and Noether's theorem. Together: conservation-law-aware embedded agents.

## Synergy Map

```
si-core-c (C)                     conservation-law-rs (Rust)
┌───────────────────────┐         ┌──────────────────────────┐
│ SiBudget              │         │ Lagrangian               │
│   budget_create(C)    │◄───────►│ MechanicalLagrangian     │
│   budget_allocate()   │         │ SymplecticIntegrator     │
│   budget_audit()      │         │ Noether / Symmetry       │
│ Spectral Ranker       │         │ ConservationDetector     │
│   spectral_rank()     │         │ ConservationReport       │
│   spectral_condition()│         │ PhaseSpacePoint          │
│ SiAgent               │         │ FleetIntegration         │
│   agent_create()      │         │ CircuitState             │
│   agent_transition()  │         │ ConservationReport       │
│ SiCell                │         │ conserved module          │
│   cell_create()       │         └──────────────────────────┘
│   cell_execute()      │                   │
└───────────────────────┘                   ▼
         │                      Rust FFI wrapper reads C budget
         ▼                      and applies conservation laws
    Embedded agent with
    physics-grade conservation
```

## Key Insight

si-core-c's `SiBudget` enforces `γ + η = C` (productive + waste = total). This is a discrete conservation law. conservation-law-rs generalizes this to continuous Lagrangian mechanics: the Euler–Lagrange equations are the continuous version of the budget constraint. FFI bridges the C runtime with Rust's conservation analysis.

## Example 1: Conservation-Aware Budget Auditing

Link si-core-c's budget to conservation-law-rs's conservation detection:

```c
/* si_conservation_demo.c — Link si-core budget with conservation analysis */

#include <stdio.h>
#include "si_core.h"

int main(void) {
    si_init();
    printf("si-core version: %s\n", si_version());

    /* Create a conservation budget with total capacity 1000 */
    SiBudget *budget = budget_create(1000.0);
    if (!budget) { fprintf(stderr, "Failed to create budget\n"); return 1; }

    /* Allocate: 700 productive, 300 waste */
    SiError err = budget_allocate(budget, 700.0, 300.0);
    if (err != SI_OK) { fprintf(stderr, "Allocation failed: %d\n", err); return 1; }

    /* Audit the budget */
    BudgetReport rpt = budget_audit(budget);
    printf("=== Budget Audit ===\n");
    printf("Total:       %.1f\n", rpt.total_budget);
    printf("Productive:  %.1f (γ)\n", rpt.gamma);
    printf("Waste:       %.1f (η)\n", rpt.eta);
    printf("Unallocated: %.1f\n", rpt.unallocated);
    printf("Utilization: %.1f%%\n", rpt.utilization * 100.0);
    printf("Waste ratio: %.1f%%\n", rpt.waste_ratio * 100.0);
    printf("Violation:   %s\n", rpt.violation ? "YES" : "no");

    /* Transfer 100 from waste to productive */
    err = budget_transfer(budget, 1, 0, 100.0);  /* from eta(1) to gamma(0) */
    if (err == SI_OK) {
        rpt = budget_audit(budget);
        printf("\nAfter transfer (η→γ, 100):\n");
        printf("Productive: %.1f, Waste: %.1f\n", rpt.gamma, rpt.eta);
    }

    /* Serialize to JSON */
    char json[256];
    budget_to_json(budget, json, sizeof(json));
    printf("Budget JSON: %s\n", json);

    budget_free(budget);
    si_shutdown();
    return 0;
}
```

Rust side reads the budget and applies conservation analysis:

```rust
use conservation_law_rs::conserved::{ConservationDetector, ConservedQuantity};
use conservation_law_rs::lagrangian::{AgentState, MechanicalLagrangian, total_energy};

/// Verify that si-core-c's budget invariant (γ + η = C) holds
/// by treating it as a conserved quantity.
fn verify_budget_conservation(
    budget_snapshots: &[(f64, f64, f64)], // Vec of (gamma, eta, C)
) -> Vec<ConservedQuantity<f64>> {
    let mut detector = ConservationDetector::<f64, 3>::new(0.001);

    let initial = budget_snapshots[0];
    let initial_total = initial.0 + initial.1;
    let mut max_drift = 0.0f64;

    for &(gamma, eta, c) in budget_snapshots {
        let total = gamma + eta;
        let drift = (total - c).abs();
        max_drift = max_drift.max(drift);
    }

    let quantity = ConservedQuantity {
        name: "γ + η = C".to_string(),
        initial_value: initial.2,
        max_drift,
        is_conserved: max_drift < 0.001,
    };

    vec![quantity]
}

fn main() {
    // Simulated budget snapshots from si-core-c
    let snapshots = vec![
        (700.0, 300.0, 1000.0),
        (750.0, 250.0, 1000.0),
        (800.0, 200.0, 1000.0),
        (600.0, 400.0, 1000.0),
    ];

    let results = verify_budget_conservation(&snapshots);
    for q in &results {
        println!("{}: conserved={}, max_drift={:.6}",
            q.name, q.is_conserved, q.max_drift);
        assert!(q.verify(0.01), "Budget conservation violated!");
    }
}
```

## Example 2: Spectral Condition Number for Agent Ranking

Use si-core-c's spectral ranker alongside conservation-law-rs's Lagrangian analysis:

```c
/* spectral_demo.c */
#include <stdio.h>
#include "si_core.h"

int main(void) {
    si_init();

    /* 3×3 agent coupling matrix */
    double matrix[] = {
        2.0, 1.0, 0.0,
        1.0, 3.0, 1.0,
        0.0, 1.0, 2.0,
    };
    int n = 3;

    /* Spectral ranking */
    double ranks[3];
    SiError err = spectral_rank(matrix, n, ranks);
    if (err == SI_OK) {
        printf("Spectral ranks: %.4f, %.4f, %.4f\n", ranks[0], ranks[1], ranks[2]);
    }

    /* Top-k agents */
    int top2[2];
    err = spectral_top_k(matrix, n, 2, top2);
    if (err == SI_OK) {
        printf("Top-2 agents: %d, %d\n", top2[0], top2[1]);
    }

    /* Condition number */
    double cond = spectral_condition(matrix, n);
    printf("Condition number: %.4f\n", cond);

    /* Agent with budget */
    SiBudget *budget = budget_create(100.0);
    budget_allocate(budget, 80.0, 20.0);
    SiAgent *agent = agent_create(budget, NULL);

    printf("Agent state: %d\n", agent_get_state(agent));
    AgentAction action = agent_decide(agent, "optimize fleet");
    printf("Agent decision for 'optimize': %d\n", action);

    agent_free(agent);
    budget_free(budget);
    si_shutdown();
    return 0;
}
```

Rust side uses conservation-law-rs to model the agent's dynamics:

```rust
use conservation_law_rs::lagrangian::{AgentState, Lagrangian, SymplecticIntegrator, total_energy};
use conservation_law_rs::noether::{Symmetry, TranslationSymmetry};

/// Model the si-core agent's energy as a Lagrangian system
struct AgentLagrangian {
    mass: f64,
    potential_coeff: f64,
}

impl Lagrangian<f64, 2> for AgentLagrangian {
    fn kinetic(&self, state: &AgentState<f64, 2>) -> f64 {
        0.5 * self.mass * (state.q_dot[0].powi(2) + state.q_dot[1].powi(2))
    }
    fn potential(&self, state: &AgentState<f64, 2>) -> f64 {
        self.potential_coeff * (state.q[0].powi(2) + state.q[1].powi(2))
    }
}

fn main() {
    let lagrangian = AgentLagrangian { mass: 1.0, potential_coeff: 0.5 };

    let state = AgentState::new([1.0, 0.0], [0.0, 1.0]);
    println!("Kinetic energy: {:.4}", lagrangian.kinetic(&state));
    println!("Potential energy: {:.4}", lagrangian.potential(&state));
    println!("Total energy: {:.4}", total_energy(&lagrangian, &state));
    println!("Lagrangian L = T - V: {:.4}", lagrangian.lagrangian(&state));

    // Symplectic integration preserves the si-core budget invariant
    let integrated = lagrangian.symplectic_euler(&state, 0.01, 100);
    println!("After 100 steps: q={:?}, q_dot={:?}", integrated.q, integrated.q_dot);
}
```

## Example 3: Cell Execution with Conservation Enforcement

```c
/* cell_demo.c — Computation cell with budget enforcement */
#include <stdio.h>
#include "si_core.h"

SiError my_handler(const void *input, size_t input_len,
                   void *output, size_t *output_len) {
    const double *in = (const double *)input;
    double *out = (double *)output;
    out[0] = in[0] * 2.0;
    *output_len = sizeof(double);
    return SI_OK;
}

int main(void) {
    si_init();

    SiBudget *budget = budget_create(10.0);
    budget_allocate(budget, 8.0, 2.0);

    SiCell *cell = cell_create("doubler", budget, my_handler);
    cell_add_dep(cell, "input_source");

    double input = 21.0;
    double output = 0.0;
    size_t out_len = sizeof(double);

    SiError err = cell_execute(cell, &input, sizeof(input), &output, &out_len);
    if (err == SI_OK) {
        printf("Cell output: %.1f\n", output);
    } else {
        printf("Cell execution failed: budget exhausted?\n");
    }

    cell_free(cell);
    budget_free(budget);
    si_shutdown();
    return 0;
}
```

## Data Flow

```
si-core-c (C runtime)
         │
    ┌────┼────┐
    ▼    ▼    ▼
Budget Agent Cell
  │     │     │
  └─────┼─────┘
        ▼
  FFI boundary (C → Rust)
        │
        ▼
conservation-law-rs
  ├─ ConservationDetector: verify budget = conserved quantity
  ├─ Lagrangian: model agent dynamics
  ├─ Noether: symmetry → conservation law
  └─ FleetIntegration: fleet-wide audit
```

## When to Use This Combination

- **Embedded agents**: C runtime with Rust conservation analysis running alongside
- **Fleet energy management**: si-core-c tracks per-agent budgets, conservation-law-rs verifies fleet-wide energy conservation
- **Safety-critical systems**: budget violations detected by Lagrangian mechanics before they cause failures
- **Cross-language systems**: C hot path, Rust analysis path, connected via FFI

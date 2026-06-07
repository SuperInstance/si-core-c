/**
 * si_core.h — Public header for the SuperInstance unified runtime C library.
 *
 * Provides conservation budgets, spectral ranking, capability discovery,
 * agent state machines, and computational cells with dependency tracking.
 *
 * Version: 0.1.0
 * License: MIT
 */

#ifndef SI_CORE_H
#define SI_CORE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version / Lifecycle ──────────────────────────────────────────────── */

#define SI_VERSION_MAJOR 0
#define SI_VERSION_MINOR 1
#define SI_VERSION_PATCH 0

/** Returns a static version string, e.g. "0.1.0". */
const char *si_version(void);

/** One-time library initialisation (idempotent). */
void si_init(void);

/** Library shutdown — free any global resources. */
void si_shutdown(void);

/* ── Error codes ──────────────────────────────────────────────────────── */

typedef enum {
    SI_OK              =  0,
    SI_ERR_INVALID     = -1,
    SI_ERR_BUDGET      = -2,
    SI_ERR_TRANSITION  = -3,
    SI_ERR_IO          = -4,
    SI_ERR_NOMEM       = -5,
    SI_ERR_OVERFLOW    = -6,
} SiError;

/* ── Conservation Budget ──────────────────────────────────────────────── */

/** Audit report produced by budget_audit(). */
typedef struct {
    double total_budget;     /**< Total capacity C. */
    double gamma;            /**< Productive allocation. */
    double eta;              /**< Waste / overhead allocation. */
    double unallocated;      /**< C - gamma - eta. */
    double utilization;      /**< gamma / C (0..1). */
    double waste_ratio;      /**< eta / C (0..1). */
    int    violation;        /**< Non-zero if gamma+eta > C. */
} BudgetReport;

/** Opaque conservation budget handle. */
typedef struct SiBudget SiBudget;

/** Create a budget with total capacity C. */
SiBudget *budget_create(double C);

/** Allocate gamma (productive) and eta (waste) from the budget. */
SiError   budget_allocate(SiBudget *b, double gamma, double eta);

/** Transfer amount between categories: 0=gamma, 1=eta. */
SiError   budget_transfer(SiBudget *b, int from, int to, double amount);

/** Generate an audit report for the budget. */
BudgetReport budget_audit(const SiBudget *b);

/** Serialize budget to JSON string in the provided buffer. */
SiError   budget_to_json(const SiBudget *b, char *buf, size_t len);

/** Free a budget. */
void      budget_free(SiBudget *b);

/* ── Spectral Ranker ──────────────────────────────────────────────────── */

/**
 * Compute spectral ranking via power iteration.
 * @param matrix  Row-major n×n symmetric matrix.
 * @param n       Dimension.
 * @param out_ranks  Output array of n doubles (eigenvalue magnitudes).
 * @return SI_OK on success.
 */
SiError spectral_rank(const double *matrix, int n, double *out_ranks);

/**
 * Return indices of the top-k ranked elements.
 * @param out_indices  Output array of k ints (0-based indices).
 */
SiError spectral_top_k(const double *matrix, int n, int k, int *out_indices);

/**
 * Compute the spectral condition number (|λ_max| / |λ_min|).
 * Returns -1.0 on error.
 */
double  spectral_condition(const double *matrix, int n);

/* ── Capability Discovery ─────────────────────────────────────────────── */

/** Single key-value capability entry. */
typedef struct CapEntry {
    char *key;
    char *value;
    struct CapEntry *next;
} CapEntry;

/** Parsed capability set loaded from a TOML-like file. */
typedef struct Capability {
    char     *name;
    char     *layer;       /**< Section/layer name from TOML. */
    CapEntry *entries;     /**< Linked list of key-value pairs. */
} Capability;

/** Load capabilities from a simple TOML file. */
Capability *capability_load(const char *path);

/** Get the capability name. */
const char *capability_get_name(const Capability *c);

/** Get the capability layer (section name). */
const char *capability_get_layer(const Capability *c);

/** Check whether a specific capability is provided. */
bool        capability_provides(const Capability *c, const char *cap_name);

/** Free a capability set. */
void        capability_free(Capability *c);

/* ── Agent State Machine ──────────────────────────────────────────────── */

typedef enum {
    AGENT_IDLE      = 0,
    AGENT_THINKING  = 1,
    AGENT_EXECUTING = 2,
    AGENT_LEARNING  = 3,
    AGENT_ERROR     = 4,
} AgentState;

/** Recommended action returned by agent_decide(). */
typedef enum {
    ACTION_NONE     = 0,
    ACTION_THINK    = 1,
    ACTION_EXECUTE  = 2,
    ACTION_LEARN    = 3,
    ACTION_RECOVER  = 4,
} AgentAction;

/** Opaque agent handle. */
typedef struct SiAgent SiAgent;

/** Create an agent with the given budget and capabilities (may be NULL). */
SiAgent    *agent_create(SiBudget *budget, Capability *caps);

/** Attempt a state transition; returns SI_OK or SI_ERR_TRANSITION. */
SiError     agent_transition(SiAgent *a, AgentState new_state);

/** Query current state. */
AgentState  agent_get_state(const SiAgent *a);

/** Decide on an action given a task description string. */
AgentAction agent_decide(SiAgent *a, const char *task);

/** Free an agent. */
void        agent_free(SiAgent *a);

/* ── Computational Cell ───────────────────────────────────────────────── */

/** Cell execution handler function pointer. */
typedef SiError (*CellHandler)(const void *input, size_t input_len,
                               void *output, size_t *output_len);

/** Opaque cell handle. */
typedef struct SiCell SiCell;

/** Create a computation cell with a name, budget (may be NULL), and handler. */
SiCell *cell_create(const char *name, SiBudget *budget, CellHandler handler);

/** Add a named dependency to the cell. */
SiError cell_add_dep(SiCell *cell, const char *dep_name);

/**
 * Execute the cell's handler, respecting budget constraints.
 * @param output_len  In/out: size of output buffer / actual bytes written.
 */
SiError cell_execute(SiCell *cell,
                     const void *input, size_t input_len,
                     void *output, size_t *output_len);

/** Free a cell. */
void    cell_free(SiCell *cell);

/* ─────────────────────────────────────────────────────────────────────── */

#ifdef __cplusplus
}
#endif

#endif /* SI_CORE_H */

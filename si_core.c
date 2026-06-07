/**
 * si_core.c — Implementation of the SuperInstance unified runtime C library.
 *
 * Conservation budgets, spectral ranking, capability discovery,
 * agent state machines, and computational cells.
 */

#define _POSIX_C_SOURCE 200809L
#include "si_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ── Version / Lifecycle ──────────────────────────────────────────────── */

static int si_initialised = 0;

const char *si_version(void)
{
    return "0.1.0";
}

void si_init(void)
{
    if (!si_initialised) {
        si_initialised = 1;
    }
}

void si_shutdown(void)
{
    si_initialised = 0;
}

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* ── Conservation Budget ──────────────────────────────────────────────── */

struct SiBudget {
    double C;        /* total capacity */
    double gamma;    /* productive */
    double eta;      /* waste */
};

SiBudget *budget_create(double C)
{
    if (C <= 0.0) return NULL;
    SiBudget *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->C = C;
    b->gamma = 0.0;
    b->eta   = 0.0;
    return b;
}

SiError budget_allocate(SiBudget *b, double gamma, double eta)
{
    if (!b) return SI_ERR_INVALID;
    if (gamma < 0.0 || eta < 0.0) return SI_ERR_INVALID;
    if (gamma + eta > b->C) return SI_ERR_BUDGET;
    b->gamma = gamma;
    b->eta   = eta;
    return SI_OK;
}

SiError budget_transfer(SiBudget *b, int from, int to, double amount)
{
    if (!b) return SI_ERR_INVALID;
    if (amount < 0.0) return SI_ERR_INVALID;
    if (from == to) return SI_OK;

    double *src, *dst;
    if (from == 0) { src = &b->gamma; } else if (from == 1) { src = &b->eta; }
    else { return SI_ERR_INVALID; }
    if (to == 0) { dst = &b->gamma; } else if (to == 1) { dst = &b->eta; }
    else { return SI_ERR_INVALID; }

    if (*src < amount) return SI_ERR_BUDGET;
    *src -= amount;
    *dst += amount;
    return SI_OK;
}

BudgetReport budget_audit(const SiBudget *b)
{
    BudgetReport r = {0};
    if (!b) return r;
    r.total_budget = b->C;
    r.gamma        = b->gamma;
    r.eta          = b->eta;
    r.unallocated  = b->C - b->gamma - b->eta;
    r.utilization  = (b->C > 0.0) ? b->gamma / b->C : 0.0;
    r.waste_ratio  = (b->C > 0.0) ? b->eta / b->C : 0.0;
    r.violation    = (b->gamma + b->eta > b->C) ? 1 : 0;
    return r;
}

SiError budget_to_json(const SiBudget *b, char *buf, size_t len)
{
    if (!b || !buf) return SI_ERR_INVALID;
    int n = snprintf(buf, len,
        "{\"C\":%.6f,\"gamma\":%.6f,\"eta\":%.6f}",
        b->C, b->gamma, b->eta);
    if (n < 0 || (size_t)n >= len) return SI_ERR_OVERFLOW;
    return SI_OK;
}

void budget_free(SiBudget *b)
{
    free(b);
}

/* ── Spectral Ranker ──────────────────────────────────────────────────── */

#define SPECTRAL_MAX_ITER  50
#define SPECTRAL_TOL       1e-12

SiError spectral_rank(const double *matrix, int n, double *out_ranks)
{
    if (!matrix || n < 1 || !out_ranks) return SI_ERR_INVALID;

    /* Work with a copy. */
    double *A = malloc((size_t)n * (size_t)n * sizeof(double));
    if (!A) return SI_ERR_NOMEM;
    memcpy(A, matrix, (size_t)n * (size_t)n * sizeof(double));

    /* Iterate deflation: find n eigenvalues via power iteration. */
    double *v  = malloc((size_t)n * sizeof(double));
    double *w  = malloc((size_t)n * sizeof(double));
    if (!v || !w) { free(A); free(v); free(w); return SI_ERR_NOMEM; }

    for (int k = 0; k < n; k++) {
        /* Initialize v to unit vector e_k. */
        for (int i = 0; i < n; i++) v[i] = (i == k) ? 1.0 : 0.0;

        double lambda = 0.0;
        for (int iter = 0; iter < SPECTRAL_MAX_ITER; iter++) {
            /* w = A * v */
            for (int i = 0; i < n; i++) {
                w[i] = 0.0;
                for (int j = 0; j < n; j++) {
                    w[i] += A[i * n + j] * v[j];
                }
            }
            lambda = 0.0;
            for (int i = 0; i < n; i++) lambda += w[i] * v[i];
            /* Normalise w. */
            double norm = 0.0;
            for (int i = 0; i < n; i++) norm += w[i] * w[i];
            norm = sqrt(norm);
            if (norm < SPECTRAL_TOL) { lambda = 0.0; break; }
            double prev_lambda = v[0]; /* for convergence */
            for (int i = 0; i < n; i++) v[i] = w[i] / norm;
            /* Check convergence. */
            if (iter > 0 && fabs(lambda - out_ranks[k < 1 ? 0 : 0]) < SPECTRAL_TOL)
                break;
            (void)prev_lambda;
        }
        out_ranks[k] = lambda;
        /* Deflate: A = A - lambda * v v^T */
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                A[i * n + j] -= lambda * v[i] * v[j];
    }

    free(A);
    free(v);
    free(w);
    return SI_OK;
}

SiError spectral_top_k(const double *matrix, int n, int k, int *out_indices)
{
    if (!matrix || n < 1 || k < 1 || k > n || !out_indices)
        return SI_ERR_INVALID;

    double *ranks = malloc((size_t)n * sizeof(double));
    if (!ranks) return SI_ERR_NOMEM;

    SiError err = spectral_rank(matrix, n, ranks);
    if (err != SI_OK) { free(ranks); return err; }

    /* Simple selection: find top-k by magnitude. */
    bool *used = calloc((size_t)n, sizeof(bool));
    for (int i = 0; i < k; i++) {
        int best = -1;
        double best_val = -1e308;
        for (int j = 0; j < n; j++) {
            if (!used[j] && fabs(ranks[j]) > best_val) {
                best_val = fabs(ranks[j]);
                best = j;
            }
        }
        out_indices[i] = best;
        if (best >= 0) used[best] = true;
    }
    free(used);
    free(ranks);
    return SI_OK;
}

double spectral_condition(const double *matrix, int n)
{
    if (!matrix || n < 1) return -1.0;

    double *ranks = malloc((size_t)n * sizeof(double));
    if (!ranks) return -1.0;

    SiError err = spectral_rank(matrix, n, ranks);
    if (err != SI_OK) { free(ranks); return -1.0; }

    double max_eig = 0.0, min_eig = 1e308;
    for (int i = 0; i < n; i++) {
        double mag = fabs(ranks[i]);
        if (mag > max_eig) max_eig = mag;
        if (mag < min_eig) min_eig = mag;
    }
    free(ranks);

    if (min_eig < SPECTRAL_TOL) return 1e308; /* near-singular */
    return max_eig / min_eig;
}

/* ── Capability Discovery ─────────────────────────────────────────────── */

static void cap_add_entry(Capability *c, const char *key, const char *value)
{
    CapEntry *e = malloc(sizeof(*e));
    if (!e) return;
    e->key   = strdup(key);
    e->value = strdup(value);
    e->next  = c->entries;
    c->entries = e;
}

Capability *capability_load(const char *path)
{
    if (!path) return NULL;
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    Capability *cap = calloc(1, sizeof(*cap));
    if (!cap) { fclose(fp); return NULL; }

    char line[1024];
    char current_section[256] = "";

    while (fgets(line, sizeof(line), fp)) {
        /* Strip newline. */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip empty / comment lines. */
        if (len == 0 || line[0] == '#') continue;

        /* Section header: [name]. */
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, line + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                /* Trim whitespace. */
                char *s = current_section;
                while (*s == ' ') s++;
                if (!cap->layer) {
                    cap->layer = strdup(s);
                }
            }
            continue;
        }

        /* key = "value" or key = 123. */
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = line;
        char *val = eq + 1;

        /* Trim key. */
        while (*key == ' ') key++;
        size_t klen = strlen(key);
        while (klen > 0 && key[klen-1] == ' ') key[--klen] = '\0';

        /* Trim val. */
        while (*val == ' ') val++;

        /* Strip quotes from value. */
        size_t vlen = strlen(val);
        if (vlen >= 2 && val[0] == '"' && val[vlen-1] == '"') {
            val[vlen-1] = '\0';
            val++;
        }

        cap_add_entry(cap, key, val);

        /* Use first 'name' key as the capability name. */
        if (strcmp(key, "name") == 0 && !cap->name) {
            cap->name = strdup(val);
        }
    }

    fclose(fp);
    return cap;
}

const char *capability_get_name(const Capability *c)
{
    return c ? c->name : NULL;
}

const char *capability_get_layer(const Capability *c)
{
    return c ? c->layer : NULL;
}

bool capability_provides(const Capability *c, const char *cap_name)
{
    if (!c || !cap_name) return false;
    for (CapEntry *e = c->entries; e; e = e->next) {
        if (strcmp(e->key, cap_name) == 0) return true;
        if (strcmp(e->value, cap_name) == 0) return true;
    }
    return false;
}

void capability_free(Capability *c)
{
    if (!c) return;
    free(c->name);
    free(c->layer);
    CapEntry *e = c->entries;
    while (e) {
        CapEntry *next = e->next;
        free(e->key);
        free(e->value);
        free(e);
        e = next;
    }
    free(c);
}

/* ── Agent State Machine ──────────────────────────────────────────────── */

/* Valid transitions table: valid[from][to]. */
static const bool valid_transition[5][5] = {
    /*            IDLE   THINK  EXEC   LEARN  ERROR */
    /* IDLE   */ {false, true,  false, false, true },
    /* THINK  */ {false, false, true,  false, true },
    /* EXEC   */ {false, false, false, true,  true },
    /* LEARN  */ {true,  false, false, false, true },
    /* ERROR  */ {true,  false, false, false, false},
};

struct SiAgent {
    SiBudget    *budget;
    Capability  *caps;
    AgentState   state;
};

SiAgent *agent_create(SiBudget *budget, Capability *caps)
{
    SiAgent *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->budget = budget;
    a->caps   = caps;
    a->state  = AGENT_IDLE;
    return a;
}

SiError agent_transition(SiAgent *a, AgentState new_state)
{
    if (!a) return SI_ERR_INVALID;
    if (new_state < AGENT_IDLE || new_state > AGENT_ERROR) return SI_ERR_INVALID;
    if (!valid_transition[a->state][new_state]) return SI_ERR_TRANSITION;
    a->state = new_state;
    return SI_OK;
}

AgentState agent_get_state(const SiAgent *a)
{
    return a ? a->state : AGENT_ERROR;
}

AgentAction agent_decide(SiAgent *a, const char *task)
{
    if (!a) return ACTION_NONE;

    /* Simple heuristic decision based on current state and task content. */
    switch (a->state) {
    case AGENT_IDLE:
        return (task && strlen(task) > 0) ? ACTION_THINK : ACTION_NONE;
    case AGENT_THINKING:
        return ACTION_EXECUTE;
    case AGENT_EXECUTING:
        return ACTION_LEARN;
    case AGENT_LEARNING:
        return ACTION_NONE;
    case AGENT_ERROR:
        return ACTION_RECOVER;
    }
    return ACTION_NONE;
}

void agent_free(SiAgent *a)
{
    free(a);
}

/* ── Computational Cell ───────────────────────────────────────────────── */

#define CELL_MAX_DEPS 32

struct SiCell {
    char        *name;
    SiBudget    *budget;
    CellHandler  handler;
    char        *deps[CELL_MAX_DEPS];
    int          dep_count;
};

SiCell *cell_create(const char *name, SiBudget *budget, CellHandler handler)
{
    if (!name || !handler) return NULL;
    SiCell *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->name    = strdup(name);
    c->budget  = budget;
    c->handler = handler;
    c->dep_count = 0;
    return c;
}

SiError cell_add_dep(SiCell *cell, const char *dep_name)
{
    if (!cell || !dep_name) return SI_ERR_INVALID;
    if (cell->dep_count >= CELL_MAX_DEPS) return SI_ERR_OVERFLOW;
    cell->deps[cell->dep_count] = strdup(dep_name);
    cell->dep_count++;
    return SI_OK;
}

SiError cell_execute(SiCell *cell,
                     const void *input, size_t input_len,
                     void *output, size_t *output_len)
{
    if (!cell || !cell->handler) return SI_ERR_INVALID;

    /* Budget enforcement: require gamma > 0. */
    if (cell->budget) {
        BudgetReport rpt = budget_audit(cell->budget);
        if (rpt.gamma <= 0.0) return SI_ERR_BUDGET;
    }

    return cell->handler(input, input_len, output, output_len);
}

void cell_free(SiCell *cell)
{
    if (!cell) return;
    free(cell->name);
    for (int i = 0; i < cell->dep_count; i++)
        free(cell->deps[i]);
    free(cell);
}

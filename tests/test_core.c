/**
 * tests/test_core.c — Comprehensive tests for si_core.
 *
 * Compile with: gcc -Wall -Wextra -std=c99 -O2 -I.. -o test_core test_core.c ../si_core.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>

#include "../si_core.h"

#define ABS_TOL 1e-9
#define ASSERT_FEQ(a, b) do { \
    assert(fabs((a) - (b)) < ABS_TOL && "float mismatch"); \
} while (0)

#define ASSERT_FEQ_EPS(a, b, eps) do { \
    assert(fabs((a) - (b)) < (eps) && "float mismatch (eps)"); \
} while (0)

static int tests_run = 0;
static int tests_passed = 0;

/* Simple wrapper to run a test and track pass/fail. */
static void run_test(void (*fn)(void), const char *name)
{
    tests_run++;
    printf("  %-45s ", name);
    fflush(stdout);
    fn();
    tests_passed++;
    printf("PASS\n");
}

/* ── Budget Tests ─────────────────────────────────────────────────────── */

static void test_budget_create_allocate(void)
{
    SiBudget *b = budget_create(100.0);
    assert(b != NULL);

    SiError err = budget_allocate(b, 60.0, 30.0);
    assert(err == SI_OK);

    BudgetReport r = budget_audit(b);
    ASSERT_FEQ(r.total_budget, 100.0);
    ASSERT_FEQ(r.gamma, 60.0);
    ASSERT_FEQ(r.eta, 30.0);
    ASSERT_FEQ(r.unallocated, 10.0);
    ASSERT_FEQ(r.utilization, 0.6);
    ASSERT_FEQ(r.waste_ratio, 0.3);
    assert(r.violation == 0);

    /* Over-allocation should fail. */
    err = budget_allocate(b, 70.0, 40.0);
    assert(err == SI_ERR_BUDGET);

    budget_free(b);
}

static void test_budget_transfer(void)
{
    SiBudget *b = budget_create(100.0);
    budget_allocate(b, 60.0, 30.0);

    /* Transfer 10 from gamma to eta. */
    SiError err = budget_transfer(b, 0, 1, 10.0);
    assert(err == SI_OK);

    BudgetReport r = budget_audit(b);
    ASSERT_FEQ(r.gamma, 50.0);
    ASSERT_FEQ(r.eta, 40.0);
    ASSERT_FEQ(r.unallocated, 10.0); /* conservation: total unchanged */

    /* Transfer more than available should fail. */
    err = budget_transfer(b, 0, 1, 60.0);
    assert(err == SI_ERR_BUDGET);

    budget_free(b);
}

static void test_budget_audit(void)
{
    SiBudget *b = budget_create(200.0);
    budget_allocate(b, 120.0, 50.0);

    BudgetReport r = budget_audit(b);
    ASSERT_FEQ(r.total_budget, 200.0);
    ASSERT_FEQ(r.unallocated, 30.0);
    ASSERT_FEQ(r.utilization, 0.6);
    ASSERT_FEQ(r.waste_ratio, 0.25);
    assert(r.violation == 0);

    /* JSON serialization. */
    char json[256];
    SiError err = budget_to_json(b, json, sizeof(json));
    assert(err == SI_OK);
    assert(strstr(json, "\"C\":") != NULL);
    assert(strstr(json, "\"gamma\":") != NULL);
    assert(strstr(json, "\"eta\":") != NULL);

    budget_free(b);
}

/* ── Spectral Tests ───────────────────────────────────────────────────── */

static void test_spectral_rank(void)
{
    /* Symmetric 3×3 matrix with known eigenvalue structure.
       [[2, 1, 0],
        [1, 2, 1],
        [0, 1, 2]]  */
    double matrix[] = {
        2.0, 1.0, 0.0,
        1.0, 2.0, 1.0,
        0.0, 1.0, 2.0
    };

    double ranks[3] = {0};
    SiError err = spectral_rank(matrix, 3, ranks);
    assert(err == SI_OK);

    /* All eigenvalues should be positive for this PD matrix. */
    for (int i = 0; i < 3; i++) {
        assert(ranks[i] > -ABS_TOL);
    }

    /* Top eigenvalue should be ≈ 2 + sqrt(2) ≈ 3.414. */
    double max_eig = 0.0;
    for (int i = 0; i < 3; i++)
        if (fabs(ranks[i]) > max_eig) max_eig = fabs(ranks[i]);
    ASSERT_FEQ_EPS(max_eig, 2.0 + sqrt(2.0), 0.1);
}

static void test_spectral_condition(void)
{
    /* Identity matrix: condition number ≈ 1.0. */
    double identity[] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    double cond = spectral_condition(identity, 3);
    ASSERT_FEQ_EPS(cond, 1.0, 0.01);

    /* Ill-conditioned matrix. */
    double ill[] = {
        1e6, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1e-6
    };
    cond = spectral_condition(ill, 3);
    assert(cond > 1e6);
}

/* ── Capability Tests ─────────────────────────────────────────────────── */

static const char *TEMP_TOML_PATH = "/tmp/si_test_cap.toml";

static void write_temp_toml(void)
{
    FILE *fp = fopen(TEMP_TOML_PATH, "w");
    assert(fp);
    fprintf(fp,
        "[inference]\n"
        "name = \"text-generator\"\n"
        "model = \"gpt-small\"\n"
        "max_tokens = 2048\n"
        "streaming = \"true\"\n"
    );
    fclose(fp);
}

static void test_capability_load(void)
{
    write_temp_toml();
    Capability *c = capability_load(TEMP_TOML_PATH);
    assert(c != NULL);

    assert(strcmp(capability_get_name(c), "text-generator") == 0);
    assert(strcmp(capability_get_layer(c), "inference") == 0);

    capability_free(c);
    unlink(TEMP_TOML_PATH);
}

static void test_capability_provides(void)
{
    write_temp_toml();
    Capability *c = capability_load(TEMP_TOML_PATH);
    assert(c != NULL);

    assert(capability_provides(c, "model") == true);
    assert(capability_provides(c, "streaming") == true);
    assert(capability_provides(c, "nonexistent") == false);

    capability_free(c);
    unlink(TEMP_TOML_PATH);
}

/* ── Agent Tests ──────────────────────────────────────────────────────── */

static void test_agent_lifecycle(void)
{
    SiBudget *b = budget_create(100.0);
    SiAgent *a = agent_create(b, NULL);
    assert(a != NULL);
    assert(agent_get_state(a) == AGENT_IDLE);

    /* IDLE -> THINKING. */
    assert(agent_transition(a, AGENT_THINKING) == SI_OK);
    assert(agent_get_state(a) == AGENT_THINKING);

    /* THINKING -> EXECUTING. */
    assert(agent_transition(a, AGENT_EXECUTING) == SI_OK);
    assert(agent_get_state(a) == AGENT_EXECUTING);

    /* EXECUTING -> LEARNING. */
    assert(agent_transition(a, AGENT_LEARNING) == SI_OK);
    assert(agent_get_state(a) == AGENT_LEARNING);

    /* LEARNING -> IDLE. */
    assert(agent_transition(a, AGENT_IDLE) == SI_OK);
    assert(agent_get_state(a) == AGENT_IDLE);

    /* Any -> ERROR. */
    assert(agent_transition(a, AGENT_ERROR) == SI_OK);
    assert(agent_get_state(a) == AGENT_ERROR);

    /* ERROR -> IDLE. */
    assert(agent_transition(a, AGENT_IDLE) == SI_OK);
    assert(agent_get_state(a) == AGENT_IDLE);

    agent_free(a);
    budget_free(b);
}

static void test_agent_invalid_transition(void)
{
    SiBudget *b = budget_create(50.0);
    SiAgent *a = agent_create(b, NULL);

    /* IDLE -> EXECUTING is invalid. */
    assert(agent_transition(a, AGENT_EXECUTING) == SI_ERR_TRANSITION);
    assert(agent_get_state(a) == AGENT_IDLE);

    /* IDLE -> LEARNING is invalid. */
    assert(agent_transition(a, AGENT_LEARNING) == SI_ERR_TRANSITION);

    agent_free(a);
    budget_free(b);
}

/* ── Cell Tests ───────────────────────────────────────────────────────── */

static SiError dummy_handler(const void *input, size_t input_len,
                              void *output, size_t *output_len)
{
    (void)input_len;
    /* Echo input to output (as string). */
    const char *in = (const char *)input;
    char *out = (char *)output;
    size_t len = strlen(in);
    if (len + 1 > *output_len) return SI_ERR_OVERFLOW;
    memcpy(out, in, len + 1);
    *output_len = len + 1;
    return SI_OK;
}

static void test_cell_create_execute(void)
{
    SiCell *cell = cell_create("echo", NULL, dummy_handler);
    assert(cell != NULL);

    char output[256];
    size_t out_len = sizeof(output);
    SiError err = cell_execute(cell, "hello", 5, output, &out_len);
    assert(err == SI_OK);
    assert(strcmp(output, "hello") == 0);
    assert(out_len == 6);

    cell_free(cell);
}

static void test_cell_dependencies(void)
{
    SiCell *cell = cell_create("compute", NULL, dummy_handler);
    assert(cell != NULL);

    assert(cell_add_dep(cell, "data-fetch") == SI_OK);
    assert(cell_add_dep(cell, "preprocess") == SI_OK);
    assert(cell_add_dep(cell, "normalize") == SI_OK);

    /* Execute still works (deps are metadata). */
    char output[64];
    size_t out_len = sizeof(output);
    assert(cell_execute(cell, "x", 1, output, &out_len) == SI_OK);

    cell_free(cell);
}

static void test_cell_budget_enforcement(void)
{
    SiBudget *b = budget_create(100.0);
    /* Allocate everything to waste, leaving gamma = 0. */
    budget_allocate(b, 0.0, 100.0);

    SiCell *cell = cell_create("blocked", b, dummy_handler);
    assert(cell != NULL);

    char output[64];
    size_t out_len = sizeof(output);
    SiError err = cell_execute(cell, "test", 4, output, &out_len);
    assert(err == SI_ERR_BUDGET);

    cell_free(cell);
    budget_free(b);
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    si_init();
    printf("si_core test suite\n");
    printf("═══════════════════════════════════════════════════\n");

    run_test(test_budget_create_allocate,      "budget: create & allocate");
    run_test(test_budget_transfer,             "budget: transfer & conservation");
    run_test(test_budget_audit,                "budget: audit & JSON");
    run_test(test_spectral_rank,               "spectral: rank 3×3 matrix");
    run_test(test_spectral_condition,          "spectral: condition number");
    run_test(test_capability_load,             "capability: load TOML");
    run_test(test_capability_provides,         "capability: provides check");
    run_test(test_agent_lifecycle,             "agent: full lifecycle");
    run_test(test_agent_invalid_transition,    "agent: invalid transition");
    run_test(test_cell_create_execute,         "cell: create & execute");
    run_test(test_cell_dependencies,           "cell: dependencies");
    run_test(test_cell_budget_enforcement,     "cell: budget enforcement");

    printf("═══════════════════════════════════════════════════\n");
    printf("Results: %d / %d passed\n", tests_passed, tests_run);

    si_shutdown();
    return (tests_passed == tests_run) ? 0 : 1;
}

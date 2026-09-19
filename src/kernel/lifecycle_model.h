/* The event model: which events can physically occur in which state.
 *
 * It is written from what exists in each state (a live leader, a step timer,
 * a grace timer, a job timer), and it never reads the lifecycle table. The
 * tests explore the table with it. Only the tests link it. */
#ifndef QWE_KERNEL_LIFECYCLE_MODEL_H
#define QWE_KERNEL_LIFECYCLE_MODEL_H

#include "src/kernel/lifecycle.h"

/* A bit set: bit (1u << e) is set when event e can occur in state s. */
unsigned qwe_lcm_events(enum qwe_lc_state s);

#endif

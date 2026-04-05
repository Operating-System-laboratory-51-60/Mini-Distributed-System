/* load_balancer.h — Worker selection algorithm (SERVER side)
 */
#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

/* find_availabe_worker: two-tier selection.
 * Tier 1: Return first worker with load_percent < 50%  (ideal)
 * Tier 2: If all >= 50%, return least-loaded worker    (fallback)
 * Skips workers that have has_task == 1 (already busy).
 * Returns slot index 0..MAX_WORKERS-1, or -1 if none available.
 * NOTE: name kept as-is (typo from original) for git consistency. */
int find_availabe_worker();

#endif

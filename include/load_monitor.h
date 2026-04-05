/* load_monitor.h — Calculate and report load (WORKER side)
 */
#ifndef LOAD_MONITOR_H
#define LOAD_MONITOR_H

/* Record that a task has started/ended to track load. */
void record_task(int duration);

/* Calculates real load % based on the last 2 minutes */
int calculate_load();

#endif

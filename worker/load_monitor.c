/* load_monitor.c — Calculate and report load (WORKER side)
 * Logic extracted from worker.c load tracking section.
 */
#include "common.h"
#include "load_monitor.h"

#define HISTORY_SIZE 20 // store last 20 task records

typedef struct {
    time_t start_time; // When did this task start?
    int duration;      // How many seconds did it run?
} TaskRecord;

static TaskRecord history[HISTORY_SIZE];
static int history_count = 0;

void record_task(int duration)
{
    if(history_count < HISTORY_SIZE)
    {
        history[history_count].start_time = time(NULL);
        history[history_count].duration = duration;
        history_count++;
    }
}

// Calculates real load based on last 2 minutes
int calculate_load()
{
    time_t now = time(NULL);
    int busy_seconds = 0;
    time_t window_start = now - 120; // 2 minutes ago

    for(int i=0; i<history_count; i++)
    {
        time_t task_end = history[i].start_time + history[i].duration;

        // Only count tasks that overlapped with our 2-minute window
        if(task_end > window_start)
        {
            time_t overlap_start = history[i].start_time > window_start ? history[i].start_time : window_start;
            time_t overlap_end = task_end < now ? task_end : now;
            busy_seconds += (int)(overlap_end - overlap_start);
        }
    }
    // cap at 100% and calculate percentage
    if(busy_seconds > 120) busy_seconds = 120;
    return (busy_seconds * 100) / 120;
}
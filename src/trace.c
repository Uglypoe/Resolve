#include "trace.h"
#include "heap.h"
#include "queue.h"
#include "mutex.h"
#include "fs.h"
#include "timer.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct trace_event_t
{
	uint64_t ticks;
	const char* name;
	int pid;
	int tid;
	char ph;
} trace_event_t;

typedef struct trace_t
{
	heap_t* heap;
	fs_t* fs;
	mutex_t* mutex;
	queue_t* durations;
	trace_event_t* trace_logs;
	const char* path;
	int capacity;
	int trace_logs_count;
	bool enabled;
} trace_t;

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->fs = fs_create(heap, 1);
	trace->mutex = mutex_create();
	trace->durations = queue_create(heap, event_capacity);
	trace->trace_logs = heap_alloc(trace->heap, sizeof(trace_event_t) * event_capacity * 2, 8);
	trace->path = NULL;
	trace->capacity = event_capacity;
	trace->trace_logs_count = 0;
	trace->enabled = false;
	return trace;
}

void trace_destroy(trace_t* trace)
{
	fs_destroy(trace->fs);
	mutex_destroy(trace->mutex);
	queue_push(trace->durations, NULL);
	queue_destroy(trace->durations);
	heap_free(trace->heap, trace->trace_logs);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	if (trace->enabled == false)
	{
		return;
	}

	mutex_lock(trace->mutex);
	if (trace->trace_logs_count < trace->capacity)
	{
		trace_event_t* trace_event_begin = (trace->trace_logs + trace->trace_logs_count * sizeof(trace_event_t*));
		trace_event_begin->name = name;
		trace_event_begin->ph = 'B';
		trace_event_begin->pid = GetCurrentProcessId();
		trace_event_begin->tid = GetCurrentThreadId();
		trace_event_begin->ticks = timer_get_ticks();
		queue_push(trace->durations, trace_event_begin);
		trace->trace_logs_count++;
	}
	mutex_unlock(trace->mutex);
}

void trace_duration_pop(trace_t* trace)
{
	if (trace->enabled == false)
	{
		return;
	}

	mutex_lock(trace->mutex);
	trace_event_t* trace_event_begin = queue_pop(trace->durations);
	if (trace->trace_logs_count < trace->capacity)
	{
		trace_event_t* trace_event_end = (trace->trace_logs + trace->trace_logs_count * sizeof(trace_event_t*));
		trace_event_end->name = trace_event_begin->name;
		trace_event_end->ph = 'E';
		trace_event_end->pid = trace_event_begin->pid;
		trace_event_end->tid = trace_event_begin->tid;
		trace_event_end->ticks = timer_get_ticks();
		trace->trace_logs_count++;
	}
	mutex_unlock(trace->mutex);
}

void trace_capture_start(trace_t* trace, const char* path)
{
	if (trace->enabled == true)
	{
		return;
	}

	trace->enabled = true;
	trace->path = path;
}

int format_output(trace_t* trace, char* dest, int size)
{
	int len = snprintf(dest, size, "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\": [\n");
	for (int i = 0; i < trace->trace_logs_count; i++)
	{
		trace_event_t* trace_event = (trace->trace_logs + i * sizeof(trace_event_t*));
		char* concat_dest = dest != NULL ? dest + len : dest;
		int remaining_size = size > 0 ? size - len : size;
		len += snprintf(concat_dest, remaining_size,
			"\t\t{\"name\":\"%s\",\"ph\":\"%c\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%jd\"}%s",
			trace_event->name, trace_event->ph, trace_event->pid, trace_event->tid, timer_ticks_to_us(trace_event->ticks),
			i < trace->trace_logs_count - 1 ? ",\n" : "\n\t]\n}");
	}
	return len;
}

void trace_capture_stop(trace_t* trace)
{
	if (trace->enabled == false)
	{
		return;
	}

	trace->enabled = false;

	int alloc_size = 1 + format_output(trace, NULL, 0); // extra space for null terminating byte

	char* buffer = heap_alloc(trace->heap, alloc_size, 8);
	int len = format_output(trace, buffer, alloc_size);

	fs_work_t* write_work = fs_write(trace->fs, trace->path, buffer, len, false);
	fs_work_wait(write_work);
	heap_free(trace->heap, buffer);
	heap_free(trace->heap, write_work);
}

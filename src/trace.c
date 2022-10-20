#include "trace.h"
#include "heap.h"
#include "mutex.h"
#include "fs.h"
#include "timer.h"
#include "atomic.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct thread_stack_t
{
	struct thread_stack_t* next;
	struct trace_event_t* durations;
	int duration_count;
	int tid;
} thread_stack_t;

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
	thread_stack_t* thread_stacks;
	trace_event_t* trace_logs;
	mutex_t* add_thread_mutex;
	const char* path;
	int capacity;
	int trace_logs_count;
	bool enabled;
} trace_t;

thread_stack_t* get_thread_stack(trace_t* trace, thread_stack_t* head, int tid)
{
	thread_stack_t* last = NULL;
	thread_stack_t* this = head;
	while (this != NULL)
	{
		if (this->tid == tid)
		{
			break;
		}
		last = this;
		this = this->next;
	}

	if (this == NULL)
	{
		this = heap_alloc(trace->heap, sizeof(thread_stack_t), 8);
		this->next = NULL;
		this->durations = heap_alloc(trace->heap, trace->capacity * sizeof(trace_event_t), 8);
		this->duration_count = 0;
		this->tid = tid;

		mutex_lock(trace->add_thread_mutex);
		if (trace->thread_stacks == NULL)
		{
			trace->thread_stacks = this;
		}
		else
		{
			thread_stack_t* itr = trace->thread_stacks;
			while (itr->next != NULL)
			{
				itr = itr->next;
			}
			itr->next = this;
		}
		mutex_unlock(trace->add_thread_mutex);
	}

	return this;
}

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->fs = fs_create(heap, 1);
	trace->thread_stacks = NULL;
	trace->trace_logs = heap_alloc(trace->heap, sizeof(trace_event_t) * event_capacity * 2, 8);
	trace->add_thread_mutex = mutex_create();
	trace->path = NULL;
	trace->capacity = event_capacity;
	trace->trace_logs_count = 0;
	trace->enabled = false;
	return trace;
}

void trace_destroy(trace_t* trace)
{
	fs_destroy(trace->fs);
	thread_stack_t* this = trace->thread_stacks;
	while (this != NULL)
	{
		thread_stack_t* next = this->next;
		heap_free(trace->heap, this->durations);
		heap_free(trace->heap, this);
		this = next;
	}
	mutex_destroy(trace->add_thread_mutex);
	heap_free(trace->heap, trace->trace_logs);
	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	if (trace->enabled == false)
	{
		return;
	}

	thread_stack_t* thread_stack = get_thread_stack(trace, trace->thread_stacks, GetCurrentThreadId());

	if (thread_stack->duration_count == trace->capacity)
	{
		return;
	}

	int dur_index = thread_stack->duration_count++;
	trace_event_t* duration = (thread_stack->durations + dur_index * sizeof(trace_event_t*));
	duration->name = name;
	duration->ph = 'B';
	duration->pid = GetCurrentProcessId();
	duration->tid = GetCurrentThreadId();
	duration->ticks = timer_get_ticks();

	int index = atomic_increment(&trace->trace_logs_count);
	if (index < trace->capacity)
	{
		trace_event_t* trace_event_begin = (trace->trace_logs + index * sizeof(trace_event_t*));
		trace_event_begin->name = name;
		trace_event_begin->ph = 'B';
		trace_event_begin->pid = GetCurrentProcessId();
		trace_event_begin->tid = GetCurrentThreadId();
		trace_event_begin->ticks = duration->ticks;
	}
}

void trace_duration_pop(trace_t* trace)
{
	if (trace->enabled == false)
	{
		return;
	}

	thread_stack_t* thread_stack = get_thread_stack(trace, trace->thread_stacks, GetCurrentThreadId());

	int dur_index = --thread_stack->duration_count;
	trace_event_t* trace_event_begin = (thread_stack->durations + dur_index * sizeof(trace_event_t*));

	int index = atomic_increment(&trace->trace_logs_count);
	if (index < trace->capacity)
	{
		trace_event_t* trace_event_end = (trace->trace_logs + index * sizeof(trace_event_t*));
		trace_event_end->name = trace_event_begin->name;
		trace_event_end->ph = 'E';
		trace_event_end->pid = trace_event_begin->pid;
		trace_event_end->tid = trace_event_begin->tid;
		trace_event_end->ticks = timer_get_ticks();
	}
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
	int num_logs = trace->trace_logs_count < trace->capacity * 2 ? trace->trace_logs_count : trace->capacity * 2;
	for (int i = 0; i < num_logs; i++)
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

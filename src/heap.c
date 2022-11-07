#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#define FUNCTION_NAME_LENGTH 64
#define MAX_STACK_DEPTH 16

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	mutex_t* mutex;
} heap_t;

void print_backtrace(DWORD64* stack)
{
	char* fn_name = VirtualAlloc(NULL, FUNCTION_NAME_LENGTH * sizeof(char),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!fn_name)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return;
	}

	HANDLE process = GetCurrentProcess();
	if (!SymInitialize(process, NULL, TRUE))
	{
		DWORD error = GetLastError();
		printf("SymInitialize returned error : %d\n", error);
	}

	char buf[sizeof(SYMBOL_INFO) + (FUNCTION_NAME_LENGTH + 1) * sizeof(TCHAR)];
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)buf;
	symbol->MaxNameLen = FUNCTION_NAME_LENGTH;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	int i = 0;
	for (i = 0; i < MAX_STACK_DEPTH; ++i) {
		DWORD64* address = (DWORD64*)(stack + i);
		if (SymFromAddr(process, *address, 0, symbol))
		{
			printf("[%d] %s\n", i, symbol->Name);
		}
		else
		{
			break;
		}

		if (strcmp(symbol->Name, "main") == 0)
		{
			break;
		}
	}

	SymCleanup(process);

	VirtualFree(fn_name, 0, MEM_RELEASE);
}

heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->mutex = mutex_create();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	mutex_lock(heap->mutex);

	size_t padding = alignment - (size % alignment);
	size_t padded_size = size + padding;
	void* address = tlsf_memalign(heap->tlsf, alignment, padded_size + MAX_STACK_DEPTH * sizeof(void*));
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, padded_size * 2) +
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, padded_size + MAX_STACK_DEPTH * sizeof(void*));
	}

	if (address)
	{
		void* stack = (void*)((char*)address + padded_size);
		CaptureStackBackTrace(2, MAX_STACK_DEPTH, stack, NULL);
	}
	
	mutex_unlock(heap->mutex);

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	mutex_lock(heap->mutex);
	tlsf_free(heap->tlsf, address);
	mutex_unlock(heap->mutex);
}

static void memory_leak_walker(char* ptr, size_t size, int used, void* user)
{
	if (used)
	{
		void* callstack = (void*)(ptr + size - (MAX_STACK_DEPTH * sizeof(void*)));
		if (!callstack)
		{
			return;
		}
		printf("Memory leak of size %zu bytes with callstack:\n", size);
		print_backtrace(callstack);
	}
}

void heap_destroy(heap_t* heap)
{
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		tlsf_walk_pool(arena->pool, memory_leak_walker, NULL);
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	mutex_destroy(heap->mutex);

	VirtualFree(heap, 0, MEM_RELEASE);
}

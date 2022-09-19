#include "heap.h"

#include "debug.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#define FUNCTION_NAME_LENGTH 64
#define MAX_STACK_DEPTH 16

typedef struct callstack_t
{
	char** stack;
	int size;
	struct callstack_t* next;
} callstack_t;

typedef struct arena_t
{
	pool_t pool;
	callstack_t* head_callstack;
	callstack_t* tail_callstack;
	struct arena_t* next;
} arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
} heap_t;

void get_backtrace(arena_t* arena)
{
	void* stack[MAX_STACK_DEPTH];
	char** str_stack = VirtualAlloc(NULL, MAX_STACK_DEPTH * sizeof(char*),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); 
	if (!str_stack)
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

	WORD callstack_size = CaptureStackBackTrace(2, MAX_STACK_DEPTH, stack, NULL);

	char buf[sizeof(SYMBOL_INFO) + (FUNCTION_NAME_LENGTH + 1) * sizeof(TCHAR)];
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)buf;
	symbol->MaxNameLen = FUNCTION_NAME_LENGTH;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	int i;
	for (i = 0; i < callstack_size; ++i) {
		str_stack[i] = VirtualAlloc(NULL, FUNCTION_NAME_LENGTH * sizeof(char),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!str_stack[i])
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return;
		}

		DWORD64 address = (DWORD64)(stack[i]);
		if (SymFromAddr(process, address, 0, symbol))
		{
			sprintf_s(str_stack[i], FUNCTION_NAME_LENGTH, "%s", symbol->Name);
		}
		else
		{
			DWORD error = GetLastError();
			sprintf_s(str_stack[i], FUNCTION_NAME_LENGTH, "(SymFromAddr failed with error %d)", error);
		}

		if (strcmp(symbol->Name, "main") == 0)
		{
			break;
		}
	}

	SymCleanup(process);

	callstack_t* callstack = VirtualAlloc(NULL, sizeof(callstack_t),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!callstack)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return;
	}

	callstack->stack = str_stack;
	callstack->size = i + 1;

	if (!arena->head_callstack)
	{
		arena->head_callstack = callstack;
		arena->tail_callstack = callstack;
	}
	else
	{
		arena->tail_callstack->next = callstack;
		arena->tail_callstack = callstack;
	}
}

void print_backtrace(char** stack, int size)
{
	for (int i = 0; i < size; ++i) {
		printf("[%d] %s\n", i, stack[i]);
		VirtualFree(stack[i], 0, MEM_RELEASE);
	}
	VirtualFree(stack, 0, MEM_RELEASE);
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

	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	void* address = tlsf_memalign(heap->tlsf, alignment, size);
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) +
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

		address = tlsf_memalign(heap->tlsf, alignment, size);
	}

	get_backtrace(heap->arena);

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	tlsf_free(heap->tlsf, address);
}

void memory_leak_walker(void* ptr, size_t size, int used, arena_t* user)
{
	callstack_t* callstack = user->head_callstack;
	if (!callstack)
	{
		return;
	}
	if (used)
	{
		printf("Memory leak of size %zu bytes with callstack:\n", size);
		print_backtrace(callstack->stack, callstack->size);
	}
	user->head_callstack = callstack->next;
	VirtualFree(callstack, 0, MEM_RELEASE);
}

void heap_destroy(heap_t* heap)
{
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		tlsf_walk_pool(arena->pool, memory_leak_walker, arena);
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	VirtualFree(heap, 0, MEM_RELEASE);
}

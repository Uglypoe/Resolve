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
	int size;
	size_t bytes;
	char** stack;
} callstack_t;

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
} heap_t;

void print_backtrace(char** stack, int size)
{
	for (int i = 0; i < size; ++i) {
		printf("[%d] %s\n", i, stack[i]);
		VirtualFree(stack[i], 0, MEM_RELEASE);
	}
	VirtualFree(stack, 0, MEM_RELEASE);
}

void get_backtrace(callstack_t* callstack, size_t bytes)
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

	callstack->stack = str_stack;
	callstack->size = i + 1;
	callstack->bytes = bytes;
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
	size_t padding = alignment - (size % alignment);
	size_t padded_size = size + padding;

	void* address = tlsf_memalign(heap->tlsf, alignment, padded_size + sizeof(callstack_t));
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

		address = tlsf_memalign(heap->tlsf, alignment, padded_size + sizeof(callstack_t));
	}

	callstack_t* callstack = (callstack_t*)((char*)address + padded_size);
	get_backtrace(callstack, size);

	return address;
}

void heap_free(heap_t* heap, void* address)
{
	tlsf_free(heap->tlsf, address);
}

static void memory_leak_walker(char* ptr, size_t size, int used, void* user)
{
	callstack_t* callstack = (callstack_t*)(ptr + size - sizeof(callstack_t));
	if (!callstack)
	{
		return;
	}
	if (used)
	{
		printf("Memory leak of size %zu bytes with callstack:\n", callstack->bytes);
		print_backtrace(callstack->stack, callstack->size);
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

	VirtualFree(heap, 0, MEM_RELEASE);
}

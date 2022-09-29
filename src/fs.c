#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "lz4/lz4.h"
#include "debug.h"
#include "stdio.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fs_t
{
	heap_t* heap;
	queue_t* file_queue;
	thread_t* file_thread;
	queue_t* compression_queue;
	thread_t* compression_thread;
} fs_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef struct fs_work_t
{
	fs_t* fs;
	heap_t* heap;
	fs_work_op_t op;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	void* buffer;
	size_t size;
	void* temp_buffer;
	size_t temp_size;
	event_t* done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int compression_thread_func(void* user);

fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->file_thread = thread_create(file_thread_func, fs);
	fs->compression_queue = queue_create(heap, queue_capacity);
	fs->compression_thread = thread_create(compression_thread_func, fs);
	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	thread_destroy(fs->file_thread);
	queue_destroy(fs->file_queue);
	queue_push(fs->compression_queue, NULL);
	thread_destroy(fs->compression_thread);
	queue_destroy(fs->compression_queue);
	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->fs = fs;
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->fs = fs;
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression)
	{
		// HOMEWORK 2: Queue file write work on compression queue!
		queue_push(fs->compression_queue, work);
	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		if (work->use_compression)
		{
			heap_free(work->heap, work->temp_buffer);
		}
		heap_free(work->heap, work);
	}
}

static void file_read(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	work->size = bytes_read;
	if (work->null_terminate)
	{
		((char*)work->buffer)[bytes_read] = 0;
	}

	CloseHandle(handle);

	if (work->use_compression)
	{
		// HOMEWORK 2: Queue file read work on decompression queue!
		queue_push(work->fs->compression_queue, work);
	}
	else
	{
		event_signal(work->done);
	}
}

static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		return;
	}

	void* buffer = (work->use_compression ? work->temp_buffer : work->buffer);
	size_t size = (work->use_compression ? work->temp_size : work->size);

	DWORD bytes_written = 0;
	if (!WriteFile(handle, buffer, (DWORD)size, &bytes_written, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		return;
	}

	if (work->use_compression)
	{
		work->temp_size = bytes_written;
	}
	else
	{
		work->size = bytes_written;
	}

	CloseHandle(handle);

	event_signal(work->done);
}

static int file_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL)
		{
			break;
		}

		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(work);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

static int compression_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->compression_queue);
		if (work == NULL)
		{
			break;
		}

		switch (work->op)
		{
		case k_fs_work_op_read:
			{
				work->temp_buffer = work->buffer;
				work->temp_size = work->size;

				int decompressed_size = atoi(work->temp_buffer); // Stops at the newline
				int metadata_len = snprintf(NULL, 0, "%d\n", decompressed_size);

				char* compressed_buffer = (char*)work->temp_buffer + metadata_len;
				char* decompressed_buffer = heap_alloc(work->heap, decompressed_size + (work->null_terminate ? 1 : 0), 8);

				int bytes_written = LZ4_decompress_safe(compressed_buffer, decompressed_buffer, (int)work->temp_size - metadata_len, decompressed_size);
				if (bytes_written > 0)
				{
					work->buffer = decompressed_buffer;
					work->size = bytes_written;
					if (work->null_terminate)
					{
						((char*)work->buffer)[bytes_written] = 0;
					}
				}
				else
				{
					debug_print(k_print_error, "There was an issue decompressing a file\n");
				}

				event_signal(work->done);
				break;
			}
		case k_fs_work_op_write:
			{
				int metadata_len = snprintf(NULL, 0, "%d\n", (int)work->size);

				int compressed_max_size = LZ4_compressBound((int)work->size);
				char* compressed_buffer = heap_alloc(work->heap, compressed_max_size + metadata_len, 8);

				snprintf(compressed_buffer, metadata_len + 1, "%d\n", (int)work->size);

				int bytes_written = LZ4_compress_default(work->buffer, compressed_buffer + metadata_len, (int)work->size, compressed_max_size);
				if (bytes_written > 0)
				{
					work->temp_buffer = compressed_buffer;
					work->temp_size = bytes_written + metadata_len;
				}
				else
				{
					debug_print(k_print_error, "There was an issue compressing a file\n");
				}

				queue_push(fs->file_queue, work);
				break;
			}
		}
	}
	return 0;
}

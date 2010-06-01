#define _GNU_SOURCE
#include "codebuf.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

void cb_create(CodeBuf *cb)
{
    cb->data     = NULL;
    cb->size     = 0;
    cb->capacity = 0;
}

void cb_destroy(CodeBuf *cb)
{
    if (cb->capacity > 0)
    {
        munmap(cb->data, cb->capacity);
        cb->data     = NULL;
        cb->size     = 0;
        cb->capacity = 0;
    }
}

/* Aligns the arguments to an integer multiple of the page size: */
static size_t align(size_t size)
{
    size_t pagesize = (size_t)getpagesize();
    if (size%pagesize != 0) size += pagesize - size%pagesize;
    return size;
}

/* Reallocates the underlying buffer to the given capacity or more. */
static void reserve(CodeBuf *cb, size_t new_capacity)
{
    void *new_data;
    new_capacity = align(new_capacity);
    if (cb->capacity == 0)
    {
        new_data = mmap(NULL, new_capacity, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    else
    {
        new_data = mremap(cb->data, cb->capacity, new_capacity, MREMAP_MAYMOVE);
    }
    assert(new_data != NULL);
    cb->data     = new_data;
    cb->capacity = new_capacity;
}

void cb_reserve(CodeBuf *cb, size_t size)
{
    if (cb->capacity < size) reserve(cb, size);
}

void cb_skip(CodeBuf *cb, size_t size)
{
    size_t new_size = cb->size + size;
    if (cb->capacity < new_size) reserve(cb, new_size);
    cb->size = new_size;
}

void cb_append(CodeBuf *cb, const void *buf, size_t len)
{
    size_t new_size = cb->size + len;
    if (cb->capacity < new_size) reserve(cb, new_size);
    memcpy(cb->data + cb->size, buf, len);
    cb->size = new_size;
}

void cb_move(CodeBuf *cb, size_t pos, size_t len, size_t new_pos)
{
    size_t end_pos = new_pos + len;
    assert(pos + len <= cb->size);
    if (end_pos > cb->capacity) reserve(cb, end_pos);
    memmove(cb->data + new_pos, cb->data + pos, len);
    if (end_pos > cb->size) cb->size = end_pos;
}

void cb_insert(CodeBuf *cb, const void *buf, size_t len, size_t pos)
{
    assert(pos <= cb->size);
    if (pos == cb->size) return cb_append(cb, buf, len);
    cb_move(cb, pos, cb->size - pos, pos + len);
    memcpy(cb->data + pos, buf, len);
}

#if 0
void cb_append_padding(CodeBuf *cb, int align)
{
    /* N.B. align must be a power of 2! */
    int add = align - (cb->size&(align - 1));
    if (cb->capacity < cb->size + add) reserve(cb, cb->size + add);
    for (; add >= 4; add -= 4)
    {
        cb->data[cb->size++] = 0x66;
        cb->data[cb->size++] = 0x66;
        cb->data[cb->size++] = 0x66;
        cb->data[cb->size++] = 0x90;
    }
    for (; add > 1; --add) cb->data[cb->size++] = 0x66;
    if (add > 0) cb->data[cb->size++] = 0x90;
}
#endif

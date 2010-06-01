#ifndef CODEBUF_H
#define CODEBUF_H

#include <stdlib.h>

/* A codebuffer is an extensible buffer for exectable code. This is implemented
   by memory mapping executable data pages. All written code must be position-
   independent, because `data' may be moved when reallocation occurs. */
typedef struct CodeBuf
{
    char *data;
    size_t size, capacity;
} CodeBuf;

void cb_create(CodeBuf *cb);
void cb_destroy(CodeBuf *cb);
#define cb_truncate(cb) do (cb)->capacity = 0; while(0)
void cb_reserve(CodeBuf *cb, size_t size);
void cb_skip(CodeBuf *cb, size_t size);
void cb_insert(CodeBuf *cb, const void *buf, size_t len, size_t pos);
void cb_append(CodeBuf *cb, const void *buf, size_t len);
void cb_move(CodeBuf *cb, size_t pos, size_t len, size_t new_pos);
#if 0
void cb_append_padding(CodeBuf *cb, int align);
#endif

#endif /* ndef CODEBUF_H */

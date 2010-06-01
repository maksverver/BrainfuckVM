#ifndef ELF_DUMPER_H_INCLUDED
#define ELF_DUMPER_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>

extern void elf_dump(FILE *fp, void *code, size_t size);

#endif /* ndef ELF_DUMPER_H_INCLUDED */

#include "elf-dumper.h"
#include <elf.h>

#if __x86_64
typedef Elf64_Ehdr Ehdr;
typedef Elf64_Shdr Shdr;
typedef Elf64_Sym  Sym;
#define ELFCLASS ELFCLASS64
#define EM EM_X86_64
#elif __i386
typedef Elf32_Ehdr Ehdr;
typedef Elf32_Shdr Shdr;
typedef Elf32_Sym  Sym;
#define ELFCLASS ELFCLASS32
#define EM EM_386
#else
#error "Could not determine target machine type."
#endif

void elf_dump(FILE *fp, void *code, size_t size)
{
    char str1_data[] = "\0.text\0.shstrtab\0.strtab\0.symtab";
    char str2_data[] = "\0bfmain\0";

    Sym syms_data[3] = {
        { 0, 0, 0, 0, 0, 0 },
        { 0, ELF32_ST_INFO(STB_LOCAL, STT_SECTION), STV_DEFAULT, 1, 0, 0 },
        { 1, ELF32_ST_INFO(STB_GLOBAL, STT_FUNC), STV_DEFAULT, 1,
          0, size } };

    size_t text_pos = sizeof(Ehdr);
    size_t str1_pos = text_pos + size;
    size_t str2_pos = str1_pos + sizeof(str1_data);
    size_t syms_pos = str2_pos + sizeof(str2_data);
    size_t shdr_pos = syms_pos + sizeof(syms_data);

    Ehdr ehdr = { { ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
                    ELFCLASS, ELFDATA2LSB, EV_CURRENT, 0,
                          0, 0, 0, 0, 0, 0, 0, 0 },
        ET_REL, EM, EV_CURRENT, 0, 0, shdr_pos, 0,
        sizeof(Ehdr), 0, 0, sizeof(Shdr), 5, 2 };

    Shdr nulh = { 0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0 };

    Shdr text = { 1, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0,
                        text_pos, size, 0, 0, 16, 0 };

    Shdr str1 = { 7, SHT_STRTAB, 0, 0,
                       str1_pos, sizeof(str1_data), 0, 0, 1, 0 };

    Shdr str2 = { 17, SHT_STRTAB, 0, 0,
                       str2_pos, sizeof(str2_data), 0, 0, 1, 0 };

    Shdr symh = { 25, SHT_SYMTAB, 0, 0, syms_pos, sizeof(syms_data),
                        3, 0, 8, sizeof(Sym) };

    if (fwrite(&ehdr, sizeof(ehdr), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write ELF header!\n");
        return;
    }
    if (fwrite(code, size, 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write code!\n");
        return;
    }
    if (fwrite(str1_data, sizeof(str1_data), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .shstrtab data!\n");
        return;
    }
    if (fwrite(str2_data, sizeof(str2_data), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .strtab data!\n");
        return;
    }
    if (fwrite(syms_data, sizeof(syms_data), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .symtab data!\n");
        return;
    }
    if (fwrite(&nulh, sizeof(nulh), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write null section header!\n");
        return;
    }
    if (fwrite(&text, sizeof(text), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .text section header!\n");
        return;
    }
    if (fwrite(&str1, sizeof(str1), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .shstrtab section header!\n");
        return;
    }
    if (fwrite(&str2, sizeof(str2), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .strtab section header!\n");
        return;
    }
    if (fwrite(&symh, sizeof(symh), 1, fp) != 1)
    {
        fprintf(stderr, "Failed to write .symtab section header!\n");
        return;
    }
}

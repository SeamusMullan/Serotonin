#ifndef _KERNEL
#define _KERNEL

#include <stdint.h>
#include <kernel/schedule/schedule.h>
#include <kernel/string.h>

#define KERNEL_ESP 0xF0400000
#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 5
#define KERNEL_VERSION_LOW 0

#define BIT(n) (1u << (n))

#define ELF_CHUNK_SIZE (512u * 1024u)

/* ELF identification indexes */
#define EI_MAG0       0 // Magic number
#define EI_MAG1       1 // Magic number
#define EI_MAG2       2 // Magic number
#define EI_MAG3       3 // Magic number
#define EI_CLASS      4 // Class
#define EI_DATA       5 // Data encoding
#define EI_VERSION    6 // Version
#define EI_OSABI      7 // OS/ABI
#define EI_ABIVERSION 8 // ABI Version
#define EI_PAD        9 // Padding
#define EI_NIDENT     16 // Number of bytes in e_ident

/* Magic bytes at e_ident[0..3] */
#define ELFMAG0 0x7F  /* e_ident[EI_MAG0] */
#define ELFMAG1 'E'   /* e_ident[EI_MAG1] */
#define ELFMAG2 'L'   /* e_ident[EI_MAG2] */
#define ELFMAG3 'F'   /* e_ident[EI_MAG3] */
#define ELFMAG  "\177ELF" /* e_ident[EI_MAG0..3] */
#define SELFMAG 4 /* e_ident[EI_MAG0..3] */

/* ELF class */
#define ELFCLASS32 1 // 32-bit
#define ELFCLASS64 2 // 64-bit

/* Data encoding */
#define ELFDATA2LSB 1 // Little-endian
#define ELFDATA2MSB 2 // Big-endian

/* ELF types */
#define ET_NONE   0 // No file type
#define ET_REL    1 // Relocatable file
#define ET_EXEC   2 // Executable file
#define ET_DYN    3 // Shared object file
#define ET_CORE   4 // Core file

/* Target machine */
#define EM_NONE  0 // No specific machine
#define EM_386   3 // Intel 80386
#define EM_X86_64 62 // AMD64

/* ELF version */
#define EV_NONE    0 // Invalid version
#define EV_CURRENT 1 // Current version

/* Program header types */
#define PT_NULL    0 // Program header type
#define PT_LOAD    1 // Loadable segment
#define PT_DYNAMIC 2 // Dynamic linking information
#define PT_INTERP  3 // Interpreter path name
#define PT_NOTE    4 // Auxiliary information
#define PT_SHLIB   5 // Reserved
#define PT_PHDR    6 // Program header table itself

/* Segment flags */
#define PF_X 0x1    /* Execute */
#define PF_W 0x2    /* Write   */
#define PF_R 0x4    /* Read    */

/* 32-bit ELF header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT]; /* Magic number and other info */
    uint16_t e_type;             /* Object file type */
    uint16_t e_machine;          /* Architecture */
    uint32_t e_version;          /* Object file version */
    uint32_t e_entry;            /* Entry point virtual address */
    uint32_t e_phoff;            /* Program header table file offset */
    uint32_t e_shoff;            /* Section header table file offset */
    uint32_t e_flags;            /* Processor-specific flags */
    uint16_t e_ehsize;           /* ELF header size in bytes */
    uint16_t e_phentsize;        /* Program header table entry size */
    uint16_t e_phnum;            /* Program header table entry count */
    uint16_t e_shentsize;        /* Section header table entry size */
    uint16_t e_shnum;            /* Section header table entry count */
    uint16_t e_shstrndx;         /* Section header string table index */
} Elf32_Ehdr;

/* 32-bit Program header */
typedef struct {
    uint32_t p_type;   /* Segment type */
    uint32_t p_offset; /* Segment file offset */
    uint32_t p_vaddr;  /* Segment virtual address */
    uint32_t p_paddr;  /* Segment physical address */
    uint32_t p_filesz; /* Segment size in file */
    uint32_t p_memsz;  /* Segment size in memory */
    uint32_t p_flags;  /* Segment flags */
    uint32_t p_align;  /* Segment alignment */
} Elf32_Phdr;

typedef struct {
    /* Leaf 1, ECX */
    uint8_t sse3;       /* ECX[0]   */
    uint8_t pclmulqdq;  /* ECX[1]   */
    uint8_t monitor;    /* ECX[3]   */
    uint8_t ssse3;      /* ECX[9]   */
    uint8_t fma;        /* ECX[12]  */
    uint8_t cx16;       /* ECX[13]  */
    uint8_t sse4_1;     /* ECX[19]  */
    uint8_t sse4_2;     /* ECX[20]  */
    uint8_t x2apic;     /* ECX[21]  */
    uint8_t popcnt;     /* ECX[23]  */
    uint8_t aes;        /* ECX[25]  */
    uint8_t xsave;      /* ECX[26]  */
    uint8_t osxsave;    /* ECX[27]  */
    uint8_t avx;        /* ECX[28]  */
    uint8_t f16c;       /* ECX[29]  */
    uint8_t rdrand;     /* ECX[30]  */

    /* Leaf 1, EDX */
    uint8_t fpu;        /* EDX[0]   */
    uint8_t mmx;        /* EDX[23]  */
    uint8_t sse;        /* EDX[25]  */
    uint8_t sse2;       /* EDX[26]  */
    uint8_t htt;        /* EDX[28]  */

    /* Leaf 7, subleaf 0, EBX */
    uint8_t bmi1;       /* EBX[3]   */
    uint8_t hle;        /* EBX[4]   */
    uint8_t avx2;       /* EBX[5]   */
    uint8_t smep;       /* EBX[7]   */
    uint8_t bmi2;       /* EBX[8]   */
    uint8_t erms;       /* EBX[9]   */
    uint8_t invpcid;    /* EBX[10]  */
    uint8_t rtm;        /* EBX[11]  */

    /* Leaf 7, subleaf 0, ECX */
    uint8_t pku;            /* ECX[3]   */
    uint8_t avx512f;        /* ECX[16]  */
    uint8_t avx512dq;       /* ECX[17]  */
    uint8_t avx512pf;       /* ECX[26]  */
    uint8_t avx512er;       /* ECX[27]  */
    uint8_t avx512cd;       /* ECX[28]  */
    uint8_t sha;            /* ECX[29]  */
    uint8_t avx512_vbmi;    /* ECX[1]   */

    /* Leaf 7, subleaf 0, EDX */
    uint8_t avx512bw;       /* EDX[30]  */
    uint8_t avx512vl;       /* EDX[31]  */

    /* Extended leaf 0x80000001, ECX */
    uint8_t lahf_lm;    /* ECX[0]   */
    uint8_t abm;        /* ECX[5]   */
    uint8_t sse4a;      /* ECX[6]   */
    uint8_t fma4;       /* ECX[16]  */
    uint8_t xop;        /* ECX[11]  */

    /* Extended leaf 0x80000001, EDX */
    uint8_t syscall_sysret; /* EDX[11] */
    uint8_t mmxext;         /* EDX[22] */
    uint8_t rdtscp;         /* EDX[27] */
    uint8_t lm;             /* EDX[29] */
} cpu_features_t;

// probs better defined elsewhere but IDGAF
static inline uint32_t count_total_string_bytes(const char* const* vec, int n) {
    uint32_t total = 0;
    for (int i = 0; i < n; ++i) total += (uint32_t)strlen(vec[i]) + 1;
    return total;
}

static inline uint32_t align_down(uint32_t x, uint32_t a) { return x & ~(a-1); }

void kernel_panic(char* str);
void *kernel_malloc(uint32_t size);
void kernel_free(void *ptr);
void *kernel_malloc_align(uint32_t align, uint32_t size);
void kernel_free_align(void *ptr);
void kernel_sleep(unsigned int milliseconds);
int kernel_load_elf(process_control_block_t *pcb, const char *path, const char *pname, const char *const *argv, int argc, const char *const *envp, int envc);

#endif

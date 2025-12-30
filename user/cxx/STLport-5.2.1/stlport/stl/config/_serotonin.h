/*
 * STLport configuration for Serotonin OS
 * Bare-metal i386 operating system with newlib
 */

#ifndef __stl_config__serotonin_h
#define __stl_config__serotonin_h

#define _STLP_PLATFORM "Serotonin OS"

/* Newlib provides standard C library functions */
#define _STLP_USE_NEWALLOC 1

/* No multithreading */
#define _STLP_NO_THREADS 1

/* Use stdio-based I/O */
#define _STLP_USE_STDIO_IO 1

/*
 * CRITICAL: We only have C headers from newlib, not C++ headers like <ctime>.
 * This must be defined early to prevent _gcc.h from setting _STLP_USE_NEW_C_HEADERS.
 * The HAS_NO_NEW_C_HEADERS flag tells STLport to use <time.h> instead of <ctime>.
 */
#define _STLP_HAS_NO_NEW_C_HEADERS 1

/* No native C++ runtime headers (we don't have libstdc++) */
#define _STLP_NO_NEW_NEW_HEADER 1
#define _STLP_HAS_NO_NEW_IOSTREAMS 1

/* We provide our own exception header or none at all */
#define _STLP_NO_EXCEPTION_HEADER 1

/* No typeinfo from runtime */
/* #define _STLP_NO_TYPEINFO 1 */

/* Use vendor (newlib) C std namespace as global */
#define _STLP_VENDOR_GLOBAL_CSTD 1

/* rand_r may not be available in newlib */
#define _STLP_NO_VENDOR_STDLIB_L 1

/* wchar_t support depends on newlib config */
/* Uncomment if your newlib doesn't have wchar support */
/* #define _STLP_NO_WCHAR_T 1 */

#endif /* __stl_config__serotonin_h */

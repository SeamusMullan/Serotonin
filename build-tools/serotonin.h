/* Target OS header for Serotonin.

   This file defines the GCC target specs for Serotonin, so that
   i686-serotonin-gcc test.c -o test works without manual linking. */

/* Define the OS builtins. */
#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()         \
  do {                                    \
    builtin_define ("__serotonin__");     \
    builtin_define ("__unix__");          \
    builtin_assert ("system=serotonin"); \
    builtin_assert ("system=unix");      \
    builtin_assert ("system=posix");     \
  } while (0)

/* Use crt0.o as the startup file.
   -nostdlib suppresses this. */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "%{!nostdlib:crt0.o%s}"

/* No end files needed - crt0.s handles fini. */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC ""

/* Default libraries: syscall wrappers, C++ runtime (including exception
   handling via libsupc++), libc, libm, and compiler runtime, all in a
   link group to resolve circular deps. */
#undef  LIB_SPEC
#define LIB_SPEC \
  "%{!nostdlib:--start-group -lstdc++ -lsyscall -lcxxrt -lsupc++ -lc -lm -lgcc --end-group}"

/* We handle -lgcc inside LIB_SPEC's link group, so suppress the
   default LIBGCC_SPEC to avoid duplicate -lgcc. */
#undef  LIBGCC_SPEC
#define LIBGCC_SPEC ""

/* Linker flags: use the Serotonin user linker script by default.
   -nostdlib suppresses this.  -shared/-static passed through.
   -L %R/usr/lib is needed so ld can find user.ld before GCC's -L flags. */
#undef  LINK_SPEC
#define LINK_SPEC \
  "%{!nostdlib:-L %R/usr/lib %{!T*:-T user.ld}} %{shared:-shared} %{static:-static}"

/* Use standard ELF sections. */
#undef  OBJECT_FORMAT_ELF
#define OBJECT_FORMAT_ELF 1

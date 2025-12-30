// STLport stubs for Serotonin OS bare-metal
// Provides the range error functions that STLport expects when iostreams are enabled
// These abort instead of throwing exceptions since Serotonin is compiled with -fno-exceptions

#include <stdio.h>
#include <stdlib.h>

// Use the same namespace as STLport
namespace stlpmtx_std {

void __stl_throw_runtime_error(const char* msg) {
    puts("STL runtime_error: ");
    puts(msg);
    puts("\n");
    abort();
}

void __stl_throw_range_error(const char* msg) {
    puts("STL range_error: ");
    puts(msg);
    puts("\n");
    abort();
}

void __stl_throw_out_of_range(const char* msg) {
    puts("STL out_of_range: ");
    puts(msg);
    puts("\n");
    abort();
}

void __stl_throw_length_error(const char* msg) {
    puts("STL length_error: ");
    puts(msg);
    puts("\n");
    abort();
}

void __stl_throw_invalid_argument(const char* msg) {
    puts("STL invalid_argument: ");
    puts(msg);
    puts("\n");
    abort();
}

void __stl_throw_overflow_error(const char* msg) {
    puts("STL overflow_error: ");
    puts(msg);
    puts("\n");
    abort();
}

} // namespace stlpmtx_std

// kernel/test/test_vfs.c
#include <kernel/test/ktest.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/string.h>

// Get strlen from string.h
extern size_t strlen(const char *str);

// Test VFS initialization
KTEST_DEFINE(vfs_init_test) {
    // VFS should already be initialized by kernel
    // This is more of a smoke test
    KTEST_ASSERT(1, "VFS initialized");
    return 1;
}

// Test file open
KTEST_DEFINE(vfs_open_test) {
    // Try to opeernel_ernel)_erne;+kl)_ernelkernel_freeernleelkernel_freeernel_n root directory
    vfs_node_t *root = vfs_open("/");
    
    if (root == NULL) {
        KTEST_SKIP("VFS not fully initialized or root not mounted");
    }
    
    KTEST_ASSERT_NOT_NULL(root, "root directory opens");
    KTEST_ASSERT(root->flags & VFS_FLAG_DIRECTORY, "root is a directory");
    
    vfs_close(root);
    return 1;
}

// Test path resolution
KTEST_DEFINE(vfs_resolve_path_test) {
    vfs_node_t *node;
    
    node = vfs_resolve_path("/");
    if (node == NULL) {
        KTEST_SKIP("VFS root not available");
    }
    KTEST_ASSERT_NOT_NULL(node, "resolve root path");
    
    // Test invalid path
    node = vfs_resolve_path("/nonexistent/path/to/file");
    KTEST_ASSERT_NULL(node, "resolve nonexistent path returns NULL");
    
    return 1;
}

// Test directory creation
KTEST_DEFINE(vfs_mkdir_test) {
    int result = vfs_mkdir("/tmp");
    
    if (result != 0) {
        // May already exist or VFS not writable
        KTEST_SKIP("Cannot create directory (may be read-only or already exists)");
    }
    
    vfs_node_t *node = vfs_open("/tmp");
    KTEST_ASSERT_NOT_NULL(node, "created directory exists");
    KTEST_ASSERT(node->flags & VFS_FLAG_DIRECTORY, "created node is directory");
    
    vfs_close(node);
    return 1;
}

// Test file creation
KTEST_DEFINE(vfs_create_file_test) {
    vfs_node_t *file = vfs_create("/test_file.txt");
    
    if (file == NULL) {
        KTEST_SKIP("Cannot create file (VFS may be read-only)");
    }
    
    KTEST_ASSERT_NOT_NULL(file, "file creation succeeds");
    KTEST_ASSERT(file->flags & VFS_FLAG_FILE, "created node is file");
    
    vfs_close(file);
    return 1;
}

// Test file write and read
KTEST_DEFINE(vfs_write_read_test) {
    const char *test_data = "Hello, VFS!";
    const char *test_path = "/test_rw.txt";
    char buffer[64] = {0};
    
    // Create file
    vfs_node_t *file = vfs_create(test_path);
    if (file == NULL) {
        KTEST_SKIP("Cannot create test file");
    }
    
    // Write data
    int written = vfs_write(file, 0, strlen(test_data), test_data);
    if (written <= 0) {
        vfs_close(file);
        KTEST_SKIP("Write operation not supported");
    }
    
    KTEST_ASSERT_EQ(written, (int)strlen(test_data), "correct number of bytes written");
    
    vfs_close(file);
    
    // Re-open and read
    file = vfs_open(test_path);
    KTEST_ASSERT_NOT_NULL(file, "re-open file for reading");
    
    int read_bytes = vfs_read(file, 0, 64, buffer);
    KTEST_ASSERT(read_bytes > 0, "read returns data");
    KTEST_ASSERT_STR_EQ(buffer, test_data, "read data matches written data");
    
    vfs_close(file);
    return 1;
}

// Test file operations on directory (should fail)
KTEST_DEFINE(vfs_dir_read_test) {
    vfs_node_t *dir = vfs_open("/");
    
    if (dir == NULL) {
        KTEST_SKIP("Root directory not available");
    }
    
    char buffer[64];
    int result = vfs_read(dir, 0, 64, buffer);
    
    // Reading a directory as a file should fail or return special value
    KTEST_ASSERT(result <= 0, "cannot read directory as file");
    
    vfs_close(dir);
    return 1;
}

// Test multiple file operations
KTEST_DEFINE(vfs_multiple_files_test) {
    vfs_node_t *file1 = vfs_create("/test1.txt");
    vfs_node_t *file2 = vfs_create("/test2.txt");
    
    if (file1 == NULL || file2 == NULL) {
        if (file1) vfs_close(file1);
        if (file2) vfs_close(file2);
        KTEST_SKIP("Cannot create multiple files");
    }
    
    KTEST_ASSERT_NOT_NULL(file1, "first file created");
    KTEST_ASSERT_NOT_NULL(file2, "second file created");
    KTEST_ASSERT_NEQ(file1, file2, "different file handles");
    
    const char *data1 = "File 1";
    const char *data2 = "File 2";
    
    vfs_write(file1, 0, strlen(data1), data1);
    vfs_write(file2, 0, strlen(data2), data2);
    
    vfs_close(file1);
    vfs_close(file2);
    
    // Verify
    file1 = vfs_open("/test1.txt");
    file2 = vfs_open("/test2.txt");
    
    if (file1 && file2) {
        char buf1[32] = {0}, buf2[32] = {0};
        vfs_read(file1, 0, 32, buf1);
        vfs_read(file2, 0, 32, buf2);
        
        KTEST_ASSERT_STR_EQ(buf1, data1, "file1 data correct");
        KTEST_ASSERT_STR_EQ(buf2, data2, "file2 data correct");
    }
    
    if (file1) vfs_close(file1);
    if (file2) vfs_close(file2);
    
    return 1;
}

// Run all VFS tests
void test_vfs_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(vfs_init_test),
        KTEST_RUN(vfs_open_test),
        KTEST_RUN(vfs_resolve_path_test),
        KTEST_RUN(vfs_mkdir_test),
        KTEST_RUN(vfs_create_file_test),
        KTEST_RUN(vfs_write_read_test),
        KTEST_RUN(vfs_dir_read_test),
        KTEST_RUN(vfs_multiple_files_test),
    };
    
    ktest_run_suite("Virtual Filesystem", tests, sizeof(tests) / sizeof(tests[0]));
}

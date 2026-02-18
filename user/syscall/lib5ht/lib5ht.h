/**
 * @file lib5ht.h
 * @brief 5HT (Serotonin) user-space library interface
 *
 * Provides user-space access to Serotonin-specific kernel features
 * including process listing, framebuffer layer management, and
 * the low-level system call mechanism.
 *
 * @defgroup lib5ht 5HT Library
 * @{
 */

#ifndef _LIB5HT
#define _LIB5HT

#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process information structure
 *
 * Contains information about a running process, retrieved
 * via sys_5ht_list_processes().
 */
typedef struct proc_5ht {
    int pid;            /**< Process ID */
    char name[32];      /**< Process name */
    int priority;       /**< Scheduling priority */
    int priv;           /**< Privilege level (0=kernel, 3=user) */
} proc_5ht_t;

/**
 * @defgroup mouse Mouse Event Interface
 * @{
 */

/** Mouse event buffer size (kernel-side) */
#define MOUSE_EVENT_BUFFER_SIZE 64

/** Mouse button masks */
#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

/** Mouse event types */
enum {
    MOUSE_EVENT_MOVE = 0,        /**< Mouse moved (reserved for future use) */
    MOUSE_EVENT_BUTTON_DOWN = 1, /**< Button pressed */
    MOUSE_EVENT_BUTTON_UP = 2    /**< Button released */
};

/**
 * @brief Mouse event structure
 *
 * Returned when reading from /dev/mouse/event.
 * Use blocking read to wait for events, or O_NONBLOCK for polling.
 */
typedef struct mouse_event {
    int16_t x;          /**< X coordinate at time of event */
    int16_t y;          /**< Y coordinate at time of event */
    uint8_t buttons;    /**< Button state (MOUSE_BTN_* masks) */
    uint8_t event_type; /**< Event type (MOUSE_EVENT_*) */
} __attribute__((packed)) mouse_event_t;

/** @} */ /* end of mouse group */

/**
 * @defgroup keyboard Keyboard Event Interface
 * @{
 */

/** Keyboard event flags */
#define KEY_FLAG_RELEASED (1 << 0)  /**< Key was released (vs pressed) */
#define KEY_FLAG_SHIFT    (1 << 1)  /**< Shift was held */
#define KEY_FLAG_CTRL     (1 << 2)  /**< Ctrl was held */

/**
 * @brief Keyboard event structure
 *
 * Returned when reading from /dev/keyboard/event.
 * Use blocking read to wait for events, or O_NONBLOCK for polling.
 */
typedef struct keyboard_event {
    uint8_t scancode;    /**< Raw PS/2 scancode (without release bit) */
    uint8_t ascii;       /**< Translated ASCII character (0 if none) */
    uint8_t flags;       /**< Event flags (KEY_FLAG_*) */
    uint8_t _pad;        /**< Padding */
} __attribute__((packed)) keyboard_event_t;

/** @} */ /* end of keyboard group */

/**
 * @brief Framebuffer information structure
 *
 * Contains global framebuffer configuration information.
 */
typedef struct fb_info {
    uint32_t size;              /**< Structure size for versioning */
    uint32_t fb_size;           /**< Total framebuffer size in bytes */
    uint32_t layer_window_size; /**< Size of each layer window */
    uint32_t metadata_size;     /**< Size of metadata region */
    uint32_t alignment;         /**< Required memory alignment */
} fb_info_t;

/**
 * @brief Framebuffer layer configuration
 *
 * Used to request or reconfigure a framebuffer layer.
 */
typedef struct fb_layer_config {
    uint32_t size;      /**< Structure size for versioning */
    uint16_t x0;        /**< Left edge X coordinate */
    uint16_t x1;        /**< Right edge X coordinate */
    uint16_t y0;        /**< Top edge Y coordinate */
    uint16_t y1;        /**< Bottom edge Y coordinate */
    uint8_t  alpha;     /**< Alpha blending enable (0=opaque, 1=blend) */
    uint16_t stride;    /**< Stride in bytes per row */
} fb_layer_config_t;

/**
 * @brief Framebuffer layer information
 *
 * Returned when a layer is allocated, contains pointers
 * to the framebuffer and metadata regions.
 */
typedef struct fb_layer_info {
    uint32_t size;              /**< Structure size for versioning */
    uint16_t layer_id;          /**< Layer identifier */
    uint16_t owned;             /**< Ownership flag */
    uintptr_t fb_user_va;       /**< User-space framebuffer address */
    uintptr_t metadata_user_va; /**< User-space metadata address */
    uint32_t fb_size;           /**< Framebuffer size in bytes */
    uint32_t metadata_size;     /**< Metadata size in bytes */
    fb_layer_config_t cfg;      /**< Current layer configuration */
} fb_layer_info_t;

/**
 * @brief Framebuffer layer metadata (shared with compositor)
 *
 * Written by the application to signal frame completion,
 * read by the compositor for synchronization.
 */
typedef struct fb_layer_metadata {
    uint8_t  ready;     /**< Frame ready flag (set by app, cleared by compositor) */
    uint16_t dx0;       /**< Dirty region left edge */
    uint16_t dx1;       /**< Dirty region right edge */
    uint16_t dy0;       /**< Dirty region top edge */
    uint16_t dy1;       /**< Dirty region bottom edge */
    uint32_t frame_id;  /**< Frame sequence number */
} fb_layer_metadata_t;

/**
 * @brief Execute a system call via interrupt 0x80
 *
 * Low-level function that performs the actual system call by triggering
 * interrupt 0x80 with the appropriate register values.
 *
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return System call return value, or sets errno and returns error code on failure
 */
static inline int do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    register uint32_t eax asm("eax") = num;
    register uint32_t ebx asm("ebx") = arg1;
    register uint32_t ecx asm("ecx") = arg2;
    register uint32_t edx asm("edx") = arg3;

    asm volatile("int $0x80"
                 : "+a"(eax)
                 : "b"(ebx), "c"(ecx), "d"(edx)
                 : "memory");

    if ((int)eax < 0) {
        errno = -(int)eax;
        return -1;
    }
    return eax;
}

/**
 * @brief List all running processes
 *
 * @param buf Buffer to store process information
 * @param max Maximum number of processes to return
 * @return Number of processes returned, or negative error code
 */
int sys_5ht_list_processes(proc_5ht_t *buf, size_t max);

/**
 * @brief Request a framebuffer layer
 *
 * Allocates and maps a framebuffer layer for the calling process.
 *
 * @param id Layer ID to request
 * @param cfg Layer configuration
 * @param out Output structure for layer information
 * @return 0 on success, negative error code on failure
 */
int sys_5ht_req_buf(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out);

/**
 * @brief Release a framebuffer layer
 *
 * @param id Layer ID to release
 * @return 0 on success, negative error code on failure
 */
int sys_5ht_rel_buf(uint16_t id);

/**
 * @brief Reconfigure a framebuffer layer
 *
 * @param id Layer ID to reconfigure
 * @param cfg New layer configuration
 * @param out Output structure for updated layer information
 * @return 0 on success, negative error code on failure
 */
int sys_5ht_rcfg_layer(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out);

/**
 * @brief Query global framebuffer information
 *
 * @param out Output structure for framebuffer information
 * @return 0 on success, negative error code on failure
 */
int sys_5ht_query_info(fb_info_t *out);

/**
 * @brief Query a specific layer's information
 *
 * @param id Layer ID to query
 * @param out Output structure for layer information
 * @return 0 on success, negative error code on failure
 */
int sys_5ht_query_layer(uint16_t id, fb_layer_info_t *out);
int sys_5ht_set_fid(pid_t pid);

/** @} */ /* end of lib5ht group */

#ifdef __cplusplus
}
#endif

#endif

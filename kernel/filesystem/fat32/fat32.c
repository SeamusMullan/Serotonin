#include "fat32.h"
#include <stdint.h>
#include "../../stdio/stdio.h"
#include "../../stdlib/stdlib.h"
#include "../../io/io.h"
#include "../../kernel.h"
#include "../../string.h"
#include "../vfs.h"
#include "../ide.h"

filesystem_t fat32_fs = {
    .name = "fat32",
    .mount = fat32_mount,
    .next = (void *)0
};

/**
 * @brief Builds a FAT32 directory entry name key from a filename.
 *
 * @param fname The input filename.
 * @param key The output key (must be 11 bytes).
 */
static void fat32_build_name_key(const char *fname, uint8_t key[11]) {
    memset(key, ' ', 11);
    const char *dot = strchr(fname, '.');
    int baselen = dot ? (dot - fname) : strlen(fname);
    if (baselen > 8) baselen = 8;
    for (int i = 0; i < baselen; i++)
        key[i] = toupper((unsigned char)fname[i]);
    if (dot) {
        int extlen = strlen(dot+1);
        if (extlen > 3) extlen = 3;
        for (int i = 0; i < extlen; i++)
            key[8+i] = toupper((unsigned char)dot[1 + i]);
    }
}

/**
 * @brief Extracts a filename from a FAT32 directory entry.
 *
 * @param raw The raw directory entry data (must be 11 bytes).
 * @param out The output buffer for the filename (must be 13 bytes).
 */
static void fat32_name_from_entry(const uint8_t raw[11], char out[13]) {
    int pos = 0;
    // copy the base name (first 8 bytes)
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ')
            break;
        out[pos++] = raw[i];
    }

    // copy the extension if any (bytes 8..10)
    // check if ext part is non-spaces
    int extstart = 8;
    while (extstart < 11 && raw[extstart] == ' ')
        extstart++;
    if (extstart < 11) {
        out[pos++] = '.';           // add dot
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ')
                break;
            out[pos++] = raw[i];
        }
    }

    out[pos] = '\0';
}

/**
 * @brief Parses the BIOS Parameter Block (BPB) of a FAT32 filesystem.
 *
 * @param info The FAT32 filesystem information structure to populate.
 * @param drive The drive number (0-based).
 * @param partition_start_lba The starting LBA of the partition.
 * @param boot_sector The raw boot sector data.
 */
static void fat32_parse_bpb(fat32_fs_info_t *info,
                            uint8_t drive,
                            uint32_t partition_start_lba,
                            uint8_t *boot_sector)
{
    fat_BS_t       *bpb = (fat_BS_t *)boot_sector;
    fat_extBS_32_t *ext = (fat_extBS_32_t *)(bpb->extended_section);

    info->drive                 = drive;
    info->partition_start_lba   = partition_start_lba;
    info->bytes_per_sector      = bpb->bytes_per_sector;
    info->sectors_per_cluster   = bpb->sectors_per_cluster;
    info->reserved_sector_count = bpb->reserved_sector_count;
    info->table_count           = bpb->table_count;
    info->fat_size              = ext->table_size_32;
    info->root_cluster          = ext->root_cluster;

    info->fat_start_lba         = partition_start_lba + info->reserved_sector_count;
    info->cluster_heap_start_lba=
        info->fat_start_lba + info->table_count * info->fat_size;
}

/**
 * @brief Initializes the FAT32 filesystem.
 *
 */
void fat32_init(void) {
    vfs_register_fs(&fat32_fs);
}

/**
 * @brief Mounts a FAT32 filesystem.
 *
 * @param device The device to mount (unused).
 * @return vfs_node_t* The root directory of the mounted filesystem.
 */
vfs_node_t *fat32_mount(const char *device) {
    uint8_t drive = device ? (uint8_t)atoi(device) : 0;

    // 1: Read MBR
    uint8_t *mbr = kernel_malloc(512);
    ide_read_sector(drive, 0, mbr);
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        printfs(PRINT_STATUS_ERROR,"fat32_mount: invalid MBR signature %02x %02x\n",
               mbr[510], mbr[511]);
        kernel_free(mbr);
        return NULL;
    }
    uint32_t part1 = mbr[454] | (mbr[455]<<8) | (mbr[456]<<16) | (mbr[457]<<24);
    printfs(PRINT_STATUS_DEBUG,"fat32_mount: partition1 @ LBA %u\n", part1);
    kernel_free(mbr);

    // 2: Read Boot Sector
    uint8_t *boot = kernel_malloc(512);
    ide_read_sector(drive, part1, boot);
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        printfs(PRINT_STATUS_DEBUG,"fat32_mount: invalid BS sig %02x %02x\n",
               boot[510], boot[511]);
        kernel_free(boot);
        return NULL;
    }

    // 3: Parse BPB
    fat32_fs_info_t *fs_info = kernel_malloc(sizeof(*fs_info));
    fat32_parse_bpb(fs_info, drive, part1, boot);
    kernel_free(boot);

    // 4: Allocate root vfs_node
    vfs_node_t *root = kernel_malloc(sizeof(*root));
    memset(root, 0, sizeof(*root));
    strcpy(root->name, "/");
    root->inode    = 0;
    root->flags    = VFS_FLAG_DIRECTORY;
    root->refcount = 1;
    root->ops      = &fat32_ops;

    // 5: Store fs_info + root cluster in fs_data
    fat32_node_info_t *ni = kernel_malloc(sizeof(*ni));
    ni->fs_info       = fs_info;
    ni->cluster_number= fs_info->root_cluster;
    root->fs_data     = ni;

    printfs(PRINT_STATUS_DEBUG,"fat32: mounted drive %u, root cluster %u\n",
           drive, fs_info->root_cluster);
    return root;
}

/**
 * @brief Reads a cluster from the FAT32 filesystem.
 *
 * @param fs_info The FAT32 filesystem information.
 * @param cluster The cluster number to read.
 * @param buffer The buffer to read the cluster data into.
 */
static void fat32_read_cluster(fat32_fs_info_t *fs_info, uint32_t cluster, uint8_t *buffer) {
    uint32_t first_sector = fs_info->cluster_heap_start_lba + (cluster - 2) * fs_info->sectors_per_cluster;

    for (uint8_t i = 0; i < fs_info->sectors_per_cluster; i++) {
        ide_read_sector(fs_info->drive, first_sector + i, buffer + (i * fs_info->bytes_per_sector));
    }
}

/**
 * @brief Reads a cluster from the FAT32 filesystem.
 *
 * @param fs_info The FAT32 filesystem information.
 * @param cluster The cluster number to read.
 * @return uint32_t The number of bytes read, or 0 on failure.
 */
static uint32_t fat32_read_fat_entry(fat32_fs_info_t *fs_info, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs_info->fat_start_lba + (fat_offset / fs_info->bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fs_info->bytes_per_sector;

    uint8_t sector[512];
    ide_read_sector(fs_info->drive, fat_sector, sector);

    uint32_t entry = *(uint32_t *)(sector + offset_in_sector);
    return entry & 0x0FFFFFFF; // mask to 28 bits
}

static uint32_t current_cluster = 0;
static uint32_t entry_offset = 0;

/**
 * @brief Reads a directory entry from the FAT32 filesystem.
 *
 * @param node The VFS node representing the directory to read from.
 * @param index The index of the entry to read.
 * @return vfs_node_t* The VFS node representing the directory entry, or NULL on failure.
 */
vfs_node_t *fat32_readdir(vfs_node_t *node, uint32_t index) {
    fat32_node_info_t *node_info = (fat32_node_info_t *)node->fs_data;
    fat32_fs_info_t *fs_info = node_info->fs_info;
    uint32_t cluster = node_info->cluster_number;

    uint8_t cluster_buffer[fs_info->sectors_per_cluster * fs_info->bytes_per_sector];
    uint32_t entries_per_cluster = (fs_info->sectors_per_cluster * fs_info->bytes_per_sector) / sizeof(fat_dir_entry_t);

    uint32_t entry_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs_info, cluster, cluster_buffer);

        fat_dir_entry_t *entries = (fat_dir_entry_t *)cluster_buffer;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00) {
                // No more entries
                return NULL;
            }
            if ((entries[i].name[0] == 0xE5) || (entries[i].attr & FAT32_ATTR_VOLUME_ID)) {
                // Skip deleted or volume ID entries
                continue;
            }

            if (entry_count == index) {
                // Return this entry
                vfs_node_t *child = kernel_malloc(sizeof(vfs_node_t));
                memset(child, 0, sizeof(vfs_node_t));

                // Convert name to null-terminated string
                char name[12];
                memcpy(name, entries[i].name, 11);
                name[11] = '\0';

                // Trim trailing spaces
                for (int j = 10; j >= 0; j--) {
                    if (name[j] == ' ') name[j] = '\0';
                    else break;
                }

                char friendly[13];
                fat32_name_from_entry(name, friendly);
                strcpy(child->name, friendly);

                child->inode = index;
                if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
                    child->flags = VFS_FLAG_DIRECTORY;
                } else {
                    child->flags = VFS_FLAG_FILE;
                }
                child->size = entries[i].file_size;
                child->refcount = 1;
                child->ops = node->ops; // reuse ops

                // Setup fs_data
                fat32_node_info_t *child_info = kernel_malloc(sizeof(fat32_node_info_t));
                child_info->fs_info = fs_info;
                child_info->cluster_number = ((entries[i].first_cluster_high << 16) | entries[i].first_cluster_low);
                child_info->parent_cluster = cluster;
                child->fs_data = child_info;

                return child;
            }

            entry_count++;
        }

        // Move to next cluster in chain
        cluster = fat32_read_fat_entry(fs_info, cluster);
    }

    return NULL; // No more entries
}

/**
 * @brief Reads data from a file in the FAT32 filesystem.
 *
 * @param node The VFS node representing the file to read from.
 * @param offset The offset to read from.
 * @param size The number of bytes to read.
 * @param buffer The buffer to read data into.
 * @return int The number of bytes read, or -1 on failure.
 */
static int fat32_read(vfs_node_t *node,
                      uint32_t offset,
                      uint32_t size,
                      char *buffer)
{
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    // clamp to file size
    if (offset >= node->size) return 0;
    if (offset + size > node->size)
        size = node->size - offset;

    fat32_node_info_t *ni  = (fat32_node_info_t*)node->fs_data;
    fat32_fs_info_t   *fs  = ni->fs_info;
    uint32_t cluster_size   = fs->bytes_per_sector * fs->sectors_per_cluster;

    // find the first cluster of this file
    uint32_t cluster = ni->cluster_number;

    // skip clusters until we reach the one containing 'offset'
    uint32_t skip = offset / cluster_size;
    uint32_t cluster_offset = offset % cluster_size;
    for (uint32_t i = 0; i < skip; i++) {
        cluster = fat32_read_fat_entry(fs, cluster);
        if (cluster >= FAT32_CLUSTER_END) return 0;
    }

    // allocate a single-cluster buffer
    uint8_t *clusbuf = kernel_malloc(cluster_size);
    uint32_t read = 0;

    // read cluster by cluster
    while (read < size && cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, clusbuf);

        // how many bytes to copy from this cluster
        uint32_t copy = cluster_size - cluster_offset;
        if (copy > size - read) copy = size - read;

        memcpy(buffer + read, clusbuf + cluster_offset, copy);
        read += copy;
        cluster_offset = 0;

        // move to next cluster if more to read
        if (read < size)
            cluster = fat32_read_fat_entry(fs, cluster);
    }

    kernel_free(clusbuf);
    return read;
}

/**
 * @brief Reads a directory entry from the FAT32 filesystem.
 *
 * @param node The VFS node representing the directory to read from.
 * @param index The index of the entry to read.
 * @return vfs_node_t* The VFS node representing the directory entry, or NULL on failure.
 */
static int fat32_open(vfs_node_t *node) {
    printfs(PRINT_STATUS_DEBUG,"fat32_open: opening node %s\n", node->name);

    // no per‐node initialization needed
    return 0;
}

/**
 * @brief Closes a file or directory in the FAT32 filesystem.
 *
 * @param node The VFS node representing the file or directory to close.
 * @return int 0 on success, or -1 on failure.
 */
static int fat32_close(vfs_node_t *node) {
    // no per‐node teardown needed
    return 0;
}

/**
 * @brief Finds a directory entry in the FAT32 filesystem.
 *
 * @param dir The VFS node representing the directory to search in.
 * @param name The name of the directory entry to find.
 * @return vfs_node_t* The VFS node representing the directory entry, or NULL on failure.
 */
static vfs_node_t *fat32_finddir(vfs_node_t *dir, const char *name) {
    // build the 11-byte key
    uint8_t key[11];
    fat32_build_name_key(name, key);

    fat32_node_info_t *ni = dir->fs_data;
    fat32_fs_info_t   *fs = ni->fs_info;
    uint32_t           cluster = ni->cluster_number;

    // how many entries per cluster
    uint32_t per_cluster =
      (fs->sectors_per_cluster * fs->bytes_per_sector)
      / sizeof(fat_dir_entry_t);

    // scratch buffer
    uint8_t scratch[fs->sectors_per_cluster * fs->bytes_per_sector];

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, scratch);
        fat_dir_entry_t *ents = (fat_dir_entry_t*)scratch;

        for (uint32_t i = 0; i < per_cluster; i++) {
            // end-of-directory
            if (ents[i].name[0] == 0x00) return NULL;
            // skip deleted or volume label
            if (ents[i].name[0] == 0xE5 ||
                (ents[i].attr & FAT32_ATTR_VOLUME_ID)) continue;

            // compare the raw 11-byte name
            if (memcmp(key, ents[i].name, 11) == 0) {
                // found it
                vfs_node_t *child = kernel_malloc(sizeof(*child));
                memset(child, 0, sizeof(*child));
                // copy back a user-friendly name
                strncpy(child->name, name, sizeof(child->name));
                child->inode    = (uint32_t)child;
                child->flags    = (ents[i].attr & FAT32_ATTR_DIRECTORY)
                                  ? VFS_FLAG_DIRECTORY
                                  : VFS_FLAG_FILE;
                child->size     = ents[i].file_size;
                child->ops      = &fat32_ops;
                child->refcount = 1;

                fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
                cni->fs_info        = fs;
                cni->cluster_number = (ents[i].first_cluster_high << 16)
                                      | ents[i].first_cluster_low;
                cni->parent_cluster = cluster;
                child->fs_data      = cni;
                return child;
            }
        }

        // next cluster
        cluster = fat32_read_fat_entry(fs, cluster);
    }

    return NULL;
}

/**
 * @brief Writes a FAT entry in the FAT32 filesystem.
 *
 * @param fs The FAT32 filesystem information.
 * @param cluster The cluster number to write to.
 * @param value The value to write.
 * @return int 0 on success, or -1 on failure.
 */
static int fat32_write_fat_entry(fat32_fs_info_t *fs, uint32_t cluster, uint32_t value)
{
    // mask to 28 bits
    value &= 0x0FFFFFFF;

    uint32_t off    = cluster * 4;
    uint32_t sector = off / fs->bytes_per_sector;
    uint32_t idx    = off % fs->bytes_per_sector;
    uint8_t  buf[512];

    for (int copy = 0; copy < fs->table_count; copy++) {
        uint32_t lba = fs->fat_start_lba
                     + copy * fs->fat_size
                     + sector;
        // 1: read
        ide_read_sector(fs->drive, lba, buf);
        // 2: patch
        *(uint32_t *)(buf + idx) = value;
        // 3: write back
        ide_write_sector(fs->drive, lba, buf);
    }
    return 0;
}

/**
 * @brief Allocates a new cluster in the FAT32 filesystem.
 *
 * @param fs The FAT32 filesystem information.
 * @return uint32_t The cluster number of the allocated cluster, or 0 on failure.
 */
static uint32_t fat32_allocate_cluster(fat32_fs_info_t *fs)
{
    // scan the FAT looking for a zero entry
    uint32_t total = (fs->fat_size * fs->bytes_per_sector) / 4;
    for (uint32_t cl = 2; cl < total; cl++) {
        if (fat32_read_fat_entry(fs, cl) == 0) {
            // mark EOC
            fat32_write_fat_entry(fs, cl, FAT32_CLUSTER_END);
            return cl;
        }
    }
    return 0;
}

/**
 * @brief Writes a cluster to the FAT32 filesystem.
 *
 * @param fs The FAT32 filesystem information.
 * @param cluster The cluster number to write to.
 * @param buffer The buffer containing the data to write.
 */
static void fat32_write_cluster(fat32_fs_info_t *fs, uint32_t cluster, const uint8_t *buffer)
{
    uint32_t first_sector = fs->cluster_heap_start_lba
                          + (cluster - 2) * fs->sectors_per_cluster;

    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        ide_write_sector(fs->drive,
                         first_sector + i,
                         buffer + (i * fs->bytes_per_sector));
    }
}

/**
 * @brief Updates a directory entry in the FAT32 filesystem.
 *
 * @param ni The FAT32 node information.
 * @param name The name of the file or directory to update.
 * @param new_size The new size of the file or directory.
 */
static void fat32_update_dir_entry(fat32_node_info_t *ni, const char *name, uint32_t new_size) {
    fat32_fs_info_t *fs = ni->fs_info;
    uint8_t  key[11];
    fat32_build_name_key(name, key);

    uint32_t cluster = ni->parent_cluster;
    uint32_t csize   = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t *buf     = kernel_malloc(csize);

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;
        uint32_t per_cl = csize / sizeof(*ents);

        for (uint32_t i = 0; i < per_cl; i++) {
            if (memcmp(ents[i].name, key, 11) == 0) {
                ents[i].file_size = new_size;
                fat32_write_cluster(fs, cluster, buf);
                kernel_free(buf);
                return;
            }
        }
        cluster = fat32_read_fat_entry(fs, cluster);
    }
    kernel_free(buf);
}

/**
 * @brief Writes data to a file in the FAT32 filesystem.
 *
 * @param node The VFS node representing the file to write to.
 * @param offset The offset within the file to write to.
 * @param size The number of bytes to write.
 * @param buffer The buffer containing the data to write.
 * @return int 0 on success, or -1 on failure.
 */
static int fat32_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer)
{
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    fat32_node_info_t *ni = node->fs_data;
    fat32_fs_info_t   *fs = ni->fs_info;
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;

    // grow file size if needed
    if (offset + size > node->size) {
        node->size = offset + size;
    }

    // find starting cluster and offset
    uint32_t cluster = ni->cluster_number;
    if (cluster < 2) {
        // file had no clusters yet → allocate first one
        cluster = fat32_allocate_cluster(fs);
        if (!cluster) return -1;
        ni->cluster_number = cluster;
    }

    // skip to the cluster containing 'offset'
    uint32_t skip = offset / cluster_size;
    uint32_t cofs = offset % cluster_size;
    for (uint32_t i = 0; i < skip; i++) {
        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next >= FAT32_CLUSTER_END) {
            // need to allocate a new cluster in chain
            next = fat32_allocate_cluster(fs);
            if (!next) return -1;
            fat32_write_fat_entry(fs, cluster, next);
        }
        cluster = next;
    }

    // temp buffer for cluster writes
    uint8_t *clusbuf = kernel_malloc(cluster_size);
    uint32_t written = 0;

    while (written < size) {
        // read-modify if partial
        if (cofs || (size - written) < cluster_size) {
            fat32_read_cluster(fs, cluster, clusbuf);
        }

        // how much to copy
        uint32_t chunk = cluster_size - cofs;
        if (chunk > size - written) chunk = size - written;
        memcpy(clusbuf + cofs, buffer + written, chunk);

        // write it back
        fat32_write_cluster(fs, cluster, clusbuf);
        written += chunk;
        cofs = 0;

        if (written < size) {
            // move to next cluster (allocate if needed)
            uint32_t next = fat32_read_fat_entry(fs, cluster);
            if (next >= FAT32_CLUSTER_END) {
                next = fat32_allocate_cluster(fs);
                if (!next) break;
                fat32_write_fat_entry(fs, cluster, next);
            }
            cluster = next;
        }
    }

    kernel_free(clusbuf);
    fat32_update_dir_entry(ni, node->name, node->size);
    return written;
}

/**
 * @brief Locates a free directory entry in the FAT32 filesystem.
 *
 * @param fs The FAT32 filesystem information.
 * @param parent_cluster The cluster number of the parent directory.
 * @return fat_dir_entry_t* A pointer to the free directory entry, or NULL on failure.
 */
static fat_dir_entry_t *locate_free_entry(fat32_fs_info_t *fs, uint32_t parent_cluster) {
    uint32_t cluster = parent_cluster;
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t entries_per_cl = cluster_size / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(cluster_size);

    while (1) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;
        for (uint32_t i = 0; i < entries_per_cl; i++) {
            if (ents[i].name[0] == 0x00 || ents[i].name[0] == 0xE5) {
                // found free slot
                fat_dir_entry_t *slot = &ents[i];
                // write back cluster after caller fills slot
                // we return buffer and slot ptr; caller must fat32_write_cluster & free buf
                // but to simplify: we'll write cluster immediately after filling slot
                // so buf lives until then
                return slot;
            }
        }
        // end of chain?
        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next >= FAT32_CLUSTER_END) {
            // allocate new cluster for directory expansion
            uint32_t nc = fat32_allocate_cluster(fs);
            if (!nc) { kernel_free(buf); return NULL; }
            fat32_write_fat_entry(fs, cluster, nc);
            // zero new cluster
            memset(buf, 0, cluster_size);
            fat32_write_cluster(fs, nc, buf);
            cluster = nc;
        } else {
            cluster = next;
        }
    }
}

/**
 * @brief Creates a new file in the FAT32 filesystem.
 *
 * @param parent The parent directory in which to create the file.
 * @param name The name of the file to create.
 * @return vfs_node_t* The VFS node representing the new file, or NULL on failure.
 */
static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name) {
    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t   *fs  = pni->fs_info;
    uint32_t cluster = pni->cluster_number;
    uint32_t csize   = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint8_t *buf     = kernel_malloc(csize);

    // build the 11-byte raw name key
    uint8_t key[11];
    fat32_build_name_key(name, key);

    // 1: find or extend parent directory cluster for a free slot
    fat_dir_entry_t *slot = NULL;
    uint32_t entries = csize / sizeof(fat_dir_entry_t);
    while (!slot) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (void*)buf;
        for (uint32_t i = 0; i < entries; i++) {
            if (ents[i].name[0] == 0x00 || ents[i].name[0] == 0xE5) {
                slot = &ents[i];
                goto got_slot;
            }
        }
        // no slot, chain‐extend parent dir
        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next >= FAT32_CLUSTER_END) {
            next = fat32_allocate_cluster(fs);
            fat32_write_fat_entry(fs, cluster, next);
            memset(buf, 0, csize);
            fat32_write_cluster(fs, next, buf);
        }
        cluster = next;
    }
got_slot:
    // 2: allocate a data cluster for the new file
    uint32_t newcl = fat32_allocate_cluster(fs);

    // 3: fill the directory‐entry
    memcpy(slot->name, key, 11);
    slot->attr = 0x20;               // archive
    slot->first_cluster_high = newcl >> 16;
    slot->first_cluster_low  = newcl & 0xFFFF;
    slot->file_size = 0;

    // 4: commit parent-dir cluster
    fat32_write_cluster(fs, cluster, buf);
    kernel_free(buf);

    // 5: create the VFS node pointing at newcl
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child, 0, sizeof(*child));
    strcpy(child->name, name);
    child->flags = VFS_FLAG_FILE;
    child->refcount = 1;
    child->ops  = &fat32_ops;
    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = newcl;
    cni->parent_cluster = cluster;
    child->fs_data      = cni;
    return child;
}

/**
 * @brief Creates a new directory in the FAT32 filesystem.
 *
 * @param parent The parent directory in which to create the new directory.
 * @param name The name of the new directory.
 * @return vfs_node_t* The VFS node representing the new directory, or NULL on failure.
 */
static vfs_node_t *fat32_mkdir(vfs_node_t *parent, const char *name) {
    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t   *fs  = pni->fs_info;
    uint32_t parent_cl = pni->cluster_number;
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;

    // 1) Allocate a cluster for the new directory itself
    uint32_t newcl = fat32_allocate_cluster(fs);
    if (!newcl) return NULL;

    // 2: Build and write the "." / ".." entries into newcl
    uint8_t *buf = kernel_malloc(cluster_size);
    memset(buf, 0, cluster_size);
    fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

    // "." entry
    memset(ents[0].name, ' ', 11);
    ents[0].name[0] = '.';
    ents[0].attr   = FAT32_ATTR_DIRECTORY;
    ents[0].first_cluster_high = (newcl >> 16) & 0xFFFF;
    ents[0].first_cluster_low  = newcl & 0xFFFF;

    // ".." entry
    memset(ents[1].name, ' ', 11);
    ents[1].name[0] = '.';  ents[1].name[1] = '.';
    ents[1].attr   = FAT32_ATTR_DIRECTORY;
    ents[1].first_cluster_high = (parent_cl >> 16) & 0xFFFF;
    ents[1].first_cluster_low  = parent_cl & 0xFFFF;

    fat32_write_cluster(fs, newcl, buf);
    kernel_free(buf);

    // 3: Prepare the 11-byte FAT name key for "name"
    uint8_t key[11];
    fat32_build_name_key(name, key);

    // 4: Find a free slot in parent directory, possibly extending it
    uint32_t cl = parent_cl;
    buf = kernel_malloc(cluster_size);
    fat_dir_entry_t *slot = NULL;
    uint32_t entries_per_cl = cluster_size / sizeof(fat_dir_entry_t);

    while (!slot) {
        fat32_read_cluster(fs, cl, buf);
        ents = (fat_dir_entry_t *)buf;

        // scan for free (0x00 or 0xE5) entry
        for (uint32_t i = 0; i < entries_per_cl; i++) {
            if (ents[i].name[0] == 0x00 || ents[i].name[0] == 0xE5) {
                slot = &ents[i];
                break;
            }
        }
        if (!slot) {
            // need to extend parent dir
            uint32_t next = fat32_read_fat_entry(fs, cl);
            if (next >= FAT32_CLUSTER_END) {
                next = fat32_allocate_cluster(fs);
                if (!next) {
                    kernel_free(buf);
                    return NULL;
                }
                fat32_write_fat_entry(fs, cl, next);
                memset(buf, 0, cluster_size);
                fat32_write_cluster(fs, next, buf);
                cl = next;
            } else {
                cl = next;
            }
        }
    }

    // 5: Fill in the new directory entry in parent
    memcpy(slot->name, key, 11);
    slot->attr = FAT32_ATTR_DIRECTORY;
    slot->first_cluster_high = (newcl >> 16) & 0xFFFF;
    slot->first_cluster_low  = newcl & 0xFFFF;
    slot->file_size = 0;

    // commit the parent directory cluster back to disk
    fat32_write_cluster(fs, cl, buf);
    kernel_free(buf);

    // 6: Allocate and return the VFS node for the new directory
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child, 0, sizeof(*child));
    strncpy(child->name, name, sizeof(child->name));
    child->flags    = VFS_FLAG_DIRECTORY;
    child->refcount = 1;
    child->ops      = &fat32_ops;

    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = newcl;
    cni->parent_cluster = parent_cl;
    child->fs_data      = cni;

    return child;
}

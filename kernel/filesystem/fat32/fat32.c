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


void fat32_init(void) {
    vfs_register_fs(&fat32_fs);
}

vfs_node_t *fat32_mount(const char *device) {
    uint8_t drive = device ? (uint8_t)atoi(device) : 0;

    // 1: Read MBR
    uint8_t *mbr = kernel_malloc(512);
    ide_read_sector(drive, 0, mbr);
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        printf("fat32_mount: invalid MBR signature %02X %02X\n",
               mbr[510], mbr[511]);
        kernel_free(mbr);
        return NULL;
    }
    uint32_t part1 = mbr[454] | (mbr[455]<<8) | (mbr[456]<<16) | (mbr[457]<<24);
    printf("FAT32 mount: partition1 @ LBA %u\n", part1);
    kernel_free(mbr);

    // 2: Read Boot Sector
    uint8_t *boot = kernel_malloc(512);
    ide_read_sector(drive, part1, boot);
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        printf("fat32_mount: invalid BS sig %02X %02X\n",
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

    printf("fat32: mounted drive %u, root cluster %u\n",
           drive, fs_info->root_cluster);
    return root;
}

static void fat32_read_cluster(fat32_fs_info_t *fs_info, uint32_t cluster, uint8_t *buffer) {
    uint32_t first_sector = fs_info->cluster_heap_start_lba + (cluster - 2) * fs_info->sectors_per_cluster;

    for (uint8_t i = 0; i < fs_info->sectors_per_cluster; i++) {
        ide_read_sector(fs_info->drive, first_sector + i, buffer + (i * fs_info->bytes_per_sector));
    }
}

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

                strcpy(child->name, name);

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

static int fat32_read(vfs_node_t *node,
                      uint32_t offset,
                      uint32_t size,
                      char *buffer)
{
    printf("okhere\n");
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

static int fat32_open(vfs_node_t *node) {
    printf("fat32_open: opening node %s\n", node->name);

    // no per‐node initialization needed
    return 0;
}

static int fat32_close(vfs_node_t *node) {
    // no per‐node teardown needed
    return 0;
}

static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name) {
    // node is a directory
    fat32_node_info_t *ni = node->fs_data;
    fat32_fs_info_t   *fs = ni->fs_info;
    uint32_t           cluster = ni->cluster_number;

    printf("finddir: looking for '%s' in dir cluster %u\n",
       name, ni->cluster_number);


    uint32_t per_cluster = (fs->sectors_per_cluster * fs->bytes_per_sector)
                           / sizeof(fat_dir_entry_t);
    uint8_t  buf[fs->sectors_per_cluster * fs->bytes_per_sector];

    while (cluster < FAT32_CLUSTER_END) {
        // read one directory‐cluster
        fat32_read_cluster(fs, cluster, buf);

        fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;
        for (uint32_t i = 0; i < per_cluster; i++) {
            // end‐of‐list
            if (ents[i].name[0] == 0x00) return NULL;
            // skip deleted or volume label
            if (ents[i].name[0] == 0xE5 ||
                (ents[i].attr & FAT32_ATTR_VOLUME_ID))
                continue;

            // build 8.3 name
            char nm[12];
            memcpy(nm, ents[i].name, 11);
            nm[11] = '\0';
            // trim spaces
            for (int j = 10; j >= 0; j--) {
                if (nm[j] == ' ') nm[j] = '\0';
                else break;
            }

            if (strcasecmp(nm, name) == 0) {
                // found it!
                vfs_node_t *child = kernel_malloc(sizeof(*child));
                memset(child, 0, sizeof(*child));
                strcpy(child->name, nm);
                child->inode    = (uint32_t)child;               // or some unique
                child->flags    = (ents[i].attr & FAT32_ATTR_DIRECTORY)
                                  ? VFS_FLAG_DIRECTORY
                                  : VFS_FLAG_FILE;
                child->size     = ents[i].file_size;
                child->ops      = &fat32_ops;
                child->refcount = 1;

                // store its cluster
                fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
                cni->fs_info        = fs;
                cni->cluster_number = (ents[i].first_cluster_high << 16)
                                     | ents[i].first_cluster_low;
                child->fs_data      = cni;

                return child;
            }
        }

        // advance to next cluster
        cluster = fat32_read_fat_entry(fs, cluster);
    }

    return NULL;
}

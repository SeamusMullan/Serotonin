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
                child->fs_data      = cni;
                return child;
            }
        }

        // next cluster
        cluster = fat32_read_fat_entry(fs, cluster);
    }

    return NULL;
}

static int fat32_write_fat_entry(fat32_fs_info_t *fs, uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset       = cluster * 4;
    uint32_t fat_sector       = fs->fat_start_lba
                              + (fat_offset / fs->bytes_per_sector);
    uint32_t offset_in_sector = fat_offset % fs->bytes_per_sector;

    // read the sector
    uint8_t *sector = kernel_malloc(fs->bytes_per_sector);
    ide_read_sector(fs->drive, fat_sector, sector);

    // update the 4-byte entry
    uint32_t *entry = (uint32_t*)(sector + offset_in_sector);
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);

    // write it back
    for (uint16_t i = 0; i < fs->bytes_per_sector / 2; i++) {
        // write 16-bit words
        uint16_t w = ((uint16_t*)sector)[i];
        // we need outw support; assume you have outw()
        outw(ATA_PRIMARY_IO, w);
    }
    kernel_free(sector);
    return 0;
}

static uint32_t fat32_allocate_cluster(fat32_fs_info_t *fs)
{
    // scan the FAT looking for a zero entry
    uint32_t total_clusters = fs->fat_size * fs->bytes_per_sector / 4;
    for (uint32_t cl = 2; cl < total_clusters; cl++) {
        if (fat32_read_fat_entry(fs, cl) == 0) {
            // mark end-of-chain
            fat32_write_fat_entry(fs, cl, FAT32_CLUSTER_END);
            return cl;
        }
    }
    return 0; // no free cluster
}

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
    return written;
}

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

static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name) {
    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t   *fs  = pni->fs_info;
    uint32_t           parent_cluster = pni->cluster_number;

    uint8_t key[11];
    fat32_build_name_key(name, key);

    // load cluster and find free slot
    uint32_t cluster = parent_cluster;
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint8_t *buf = kernel_malloc(cluster_size);
    fat_dir_entry_t *slot = NULL;

    // locate_free_entry simplified inline:
    {
        uint32_t entries_per_cl = cluster_size / sizeof(fat_dir_entry_t);
        while (!slot) {
            fat32_read_cluster(fs, cluster, buf);
            fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;
            for (uint32_t i=0; i<entries_per_cl; i++) {
                if (ents[i].name[0]==0x00 || ents[i].name[0]==0xE5) {
                    slot = &ents[i];
                    break;
                }
            }
            if (!slot) {
                uint32_t next = fat32_read_fat_entry(fs, cluster);
                if (next >= FAT32_CLUSTER_END) {
                    next = fat32_allocate_cluster(fs);
                    if (!next) { kernel_free(buf); return NULL; }
                    fat32_write_fat_entry(fs, cluster, next);
                    memset(buf,0,cluster_size);
                    fat32_write_cluster(fs, next, buf);
                    cluster = next;
                } else {
                    cluster = next;
                }
            }
        }
    }

    // fill the slot
    memcpy(slot->name, key, 11);
    slot->attr  = 0x20;         // archive bit
    slot->reserved = 0;
    slot->creation_time_tenths = 0;
    slot->creation_time  = 0;
    slot->creation_date  = 0;
    slot->last_access_date = 0;
    slot->first_cluster_high = 0;
    slot->write_time   = 0;
    slot->write_date   = 0;
    slot->first_cluster_low  = 0;
    slot->file_size    = 0;

    // commit
    fat32_write_cluster(fs, cluster, buf);
    kernel_free(buf);

    // now build a VFS node for it
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child,0,sizeof(*child));
    strcpy(child->name, name);
    child->inode    = (uint32_t)child;
    child->flags    = VFS_FLAG_FILE;
    child->size     = 0;
    child->refcount = 1;
    child->ops      = &fat32_ops;

    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = 0;  // no data cluster yet
    child->fs_data      = cni;
    return child;
}

static vfs_node_t *fat32_mkdir(vfs_node_t *parent, const char *name) {
    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t   *fs  = pni->fs_info;
    uint32_t           parent_cluster = pni->cluster_number;

    uint8_t key[11];
    fat32_build_name_key(name, key);

    // allocate a new cluster for the directory itself
    uint32_t newcl = fat32_allocate_cluster(fs);
    if (!newcl) return NULL;

    // init its cluster with . and .. entries
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint8_t *buf = kernel_malloc(cluster_size);
    memset(buf,0,cluster_size);

    fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;
    // "." entry
    memcpy(ents[0].name, key, 11);   // actually name="." → raw ".       "
    memset(ents[0].name,' ',11);
    ents[0].name[0] = '.';
    ents[0].attr = FAT32_ATTR_DIRECTORY;
    ents[0].first_cluster_high = (newcl >> 16) & 0xFFFF;
    ents[0].first_cluster_low  = newcl & 0xFFFF;
    ents[0].file_size = 0;
    // ".." entry
    memset(ents[1].name,' ',11);
    ents[1].name[0] = '.';
    ents[1].name[1] = '.';
    ents[1].attr = FAT32_ATTR_DIRECTORY;
    ents[1].first_cluster_high = (parent_cluster >> 16) & 0xFFFF;
    ents[1].first_cluster_low  = parent_cluster & 0xFFFF;
    ents[1].file_size = 0;

    fat32_write_cluster(fs, newcl, buf);
    kernel_free(buf);

    // now add its entry into the parent dir (reuse fat32_create logic, but with DIR attr)
    // so we find a free slot in parent as above
    // (extracted earlier code)
    uint32_t cluster = parent_cluster;
    uint8_t *pbuf = kernel_malloc(cluster_size);
    fat_dir_entry_t *slot = NULL;
    uint32_t entries_per_cl = cluster_size / sizeof(fat_dir_entry_t);
    while (!slot) {
        fat32_read_cluster(fs, cluster, pbuf);
        fat_dir_entry_t *pe = (fat_dir_entry_t*)pbuf;
        for (uint32_t i=0;i<entries_per_cl;i++){
            if (pe[i].name[0]==0x00 || pe[i].name[0]==0xE5){
                slot = &pe[i];
                break;
            }
        }
        if (!slot) {
            uint32_t next = fat32_read_fat_entry(fs, cluster);
            if (next >= FAT32_CLUSTER_END) {
                next = fat32_allocate_cluster(fs);
                if (!next) { kernel_free(pbuf); return NULL; }
                fat32_write_fat_entry(fs, cluster, next);
                memset(pbuf,0,cluster_size);
                fat32_write_cluster(fs, next, pbuf);
                cluster = next;
            } else {
                cluster = next;
            }
        }
    }

    memcpy(slot->name, key, 11);
    slot->attr = FAT32_ATTR_DIRECTORY;
    slot->first_cluster_high = (newcl >> 16)&0xFFFF;
    slot->first_cluster_low  = newcl & 0xFFFF;
    slot->file_size = 0;
    fat32_write_cluster(fs, cluster, pbuf);
    kernel_free(pbuf);

    // build a VFS node
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child,0,sizeof(*child));
    strcpy(child->name, name);
    child->inode    = (uint32_t)child;
    child->flags    = VFS_FLAG_DIRECTORY;
    child->size     = 0;
    child->refcount = 1;
    child->ops      = &fat32_ops;

    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = newcl;
    child->fs_data      = cni;
    return child;
}

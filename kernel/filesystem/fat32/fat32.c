#include <kernel/filesystem/fat32/fat32.h>
#include <stdint.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/io/io.h>
#include <kernel/kernel.h>
#include <kernel/string.h>
#include <kernel/filesystem/vfs.h>
#include <kernel/filesystem/ide.h>
#include <kernel/filesystem/blkcache.h>
#include <kernel/device/ide/ide_pci.h>
#include <kernel/syscall/sys/file.h>

static void fat32_read_cluster(fat32_fs_info_t *fs_info, uint32_t cluster, uint8_t *buffer);
static uint32_t fat32_read_fat_entry(fat32_fs_info_t *fs_info, uint32_t cluster);
static void fat32_flush_fat_cache(fat32_fs_info_t *fs);

static int fat32_read(vfs_node_t *node, uint32_t offset, uint32_t size, char *buffer);
static int fat32_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
static int fat32_truncate(vfs_node_t *node, uint32_t size);
static int fat32_unlink(vfs_node_t *parent, const char *name);
static int fat32_rmdir(vfs_node_t *parent, const char *name);
static int fat32_open(vfs_node_t *node);
static int fat32_close(vfs_node_t *node);
static vfs_node_t *fat32_finddir(vfs_node_t *dir, const char *name);
static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name);
static vfs_node_t *fat32_mkdir(vfs_node_t *parent, const char *name);

vfs_ops_t fat32_ops = {
    .read    = fat32_read,
    .write   = fat32_write,
    .truncate = fat32_truncate,
    .unlink  = fat32_unlink,
    .rmdir   = fat32_rmdir,
    .open    = fat32_open,
    .close   = fat32_close,
    .readdir = fat32_readdir,
    .finddir = fat32_finddir,
    .create  = fat32_create,
    .mkdir   = fat32_mkdir
};

filesystem_t fat32_fs = {
    .name = "fat32",
    .mount = fat32_mount,
    .next = (void *)0
};

static int fat32_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static uint8_t fat32_lfn_checksum(const uint8_t name[11]) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i];
    return sum;
}

static int fat32_name_is_8dot3(const char *name) {
    const char *dot = strchr(name, '.');
    int baselen = dot ? (int)(dot - name) : (int)strlen(name);
    if (baselen < 1 || baselen > 8) return 0;
    if (dot) {
        int extlen = strlen(dot + 1);
        if (extlen < 1 || extlen > 3) return 0;
        if (strchr(dot + 1, '.')) return 0; // multiple dots
    }
    // check characters are valid 8.3 (uppercase alpha, digits, some specials)
    for (const char *p = name; *p; p++) {
        if (*p == '.') continue;
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') return 0; // lowercase, needs LFN
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
            c == '\'' || c == '(' || c == ')' || c == '-' || c == '@' ||
            c == '^' || c == '_' || c == '`' || c == '{' || c == '}' ||
            c == '~') continue;
        if (c >= 128) continue; // high-ASCII ok in short names
        return 0;
    }
    return 1;
}

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
 * @brief Generate a unique 8.3 short name with a numeric tail (~1, ~2, etc).
 *
 * @param fname    Long file name
 * @param key      Output 11-byte short name
 * @param fs       Filesystem info (for checking collisions)
 * @param dir_cluster  Directory cluster to check for collisions
 */
static void fat32_generate_short_name(const char *fname, uint8_t key[11], fat32_fs_info_t *fs, uint32_t dir_cluster)
{
    // Start with the basic 8.3 conversion
    memset(key, ' ', 11);

    // Strip leading dots and spaces
    while (*fname == '.' || *fname == ' ') fname++;

    // Find the last dot for the extension
    const char *last_dot = NULL;
    for (const char *p = fname; *p; p++)
        if (*p == '.') last_dot = p;

    // Copy basename (skip dots, max 6 chars for ~N suffix room)
    int pos = 0;
    for (const char *p = fname; *p && p != last_dot && pos < 6; p++) {
        if (*p == ' ' || *p == '.') continue;
        key[pos++] = toupper((unsigned char)*p);
    }
    if (pos == 0) {
        // fallback
        key[0] = '_';
        pos = 1;
    }

    // Copy extension
    if (last_dot) {
        int epos = 0;
        for (const char *p = last_dot + 1; *p && epos < 3; p++) {
            if (*p == ' ') continue;
            key[8 + epos++] = toupper((unsigned char)*p);
        }
    }

    // Try ~1 through ~9999
    uint32_t cluster_size = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t entries_per_cl = cluster_size / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(cluster_size);

    for (int tail = 1; tail < 10000; tail++) {
        // Build the tail string
        char tailstr[8];
        tailstr[0] = '~';
        int tlen = 1;
        int tmp = tail;
        char digits[5];
        int dlen = 0;
        while (tmp > 0) { digits[dlen++] = '0' + (tmp % 10); tmp /= 10; }
        for (int d = dlen - 1; d >= 0; d--)
            tailstr[tlen++] = digits[d];

        // Truncate base to fit tail
        int base_max = 8 - tlen;
        if (pos < base_max) base_max = pos;
        for (int i = 0; i < base_max; i++)
            key[i] = key[i]; // already set
        for (int i = 0; i < tlen; i++)
            key[base_max + i] = tailstr[i];
        for (int i = base_max + tlen; i < 8; i++)
            key[i] = ' ';

        // Check if this short name already exists in the directory
        int found = 0;
        uint32_t cl = dir_cluster;
        while (cl < FAT32_CLUSTER_END && !found) {
            fat32_read_cluster(fs, cl, buf);
            fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;
            for (uint32_t i = 0; i < entries_per_cl; i++) {
                uint8_t first = (uint8_t)ents[i].name[0];
                if (first == 0x00) goto unique;
                if (first == 0xE5) continue;
                if ((ents[i].attr & 0x0F) == 0x0F) continue;
                if (memcmp(ents[i].name, key, 11) == 0) {
                    found = 1;
                    break;
                }
            }
            cl = fat32_read_fat_entry(fs, cl);
        }
        if (!found) goto unique;
    }
unique:
    kernel_free(buf);
}

/**
 * @brief Extract the 13 UCS-2 characters from an LFN entry into a char buffer.
 *
 * @param lfn   The LFN entry
 * @param out   Output buffer (must hold at least 13 chars)
 * @return Number of characters extracted (may include 0xFFFF padding)
 */
static int fat32_lfn_extract_chars(const fat_lfn_entry_t *lfn, char *out) {
    int pos = 0;
    for (int i = 0; i < 5; i++)
        out[pos++] = (lfn->name1[i] == 0xFFFF || lfn->name1[i] == 0) ? '\0' : (char)(lfn->name1[i] & 0xFF);
    for (int i = 0; i < 6; i++)
        out[pos++] = (lfn->name2[i] == 0xFFFF || lfn->name2[i] == 0) ? '\0' : (char)(lfn->name2[i] & 0xFF);
    for (int i = 0; i < 2; i++)
        out[pos++] = (lfn->name3[i] == 0xFFFF || lfn->name3[i] == 0) ? '\0' : (char)(lfn->name3[i] & 0xFF);
    return 13;
}

/**
 * @brief Fill an LFN entry with up to 13 characters from a name.
 *
 * @param lfn    The LFN entry to fill
 * @param name   Source name string
 * @param offset Character offset into the name to start from
 * @param namelen Total length of the name
 */
static void fat32_lfn_fill_entry(fat_lfn_entry_t *lfn, const char *name, int offset, int namelen) {
    int src = offset;
    // name1: chars 0-4
    for (int i = 0; i < 5; i++) {
        if (src < namelen)       lfn->name1[i] = (uint16_t)(unsigned char)name[src++];
        else if (src == namelen) { lfn->name1[i] = 0x0000; src++; }
        else                     lfn->name1[i] = 0xFFFF;
    }
    // name2: chars 5-10
    for (int i = 0; i < 6; i++) {
        if (src < namelen)       lfn->name2[i] = (uint16_t)(unsigned char)name[src++];
        else if (src == namelen) { lfn->name2[i] = 0x0000; src++; }
        else                     lfn->name2[i] = 0xFFFF;
    }
    // name3: chars 11-12
    for (int i = 0; i < 2; i++) {
        if (src < namelen)       lfn->name3[i] = (uint16_t)(unsigned char)name[src++];
        else if (src == namelen) { lfn->name3[i] = 0x0000; src++; }
        else                     lfn->name3[i] = 0xFFFF;
    }
}

/**
 * @brief Extracts a readable filename from a FAT32 8.3 directory entry.
 *
 * The name is returned in lowercase for consistency, since the original
 * case information is lost in 8.3 entries.
 */
static void fat32_name_from_entry(const uint8_t raw[11], char out[13]) {
    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (raw[i] == ' ')
            break;
        out[pos++] = tolower(raw[i]);
    }

    int extstart = 8;
    while (extstart < 11 && raw[extstart] == ' ')
        extstart++;
    if (extstart < 11) {
        out[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (raw[i] == ' ')
                break;
            out[pos++] = tolower(raw[i]);
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
    printfs(PRINT_STATUS_INFO,"fat32_mount: partition1 @ LBA %u\n", part1);
    kernel_free(mbr);

    // 2: Read Boot Sector
    uint8_t *boot = kernel_malloc(512);
    ide_read_sector(drive, part1, boot);
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        printfs(PRINT_STATUS_ERROR,"fat32_mount: invalid BS sig %02x %02x\n",
               boot[510], boot[511]);
        kernel_free(boot);
        return NULL;
    }

    // 3: Parse BPB
    fat32_fs_info_t *fs_info = kernel_malloc(sizeof(*fs_info));
    fat32_parse_bpb(fs_info, drive, part1, boot);
    kernel_free(boot);

    // Initialize FAT entry cache (up to 64 sectors = 32KB, covers 8192 clusters)
    uint32_t cache_secs = fs_info->fat_size;
    if (cache_secs > 64) cache_secs = 64;
    fs_info->fat_cache = kernel_malloc(cache_secs * 512);
    fs_info->fat_cache_start = 0;
    fs_info->fat_cache_sectors = cache_secs;
    fs_info->fat_cache_dirty = 0;
    if (fs_info->fat_cache) {
        blkcache_read_sectors(fs_info->drive, fs_info->fat_start_lba,
                              (uint8_t)cache_secs, fs_info->fat_cache);
    }

    // 4: Allocate root vfs_node
    vfs_node_t *root = kernel_malloc(sizeof(*root));
    memset(root, 0, sizeof(*root));
    strcpy(root->name, "/");
    root->inode    = 0;
    root->flags    = VFS_FLAG_DIRECTORY | VFS_FLAG_DISKIO;
    root->refcount = 1;
    root->ops      = &fat32_ops;
    root->uid      = 0;
    root->gid      = 0;
    root->mode     = S_IFDIR | 0755;

    // 5: Store fs_info + root cluster in fs_data
    fat32_node_info_t *ni = kernel_malloc(sizeof(*ni));
    ni->fs_info       = fs_info;
    ni->cluster_number= fs_info->root_cluster;
    root->fs_data     = ni;

    printfs(PRINT_STATUS_INFO,"fat32: mounted drive %u, root cluster %u\n",
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
    if (!buffer || cluster < 2 || cluster >= FAT32_CLUSTER_END) {
        if (buffer && fs_info) {
            uint32_t cluster_size = fs_info->bytes_per_sector * fs_info->sectors_per_cluster;
            memset(buffer, 0, cluster_size);
        }
        return;
    }
    uint32_t first_sector = fs_info->cluster_heap_start_lba + (cluster - 2) * fs_info->sectors_per_cluster;
    blkcache_read_sectors(fs_info->drive, first_sector, fs_info->sectors_per_cluster, buffer);
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
    uint32_t sector_in_fat = fat_offset / fs_info->bytes_per_sector;
    uint32_t offset_in_sector = fat_offset % fs_info->bytes_per_sector;

    // check FAT cache
    if (fs_info->fat_cache &&
        sector_in_fat >= fs_info->fat_cache_start &&
        sector_in_fat < fs_info->fat_cache_start + fs_info->fat_cache_sectors) {
        uint32_t cache_off = (sector_in_fat - fs_info->fat_cache_start) * fs_info->bytes_per_sector
                           + offset_in_sector;
        uint32_t entry = *(uint32_t *)(fs_info->fat_cache + cache_off);
        return entry & 0x0FFFFFFF;
    }

    // cache miss - reload window centered on requested sector
    if (fs_info->fat_cache) {
        if (fs_info->fat_cache_dirty)
            fat32_flush_fat_cache(fs_info);

        uint32_t new_start = 0;
        if (sector_in_fat >= fs_info->fat_cache_sectors / 2)
            new_start = sector_in_fat - fs_info->fat_cache_sectors / 2;
        if (new_start + fs_info->fat_cache_sectors > fs_info->fat_size)
            new_start = fs_info->fat_size - fs_info->fat_cache_sectors;

        blkcache_read_sectors(fs_info->drive, fs_info->fat_start_lba + new_start,
                              (uint8_t)fs_info->fat_cache_sectors, fs_info->fat_cache);
        fs_info->fat_cache_start = new_start;

        uint32_t cache_off = (sector_in_fat - new_start) * fs_info->bytes_per_sector
                           + offset_in_sector;
        uint32_t entry = *(uint32_t *)(fs_info->fat_cache + cache_off);
        return entry & 0x0FFFFFFF;
    }

    // no cache - fallback to direct read
    uint8_t sector[512];
    blkcache_read_sector(fs_info->drive, fs_info->fat_start_lba + sector_in_fat, sector);
    uint32_t entry = *(uint32_t *)(sector + offset_in_sector);
    return entry & 0x0FFFFFFF;
}

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

    uint32_t cluster_size = fs_info->sectors_per_cluster * fs_info->bytes_per_sector;
    uint8_t *cluster_buffer = kernel_malloc(cluster_size);
    uint32_t entries_per_cluster = cluster_size / sizeof(fat_dir_entry_t);

    // Track LFN fragments as we scan forward
    char lfn_buf[256];
    int lfn_pos = 0;
    int lfn_valid = 0;
    uint8_t lfn_checksum = 0;
    char lfn_fragments[20][13];
    int lfn_frag_count = 0;

    uint32_t entry_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs_info, cluster, cluster_buffer);

        fat_dir_entry_t *entries = (fat_dir_entry_t *)cluster_buffer;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first = (uint8_t)entries[i].name[0];
            if (first == 0x00) {
                kernel_free(cluster_buffer);
                return NULL;
            }
            if (first == 0xE5) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            // Collect LFN entries
            if ((entries[i].attr & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&entries[i];
                if (lfn->order & 0x40) {
                    // First LFN entry (last in name sequence)
                    lfn_frag_count = 0;
                    lfn_checksum = lfn->checksum;
                    lfn_valid = 1;
                }
                if (lfn_valid && lfn->checksum == lfn_checksum) {
                    fat32_lfn_extract_chars(lfn, lfn_fragments[lfn_frag_count]);
                    lfn_frag_count++;
                } else {
                    lfn_valid = 0;
                    lfn_frag_count = 0;
                }
                continue;
            }

            // Skip volume ID
            if (entries[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }
            if (first == ' ') {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            // This is a real short-name entry
            // Check if accumulated LFN matches this short entry
            int have_lfn = 0;
            if (lfn_valid && lfn_frag_count > 0) {
                uint8_t ck = fat32_lfn_checksum((uint8_t *)entries[i].name);
                if (ck == lfn_checksum) {
                    // Assemble LFN (fragments are in reverse order)
                    lfn_pos = 0;
                    for (int f = lfn_frag_count - 1; f >= 0; f--) {
                        for (int c = 0; c < 13 && lfn_pos < 255; c++) {
                            if (lfn_fragments[f][c] == '\0') goto lfn_done;
                            lfn_buf[lfn_pos++] = lfn_fragments[f][c];
                        }
                    }
                lfn_done:
                    lfn_buf[lfn_pos] = '\0';
                    have_lfn = 1;
                }
            }

            if (entry_count == index) {
                vfs_node_t *child = kernel_malloc(sizeof(vfs_node_t));
                memset(child, 0, sizeof(vfs_node_t));

                if (have_lfn) {
                    strncpy(child->name, lfn_buf, sizeof(child->name));
                } else {
                    char friendly[13];
                    fat32_name_from_entry((uint8_t *)entries[i].name, friendly);
                    strncpy(child->name, friendly, sizeof(child->name));
                }
                child->name[sizeof(child->name) - 1] = '\0';

                child->inode = ((entries[i].first_cluster_high << 16) | entries[i].first_cluster_low);
                if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
                    child->flags = VFS_FLAG_DIRECTORY | VFS_FLAG_DISKIO;
                    child->mode = S_IFDIR | 0755;
                } else {
                    child->flags = VFS_FLAG_FILE | VFS_FLAG_DISKIO;
                    child->mode = S_IFREG | ((entries[i].attr & 0x01) ? 0444 : 0644);
                }
                child->size = entries[i].file_size;
                child->refcount = 0;
                child->ops = node->ops;
                child->uid = 0;
                child->gid = 0;

                fat32_node_info_t *child_info = kernel_malloc(sizeof(fat32_node_info_t));
                child_info->fs_info = fs_info;
                child_info->cluster_number = ((entries[i].first_cluster_high << 16) | entries[i].first_cluster_low);
                child_info->parent_cluster = cluster;
                child->fs_data = child_info;

                kernel_free(cluster_buffer);
                return child;
            }

            entry_count++;
            lfn_valid = 0;
            lfn_frag_count = 0;
        }

        cluster = fat32_read_fat_entry(fs_info, cluster);
    }

    kernel_free(cluster_buffer);
    return NULL;
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
static uint32_t fat32_resolve_chain(fat32_fs_info_t *fs, uint32_t start,
                                    uint32_t *chain, uint32_t max) {
    uint32_t count = 0;
    uint32_t cl = start;
    while (cl >= 2 && cl < FAT32_CLUSTER_END && count < max) {
        chain[count++] = cl;
        cl = fat32_read_fat_entry(fs, cl);
    }
    return count;
}

static int fat32_read(vfs_node_t *node,
                      uint32_t offset,
                      uint32_t size,
                      char *buffer)
{
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    if (offset >= node->size) return 0;
    if (offset + size > node->size)
        size = node->size - offset;

    fat32_node_info_t *ni  = (fat32_node_info_t*)node->fs_data;
    fat32_fs_info_t   *fs  = ni->fs_info;
    uint32_t cluster_size   = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t spc = fs->sectors_per_cluster;

    // walk to the cluster containing 'offset'
    uint32_t skip = offset / cluster_size;
    uint32_t cofs = offset % cluster_size;
    uint32_t start_cluster = ni->cluster_number;
    for (uint32_t i = 0; i < skip; i++) {
        start_cluster = fat32_read_fat_entry(fs, start_cluster);
        if (start_cluster < 2 || start_cluster >= FAT32_CLUSTER_END) return 0;
    }

    uint32_t chain[128];
    uint32_t read_total = 0;
    uint32_t cur_cluster = start_cluster;

    while (read_total < size) {
        // resolve up to 128 clusters from the current position
        uint32_t chain_len = fat32_resolve_chain(fs, cur_cluster, chain, 128);
        if (chain_len == 0) break;

        uint32_t ci = 0;
        while (read_total < size && ci < chain_len) {
            // find contiguous run of clusters (cap so total sectors fits in uint8_t)
            uint32_t max_run = 255 / spc;
            if (max_run < 1) max_run = 1;
            uint32_t run_len = 1;
            while (ci + run_len < chain_len &&
                   chain[ci + run_len] == chain[ci + run_len - 1] + 1 &&
                   run_len < max_run)
                run_len++;

            uint32_t run_bytes = run_len * cluster_size - cofs;
            if (run_bytes > size - read_total) run_bytes = size - read_total;

            uint32_t first_lba = fs->cluster_heap_start_lba
                               + (chain[ci] - 2) * spc;
            uint32_t total_sectors = run_len * spc;

            if (cofs == 0 && run_bytes == run_len * cluster_size) {
                // aligned: read directly into output buffer
                blkcache_read_sectors(fs->drive, first_lba,
                                      (uint8_t)total_sectors,
                                      (uint8_t *)(buffer + read_total));
            } else {
                // partial: use temp buffer
                uint8_t *tmp = kernel_malloc(run_len * cluster_size);
                if (!tmp) return read_total > 0 ? (int)read_total : -1;
                blkcache_read_sectors(fs->drive, first_lba,
                                      (uint8_t)total_sectors, tmp);
                memcpy(buffer + read_total, tmp + cofs, run_bytes);
                kernel_free(tmp);
            }

            read_total += run_bytes;
            ci += run_len;
            cofs = 0;
        }

        // advance cur_cluster past the chain we just consumed
        cur_cluster = fat32_read_fat_entry(fs, chain[chain_len - 1]);
    }

    return read_total;
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
    // build the 11-byte key for 8.3 comparison
    uint8_t key[11];
    fat32_build_name_key(name, key);

    fat32_node_info_t *ni = dir->fs_data;
    fat32_fs_info_t   *fs = ni->fs_info;
    uint32_t           cluster = ni->cluster_number;

    uint32_t per_cluster =
      (fs->sectors_per_cluster * fs->bytes_per_sector)
      / sizeof(fat_dir_entry_t);

    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t *scratch = kernel_malloc(cluster_size);

    // Track LFN fragments as we scan forward
    char lfn_buf[256];
    int lfn_pos = 0;
    int lfn_valid = 0;
    uint8_t lfn_checksum = 0;
    char lfn_fragments[20][13];
    int lfn_frag_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, scratch);
        fat_dir_entry_t *ents = (fat_dir_entry_t*)scratch;

        for (uint32_t i = 0; i < per_cluster; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00) {
                kernel_free(scratch);
                return NULL;
            }
            if (first == 0xE5) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            // Collect LFN entries
            if ((ents[i].attr & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[i];
                if (lfn->order & 0x40) {
                    lfn_frag_count = 0;
                    lfn_checksum = lfn->checksum;
                    lfn_valid = 1;
                }
                if (lfn_valid && lfn->checksum == lfn_checksum) {
                    fat32_lfn_extract_chars(lfn, lfn_fragments[lfn_frag_count]);
                    lfn_frag_count++;
                } else {
                    lfn_valid = 0;
                    lfn_frag_count = 0;
                }
                continue;
            }

            // Skip volume label
            if (ents[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }
            if (first == ' ') {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            // Real short-name entry — check for match
            int matched = 0;

            // 1) Check LFN match (case-insensitive)
            if (lfn_valid && lfn_frag_count > 0) {
                uint8_t ck = fat32_lfn_checksum((uint8_t *)ents[i].name);
                if (ck == lfn_checksum) {
                    lfn_pos = 0;
                    for (int f = lfn_frag_count - 1; f >= 0; f--) {
                        for (int c = 0; c < 13 && lfn_pos < 255; c++) {
                            if (lfn_fragments[f][c] == '\0') goto finddir_lfn_done;
                            lfn_buf[lfn_pos++] = lfn_fragments[f][c];
                        }
                    }
                finddir_lfn_done:
                    lfn_buf[lfn_pos] = '\0';
                    if (fat32_strcasecmp(name, lfn_buf) == 0)
                        matched = 1;
                }
            }

            // 2) Check 8.3 match (always case-insensitive via uppercased key)
            if (!matched && memcmp(key, ents[i].name, 11) == 0)
                matched = 1;

            if (matched) {
                vfs_node_t *child = kernel_malloc(sizeof(*child));
                memset(child, 0, sizeof(*child));
                strncpy(child->name, name, sizeof(child->name));
                child->name[sizeof(child->name) - 1] = '\0';
                child->inode    = ((ents[i].first_cluster_high << 16) | ents[i].first_cluster_low);
                child->flags    = (ents[i].attr & FAT32_ATTR_DIRECTORY)
                                  ? (VFS_FLAG_DIRECTORY | VFS_FLAG_DISKIO)
                                  : (VFS_FLAG_FILE | VFS_FLAG_DISKIO);
                child->size     = ents[i].file_size;
                child->ops      = &fat32_ops;
                child->refcount = 0;
                child->uid      = 0;
                child->gid      = 0;
                if (ents[i].attr & FAT32_ATTR_DIRECTORY) {
                    child->mode = S_IFDIR | 0755;
                } else {
                    child->mode = S_IFREG | ((ents[i].attr & 0x01) ? 0444 : 0644);
                }

                fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
                cni->fs_info        = fs;
                cni->cluster_number = (ents[i].first_cluster_high << 16)
                                      | ents[i].first_cluster_low;
                cni->parent_cluster = cluster;
                child->fs_data      = cni;
                kernel_free(scratch);
                return child;
            }

            lfn_valid = 0;
            lfn_frag_count = 0;
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    kernel_free(scratch);
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
    value &= 0x0FFFFFFF;

    uint32_t off    = cluster * 4;
    uint32_t sector_in_fat = off / fs->bytes_per_sector;
    uint32_t idx    = off % fs->bytes_per_sector;

    // try to write into FAT cache
    if (fs->fat_cache &&
        sector_in_fat >= fs->fat_cache_start &&
        sector_in_fat < fs->fat_cache_start + fs->fat_cache_sectors) {
        uint32_t cache_off = (sector_in_fat - fs->fat_cache_start) * fs->bytes_per_sector + idx;
        *(uint32_t *)(fs->fat_cache + cache_off) = value;
        fs->fat_cache_dirty = 1;
        return 0;
    }

    // ensure sector is in cache by reading it first (forces window slide if needed)
    if (fs->fat_cache) {
        fat32_read_fat_entry(fs, cluster);
        // now it should be in the cache window
        if (sector_in_fat >= fs->fat_cache_start &&
            sector_in_fat < fs->fat_cache_start + fs->fat_cache_sectors) {
            uint32_t cache_off = (sector_in_fat - fs->fat_cache_start) * fs->bytes_per_sector + idx;
            *(uint32_t *)(fs->fat_cache + cache_off) = value;
            fs->fat_cache_dirty = 1;
            return 0;
        }
    }

    // fallback: direct read-modify-write through blkcache
    uint8_t buf[512];
    for (int copy = 0; copy < fs->table_count; copy++) {
        uint32_t lba = fs->fat_start_lba + copy * fs->fat_size + sector_in_fat;
        blkcache_read_sector(fs->drive, lba, buf);
        *(uint32_t *)(buf + idx) = value;
        blkcache_write_sector(fs->drive, lba, buf);
    }
    return 0;
}

static void fat32_flush_fat_cache(fat32_fs_info_t *fs) {
    if (!fs->fat_cache || !fs->fat_cache_dirty) return;

    for (uint8_t copy = 0; copy < fs->table_count; copy++) {
        uint32_t base = fs->fat_start_lba + copy * fs->fat_size + fs->fat_cache_start;
        for (uint32_t i = 0; i < fs->fat_cache_sectors; i++) {
            blkcache_write_sector(fs->drive, base + i,
                                  fs->fat_cache + i * fs->bytes_per_sector);
        }
    }
    fs->fat_cache_dirty = 0;
    ide_cache_flush(fs->drive);
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
    if (!buffer || cluster < 2 || cluster >= FAT32_CLUSTER_END) return;
    uint32_t first_sector = fs->cluster_heap_start_lba
                          + (cluster - 2) * fs->sectors_per_cluster;

    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        blkcache_write_sector(fs->drive,
                              first_sector + i,
                              buffer + (i * fs->bytes_per_sector));
    }
}

/**
 * @brief Mark a short-name entry and all its preceding LFN entries as deleted.
 *
 * Works within a single cluster. Caller provides the cluster buffer and the
 * index of the short-name entry within it.
 */
static void fat32_delete_lfn_chain(fat_dir_entry_t *ents, uint32_t sfn_index) {
    // Delete the short-name entry
    ents[sfn_index].name[0] = 0xE5;

    // Walk backwards and delete any preceding LFN entries
    for (int j = (int)sfn_index - 1; j >= 0; j--) {
        if ((ents[j].attr & 0x0F) == 0x0F && (uint8_t)ents[j].name[0] != 0xE5) {
            ents[j].name[0] = 0xE5;
            fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[j];
            if (lfn->order & 0x40) break; // was the last one
        } else {
            break;
        }
    }
}

static int fat32_unlink(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return -1;

    uint8_t key[11];
    fat32_build_name_key(name, key);

    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t *fs = pni->fs_info;
    uint32_t cluster = pni->cluster_number;

    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(cluster_size);

    // Track LFN state for matching by long name
    int lfn_valid = 0;
    uint8_t lfn_checksum = 0;
    char lfn_fragments[20][13];
    int lfn_frag_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00) {
                kernel_free(buf);
                return -1;
            }
            if (first == 0xE5) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            if ((ents[i].attr & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[i];
                if (lfn->order & 0x40) {
                    lfn_frag_count = 0;
                    lfn_checksum = lfn->checksum;
                    lfn_valid = 1;
                }
                if (lfn_valid && lfn->checksum == lfn_checksum) {
                    fat32_lfn_extract_chars(lfn, lfn_fragments[lfn_frag_count]);
                    lfn_frag_count++;
                } else {
                    lfn_valid = 0;
                    lfn_frag_count = 0;
                }
                continue;
            }

            if (ents[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            // Check match by 8.3 or LFN
            int matched = 0;

            // LFN match
            if (lfn_valid && lfn_frag_count > 0) {
                uint8_t ck = fat32_lfn_checksum((uint8_t *)ents[i].name);
                if (ck == lfn_checksum) {
                    char lfn_buf[256];
                    int pos = 0;
                    for (int f = lfn_frag_count - 1; f >= 0; f--)
                        for (int c = 0; c < 13 && pos < 255; c++) {
                            if (lfn_fragments[f][c] == '\0') goto unlink_lfn_done;
                            lfn_buf[pos++] = lfn_fragments[f][c];
                        }
                unlink_lfn_done:
                    lfn_buf[pos] = '\0';
                    if (fat32_strcasecmp(name, lfn_buf) == 0)
                        matched = 1;
                }
            }

            // 8.3 match
            if (!matched && memcmp(key, ents[i].name, 11) == 0)
                matched = 1;

            if (matched) {
                if (ents[i].attr & FAT32_ATTR_DIRECTORY) {
                    kernel_free(buf);
                    return -1;
                }
                fat32_delete_lfn_chain(ents, i);
                ents[i].file_size = 0;
                fat32_write_cluster(fs, cluster, buf);
                kernel_free(buf);
                return 0;
            }

            lfn_valid = 0;
            lfn_frag_count = 0;
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    kernel_free(buf);
    return -1;
}

static int fat32_dir_is_empty(fat32_fs_info_t *fs, uint32_t dir_cluster) {
    uint32_t cluster = dir_cluster;
    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(cluster_size);

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00) {
                kernel_free(buf);
                return 1;
            }
            if (first == 0xE5 ||
                (ents[i].attr & FAT32_ATTR_VOLUME_ID) ||
                ((ents[i].attr & 0x0F) == 0x0F) ||
                first == ' ') {
                continue;
            }
            if (first == '.' &&
                (ents[i].name[1] == ' ' || ents[i].name[1] == '.')) {
                continue;
            }
            kernel_free(buf);
            return 0;
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    kernel_free(buf);
    return 1;
}

static int fat32_rmdir(vfs_node_t *parent, const char *name) {
    if (!(parent->flags & VFS_FLAG_DIRECTORY)) return -1;

    uint8_t key[11];
    fat32_build_name_key(name, key);

    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t *fs = pni->fs_info;
    uint32_t cluster = pni->cluster_number;

    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint32_t entries_per_cluster = cluster_size / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(cluster_size);

    int lfn_valid = 0;
    uint8_t lfn_checksum = 0;
    char lfn_fragments[20][13];
    int lfn_frag_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00) {
                kernel_free(buf);
                return -1;
            }
            if (first == 0xE5) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            if ((ents[i].attr & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[i];
                if (lfn->order & 0x40) {
                    lfn_frag_count = 0;
                    lfn_checksum = lfn->checksum;
                    lfn_valid = 1;
                }
                if (lfn_valid && lfn->checksum == lfn_checksum) {
                    fat32_lfn_extract_chars(lfn, lfn_fragments[lfn_frag_count]);
                    lfn_frag_count++;
                } else {
                    lfn_valid = 0;
                    lfn_frag_count = 0;
                }
                continue;
            }

            if (ents[i].attr & FAT32_ATTR_VOLUME_ID) {
                lfn_valid = 0;
                lfn_frag_count = 0;
                continue;
            }

            int matched = 0;

            if (lfn_valid && lfn_frag_count > 0) {
                uint8_t ck = fat32_lfn_checksum((uint8_t *)ents[i].name);
                if (ck == lfn_checksum) {
                    char lfn_buf[256];
                    int pos = 0;
                    for (int f = lfn_frag_count - 1; f >= 0; f--)
                        for (int c = 0; c < 13 && pos < 255; c++) {
                            if (lfn_fragments[f][c] == '\0') goto rmdir_lfn_done;
                            lfn_buf[pos++] = lfn_fragments[f][c];
                        }
                rmdir_lfn_done:
                    lfn_buf[pos] = '\0';
                    if (fat32_strcasecmp(name, lfn_buf) == 0)
                        matched = 1;
                }
            }

            if (!matched && memcmp(key, ents[i].name, 11) == 0)
                matched = 1;

            if (matched) {
                if (!(ents[i].attr & FAT32_ATTR_DIRECTORY)) {
                    kernel_free(buf);
                    return -1;
                }

                uint32_t dir_cluster = (ents[i].first_cluster_high << 16)
                                     | ents[i].first_cluster_low;
                if (!fat32_dir_is_empty(fs, dir_cluster)) {
                    kernel_free(buf);
                    return -1;
                }

                fat32_delete_lfn_chain(ents, i);
                ents[i].file_size = 0;
                fat32_write_cluster(fs, cluster, buf);
                kernel_free(buf);
                return 0;
            }

            lfn_valid = 0;
            lfn_frag_count = 0;
        }

        cluster = fat32_read_fat_entry(fs, cluster);
    }

    kernel_free(buf);
    return -1;
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
    uint32_t per_cl  = csize / sizeof(fat_dir_entry_t);
    uint8_t *buf     = kernel_malloc(csize);

    int lfn_valid = 0;
    uint8_t lfn_checksum = 0;
    char lfn_fragments[20][13];
    int lfn_frag_count = 0;

    while (cluster < FAT32_CLUSTER_END) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t*)buf;

        for (uint32_t i = 0; i < per_cl; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00) { kernel_free(buf); return; }
            if (first == 0xE5) { lfn_valid = 0; lfn_frag_count = 0; continue; }

            if ((ents[i].attr & 0x0F) == 0x0F) {
                fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[i];
                if (lfn->order & 0x40) {
                    lfn_frag_count = 0;
                    lfn_checksum = lfn->checksum;
                    lfn_valid = 1;
                }
                if (lfn_valid && lfn->checksum == lfn_checksum) {
                    fat32_lfn_extract_chars(lfn, lfn_fragments[lfn_frag_count]);
                    lfn_frag_count++;
                } else { lfn_valid = 0; lfn_frag_count = 0; }
                continue;
            }

            int matched = 0;

            // Check LFN match
            if (lfn_valid && lfn_frag_count > 0) {
                uint8_t ck = fat32_lfn_checksum((uint8_t *)ents[i].name);
                if (ck == lfn_checksum) {
                    char lfn_buf[256];
                    int pos = 0;
                    for (int f = lfn_frag_count - 1; f >= 0; f--)
                        for (int c = 0; c < 13 && pos < 255; c++) {
                            if (lfn_fragments[f][c] == '\0') goto update_lfn_done;
                            lfn_buf[pos++] = lfn_fragments[f][c];
                        }
                update_lfn_done:
                    lfn_buf[pos] = '\0';
                    if (fat32_strcasecmp(name, lfn_buf) == 0)
                        matched = 1;
                }
            }

            if (!matched && memcmp(ents[i].name, key, 11) == 0)
                matched = 1;

            if (matched) {
                ents[i].file_size = new_size;
                fat32_write_cluster(fs, cluster, buf);
                kernel_free(buf);
                return;
            }

            lfn_valid = 0;
            lfn_frag_count = 0;
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
    if (!clusbuf) return -1;
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
    fat32_flush_fat_cache(fs);
    ide_cache_flush(fs->drive);
    fat32_update_dir_entry(ni, node->name, node->size);
    return written;
}

static int fat32_truncate(vfs_node_t *node, uint32_t size) {
    if (!(node->flags & VFS_FLAG_FILE)) return -1;

    fat32_node_info_t *ni = node->fs_data;
    if (!ni) return -1;

    node->size = size;
    ni->size = size;
    fat32_update_dir_entry(ni, node->name, size);
    return 0;
}

/**
 * @brief Find N contiguous free directory entry slots in a directory.
 *
 * Returns the cluster and index of the first slot. Extends the directory
 * chain if needed.
 *
 * @param fs           Filesystem info
 * @param dir_cluster  Starting cluster of the directory
 * @param n            Number of contiguous slots needed
 * @param out_cluster  Output: cluster containing the first slot
 * @param out_index    Output: index of the first slot within the cluster
 * @return 0 on success, -1 on failure
 */
static int fat32_find_free_slots(fat32_fs_info_t *fs, uint32_t dir_cluster,
                                 int n, uint32_t *out_cluster, uint32_t *out_index)
{
    uint32_t csize = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t entries_per_cl = csize / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(csize);

    // We need to find N contiguous free entries.
    // Simple approach: scan linearly, track run of free entries.
    int run = 0;
    uint32_t run_start_cluster = dir_cluster;
    uint32_t run_start_index = 0;

    uint32_t cluster = dir_cluster;
    while (1) {
        fat32_read_cluster(fs, cluster, buf);
        fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

        for (uint32_t i = 0; i < entries_per_cl; i++) {
            uint8_t first = (uint8_t)ents[i].name[0];
            if (first == 0x00 || first == 0xE5) {
                if (run == 0) {
                    run_start_cluster = cluster;
                    run_start_index = i;
                }
                run++;
                if (run >= n) {
                    kernel_free(buf);
                    *out_cluster = run_start_cluster;
                    *out_index = run_start_index;
                    return 0;
                }
            } else {
                run = 0;
            }
        }

        uint32_t next = fat32_read_fat_entry(fs, cluster);
        if (next >= FAT32_CLUSTER_END) {
            // Extend directory
            next = fat32_allocate_cluster(fs);
            if (!next) { kernel_free(buf); return -1; }
            fat32_write_fat_entry(fs, cluster, next);
            memset(buf, 0, csize);
            fat32_write_cluster(fs, next, buf);
        }
        cluster = next;
    }
}

/**
 * @brief Write LFN entries + short-name entry into a directory.
 *
 * @param fs           Filesystem info
 * @param dir_cluster  Starting cluster of the directory
 * @param slot_cluster Cluster containing the first free slot
 * @param slot_index   Index of the first free slot
 * @param name         Long file name
 * @param key          11-byte short name
 * @param attr         Attribute byte for the short-name entry
 * @param data_cluster First cluster of the file/directory data
 * @param file_size    File size (0 for directories)
 * @return Cluster where the short-name entry was written
 */
static uint32_t fat32_write_dir_entries(fat32_fs_info_t *fs,
                                        uint32_t slot_cluster, uint32_t slot_index,
                                        const char *name, const uint8_t key[11],
                                        uint8_t attr, uint32_t data_cluster,
                                        uint32_t file_size)
{
    int namelen = strlen(name);
    int need_lfn = !fat32_name_is_8dot3(name);
    int lfn_count = need_lfn ? ((namelen + 12) / 13) : 0;

    uint32_t csize = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t entries_per_cl = csize / sizeof(fat_dir_entry_t);
    uint8_t *buf = kernel_malloc(csize);

    uint8_t checksum = fat32_lfn_checksum(key);

    // Write entries sequentially starting at slot_cluster:slot_index
    uint32_t cluster = slot_cluster;
    uint32_t idx = slot_index;
    // We might need to re-read the cluster
    fat32_read_cluster(fs, cluster, buf);
    fat_dir_entry_t *ents = (fat_dir_entry_t *)buf;

    for (int e = 0; e < lfn_count + 1; e++) {
        if (idx >= entries_per_cl) {
            // Write current cluster and move to next
            fat32_write_cluster(fs, cluster, buf);
            cluster = fat32_read_fat_entry(fs, cluster);
            fat32_read_cluster(fs, cluster, buf);
            ents = (fat_dir_entry_t *)buf;
            idx = 0;
        }

        if (e < lfn_count) {
            // Write LFN entry
            // LFN entries are stored in reverse order: last fragment first
            int seq = lfn_count - e; // sequence number (1-based)
            int char_offset = (seq - 1) * 13;

            fat_lfn_entry_t *lfn = (fat_lfn_entry_t *)&ents[idx];
            memset(lfn, 0, sizeof(*lfn));
            lfn->order = seq;
            if (e == 0) lfn->order |= 0x40; // mark as last LFN entry
            lfn->attr = FAT32_ATTR_LFN;
            lfn->type = 0;
            lfn->checksum = checksum;
            lfn->first_cluster = 0;
            fat32_lfn_fill_entry(lfn, name, char_offset, namelen);
        } else {
            // Write short-name entry
            memset(&ents[idx], 0, sizeof(fat_dir_entry_t));
            memcpy(ents[idx].name, key, 11);
            ents[idx].attr = attr;
            ents[idx].first_cluster_high = (data_cluster >> 16) & 0xFFFF;
            ents[idx].first_cluster_low = data_cluster & 0xFFFF;
            ents[idx].file_size = file_size;
        }
        idx++;
    }

    // Write final cluster
    fat32_write_cluster(fs, cluster, buf);
    kernel_free(buf);
    return cluster; // cluster where the SFN entry lives
}

static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name) {
    fat32_node_info_t *pni = parent->fs_data;
    fat32_fs_info_t   *fs  = pni->fs_info;
    uint32_t dir_cluster = pni->cluster_number;

    // Build the 8.3 short name
    uint8_t key[11];
    int need_lfn = !fat32_name_is_8dot3(name);
    if (need_lfn) {
        fat32_generate_short_name(name, key, fs, dir_cluster);
    } else {
        fat32_build_name_key(name, key);
    }

    // Number of entries needed: LFN entries + 1 short entry
    int namelen = strlen(name);
    int lfn_count = need_lfn ? ((namelen + 12) / 13) : 0;
    int total_entries = lfn_count + 1;

    // Find contiguous free slots
    uint32_t slot_cluster, slot_index;
    if (fat32_find_free_slots(fs, dir_cluster, total_entries, &slot_cluster, &slot_index) < 0)
        return NULL;

    // Allocate a data cluster for the new file
    uint32_t newcl = fat32_allocate_cluster(fs);
    if (!newcl) return NULL;

    // Write directory entries
    uint32_t sfn_cluster = fat32_write_dir_entries(fs, slot_cluster, slot_index,
                                                    name, key, FAT32_ATTR_ARCHIVE, newcl, 0);

    // Create the VFS node
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child, 0, sizeof(*child));
    strncpy(child->name, name, sizeof(child->name));
    child->name[sizeof(child->name) - 1] = '\0';
    child->flags = VFS_FLAG_FILE | VFS_FLAG_DISKIO;
    child->refcount = 1;
    child->ops  = &fat32_ops;
    child->uid  = 0;
    child->gid  = 0;
    child->mode = S_IFREG | 0644;
    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = newcl;
    cni->parent_cluster = sfn_cluster;
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

    memset(ents[0].name, ' ', 11);
    ents[0].name[0] = '.';
    ents[0].attr   = FAT32_ATTR_DIRECTORY;
    ents[0].first_cluster_high = (newcl >> 16) & 0xFFFF;
    ents[0].first_cluster_low  = newcl & 0xFFFF;

    memset(ents[1].name, ' ', 11);
    ents[1].name[0] = '.';  ents[1].name[1] = '.';
    ents[1].attr   = FAT32_ATTR_DIRECTORY;
    ents[1].first_cluster_high = (parent_cl >> 16) & 0xFFFF;
    ents[1].first_cluster_low  = parent_cl & 0xFFFF;

    fat32_write_cluster(fs, newcl, buf);
    kernel_free(buf);

    // 3: Build short name (with LFN generation if needed)
    uint8_t key[11];
    int need_lfn = !fat32_name_is_8dot3(name);
    if (need_lfn) {
        fat32_generate_short_name(name, key, fs, parent_cl);
    } else {
        fat32_build_name_key(name, key);
    }

    // 4: Find contiguous free slots
    int namelen = strlen(name);
    int lfn_count = need_lfn ? ((namelen + 12) / 13) : 0;
    int total_entries = lfn_count + 1;

    uint32_t slot_cluster, slot_index;
    if (fat32_find_free_slots(fs, parent_cl, total_entries, &slot_cluster, &slot_index) < 0)
        return NULL;

    // 5: Write LFN + short-name entries
    fat32_write_dir_entries(fs, slot_cluster, slot_index,
                            name, key, FAT32_ATTR_DIRECTORY, newcl, 0);

    // 6: Build VFS node
    vfs_node_t *child = kernel_malloc(sizeof(*child));
    memset(child, 0, sizeof(*child));
    strncpy(child->name, name, sizeof(child->name));
    child->name[sizeof(child->name) - 1] = '\0';
    child->flags    = VFS_FLAG_DIRECTORY | VFS_FLAG_DISKIO;
    child->refcount = 1;
    child->ops      = &fat32_ops;
    child->uid      = 0;
    child->gid      = 0;
    child->mode     = S_IFDIR | 0755;

    fat32_node_info_t *cni = kernel_malloc(sizeof(*cni));
    cni->fs_info        = fs;
    cni->cluster_number = newcl;
    cni->parent_cluster = parent_cl;
    child->fs_data      = cni;

    return child;
}

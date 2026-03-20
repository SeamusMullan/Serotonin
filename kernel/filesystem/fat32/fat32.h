#ifndef _FS_FAT32
#define _FS_FAT32

#include <stdint.h>
#include "../vfs.h"
#include "../../stdio/stdio.h"

typedef struct fat_extBS_32
{
	//extended fat32 stuff
	unsigned int		table_size_32;
	unsigned short		extended_flags;
	unsigned short		fat_version;
	unsigned int		root_cluster;
	unsigned short		fat_info;
	unsigned short		backup_BS_sector;
	unsigned char 		reserved_0[12];
	unsigned char		drive_number;
	unsigned char 		reserved_1;
	unsigned char		boot_signature;
	unsigned int 		volume_id;
	unsigned char		volume_label[11];
	unsigned char		fat_type_label[8];

}__attribute__((packed)) fat_extBS_32_t;

typedef struct fat_BS
{
	unsigned char 		bootjmp[3];
	unsigned char 		oem_name[8];
	unsigned short 	        bytes_per_sector;
	unsigned char		sectors_per_cluster;
	unsigned short		reserved_sector_count;
	unsigned char		table_count;
	unsigned short		root_entry_count;
	unsigned short		total_sectors_16;
	unsigned char		media_type;
	unsigned short		table_size_16;
	unsigned short		sectors_per_track;
	unsigned short		head_side_count;
	unsigned int 		hidden_sector_count;
	unsigned int 		total_sectors_32;
	
	//this will be cast to it's specific type once the driver actually knows what type of FAT this is.
	unsigned char		extended_section[54];
	
}__attribute__((packed)) fat_BS_t;

typedef struct fat32_fs_info {
    uint8_t  drive;
    uint32_t partition_start_lba;

    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  table_count;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t fat_start_lba;
    uint32_t cluster_heap_start_lba;
} fat32_fs_info_t;

typedef struct fat32_node_info {
    fat32_fs_info_t *fs_info;
    uint32_t cluster_number;
    uint32_t size;
    uint32_t parent_cluster;
} fat32_node_info_t;

typedef struct fat_dir_entry {
    char name[11];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dir_entry_t;

typedef struct fat_lfn_entry {
    uint8_t  order;        // Sequence number (OR'd with 0x40 for last)
    uint16_t name1[5];     // Characters 1-5 (UCS-2)
    uint8_t  attr;         // Always 0x0F
    uint8_t  type;         // Always 0x00 for LFN
    uint8_t  checksum;     // Checksum of short name
    uint16_t name2[6];     // Characters 6-11 (UCS-2)
    uint16_t first_cluster; // Always 0x0000
    uint16_t name3[2];     // Characters 12-13 (UCS-2)
} __attribute__((packed)) fat_lfn_entry_t;

// FAT32 end-of-chain marker
#define FAT32_CLUSTER_END     0x0FFFFFF8

// Directory entry attribute bits
#define FAT32_ATTR_READ_ONLY  0x01   // Read-only entry
#define FAT32_ATTR_HIDDEN     0x02   // Hidden entry
#define FAT32_ATTR_SYSTEM     0x04   // System entry
#define FAT32_ATTR_VOLUME_ID  0x08   // Volume label entry
#define FAT32_ATTR_DIRECTORY  0x10   // Directory entry
#define FAT32_ATTR_ARCHIVE    0x20   // Archive entry
#define FAT32_ATTR_LFN        0x0F   // Long file name entry

// Maximum LFN length (255 UCS-2 characters)
#define FAT32_LFN_MAX         255


void fat32_init(void);
static void fat32_parse_bpb(fat32_fs_info_t *info, uint8_t drive, uint32_t partition_start_lba, uint8_t *boot_sector);
static void fat32_read_cluster(fat32_fs_info_t *fs_info, uint32_t cluster, uint8_t *buffer);
static uint32_t fat32_read_fat_entry(fat32_fs_info_t *fs_info, uint32_t cluster);
extern vfs_node_t *fat32_mount(const char *device);
extern vfs_node_t *fat32_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name);
static int fat32_read(vfs_node_t *node,uint32_t offset,uint32_t size,char *buffer);
static int fat32_open(vfs_node_t *node);
static int fat32_close(vfs_node_t *node);
static int fat32_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
static int fat32_truncate(vfs_node_t *node, uint32_t size);
static int fat32_unlink(vfs_node_t *parent, const char *name);
static int fat32_rmdir(vfs_node_t *parent, const char *name);
static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name);
static vfs_node_t *fat32_mkdir(vfs_node_t *parent, const char *name);

static vfs_ops_t fat32_ops = {
    .read    = fat32_read,
    .write   = fat32_write,
    .truncate = fat32_truncate,
    .unlink = fat32_unlink,
    .rmdir = fat32_rmdir,
    .open    = fat32_open,
    .close   = fat32_close,
    .readdir = fat32_readdir,
    .finddir = fat32_finddir,
    .create  = fat32_create,
    .mkdir   = fat32_mkdir
};


extern filesystem_t fat32_fs;


#endif

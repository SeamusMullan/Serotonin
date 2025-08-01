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

// FAT32 end-of-chain marker
#define FAT32_CLUSTER_END     0x0FFFFFF8

// Directory entry attribute bits
#define FAT32_ATTR_VOLUME_ID  0x08   // Volume label entry
#define FAT32_ATTR_DIRECTORY  0x10   // Directory entry


void fat32_init(void);
static void fat32_parse_bpb(fat32_fs_info_t *info, uint8_t drive, uint32_t partition_start_lba, uint8_t *boot_sector);
extern vfs_node_t *fat32_mount(const char *device);
extern vfs_node_t *fat32_readdir(vfs_node_t *node, uint32_t index);
static vfs_node_t *fat32_finddir(vfs_node_t *node, const char *name);
static int fat32_read(vfs_node_t *node,uint32_t offset,uint32_t size,char *buffer);
static int fat32_open(vfs_node_t *node);
static int fat32_close(vfs_node_t *node);
static int fat32_write(vfs_node_t *node, uint32_t offset, uint32_t size, const char *buffer);
static vfs_node_t *fat32_create(vfs_node_t *parent, const char *name);
static vfs_node_t *fat32_mkdir(vfs_node_t *parent, const char *name);

static vfs_ops_t fat32_ops = {
    .read    = fat32_read,
    .write   = fat32_write,
    .open    = fat32_open,
    .close   = fat32_close,
    .readdir = fat32_readdir,
    .finddir = fat32_finddir,
    .create  = fat32_create,
    .mkdir   = fat32_mkdir
};


extern filesystem_t fat32_fs;


#endif
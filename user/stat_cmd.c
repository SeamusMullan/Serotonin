/**
 * @file stat_cmd.c
 * @brief Display file status for Serotonin OS
 *
 * Prints detailed file information including size, permissions,
 * timestamps, and ownership.
 */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000
#define S_IFCHR  0x2000
#define S_IFBLK  0x6000
#define S_IFIFO  0x1000
#define S_IFLNK  0xA000
#define S_IFSOCK 0xC000

static const char *file_type_str(unsigned int mode) {
    switch (mode & S_IFMT) {
        case S_IFREG:  return "regular file";
        case S_IFDIR:  return "directory";
        case S_IFCHR:  return "character device";
        case S_IFBLK:  return "block device";
        case S_IFIFO:  return "fifo";
        case S_IFLNK:  return "symbolic link";
        case S_IFSOCK: return "socket";
        default:       return "unknown";
    }
}

static void format_mode(unsigned int mode, char *out) {
    switch (mode & S_IFMT) {
        case S_IFDIR:  out[0] = 'd'; break;
        case S_IFLNK:  out[0] = 'l'; break;
        case S_IFCHR:  out[0] = 'c'; break;
        case S_IFBLK:  out[0] = 'b'; break;
        case S_IFIFO:  out[0] = 'p'; break;
        case S_IFSOCK: out[0] = 's'; break;
        default:       out[0] = '-'; break;
    }
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 040)  ? 'r' : '-';
    out[5] = (mode & 020)  ? 'w' : '-';
    out[6] = (mode & 010)  ? 'x' : '-';
    out[7] = (mode & 04)   ? 'r' : '-';
    out[8] = (mode & 02)   ? 'w' : '-';
    out[9] = (mode & 01)   ? 'x' : '-';
    out[10] = '\0';
}

static void epoch_to_date(long epoch, int *year, int *month, int *day,
                          int *hour, int *min, int *sec) {
    long s = epoch;
    *sec  = (int)(s % 60); s /= 60;
    *min  = (int)(s % 60); s /= 60;
    *hour = (int)(s % 24); s /= 24;

    /* Days since 1970-01-01 */
    long days = s;
    int y = 1970;

    for (;;) {
        int yd = 365;
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) yd = 366;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    *year = y;

    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
    int mdays[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 0;
    while (m < 11 && days >= mdays[m]) {
        days -= mdays[m];
        m++;
    }
    *month = m + 1;
    *day = (int)days + 1;
}

static void print_time(long epoch) {
    int y, m, d, h, mi, s;
    epoch_to_date(epoch, &y, &m, &d, &h, &mi, &s);
    printf("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, h, mi, s);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: stat FILE...\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            printf("stat: %s: cannot stat (errno=%d)\n", argv[i], errno);
            status = 1;
            continue;
        }

        char mode_str[11];
        format_mode((unsigned int)st.st_mode, mode_str);

        printf("  File: %s\n", argv[i]);
        printf("  Size: %-15ld Blocks: %-10ld IO Block: %-6ld %s\n",
               (long)st.st_size, (long)st.st_blocks,
               (long)st.st_blksize, file_type_str((unsigned int)st.st_mode));
        printf("Device: %-15ld Inode: %-10ld Links: %ld\n",
               (long)st.st_dev, (long)st.st_ino, (long)st.st_nlink);
        printf("Access: (%04o/%s)  Uid: %ld  Gid: %ld\n",
               (unsigned int)(st.st_mode & 07777), mode_str,
               (long)st.st_uid, (long)st.st_gid);
        printf("Access: "); print_time((long)st.st_atim.tv_sec); printf("\n");
        printf("Modify: "); print_time((long)st.st_mtim.tv_sec); printf("\n");
        printf("Change: "); print_time((long)st.st_ctim.tv_sec); printf("\n");

        if (i < argc - 1) printf("\n");
    }

    return status;
}

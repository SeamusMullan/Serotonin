/**
 * @file ls.c
 * @brief Directory listing utility for Serotonin OS
 *
 * Lists directory contents with optional long format (-l) and
 * hidden file display (-a).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int listdir(const char *path, char *buf, size_t size);

/* ── Helpers ─────────────────────────────────────────────────────── */

static void format_mode(mode_t m, char *out)
{
	/* file type */
	switch (m & S_IFMT) {
	case S_IFDIR:  out[0] = 'd'; break;
	case S_IFCHR:  out[0] = 'c'; break;
	case S_IFBLK:  out[0] = 'b'; break;
	case S_IFIFO:  out[0] = 'p'; break;
	case S_IFLNK:  out[0] = 'l'; break;
	case S_IFSOCK: out[0] = 's'; break;
	default:       out[0] = '-'; break;
	}
	out[1] = (m & S_IRUSR) ? 'r' : '-';
	out[2] = (m & S_IWUSR) ? 'w' : '-';
	out[3] = (m & S_IXUSR) ? 'x' : '-';
	out[4] = (m & S_IRGRP) ? 'r' : '-';
	out[5] = (m & S_IWGRP) ? 'w' : '-';
	out[6] = (m & S_IXGRP) ? 'x' : '-';
	out[7] = (m & S_IROTH) ? 'r' : '-';
	out[8] = (m & S_IWOTH) ? 'w' : '-';
	out[9] = (m & S_IXOTH) ? 'x' : '-';
	out[10] = '\0';
}

static void epoch_to_date(long epoch, char *out, size_t outsize)
{
	if (epoch <= 0) {
		strncpy(out, "---", outsize);
		return;
	}

	long s = epoch;
	int sec = s % 60; s /= 60;
	int min = s % 60; s /= 60;
	int hour = s % 24; s /= 24;

	/* days since 1970-01-01 */
	long days = s;
	int year = 1970;
	for (;;) {
		int ydays = 365;
		if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
			ydays = 366;
		if (days < ydays) break;
		days -= ydays;
		year++;
	}

	int leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
	int mdays[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int month = 0;
	while (month < 11 && days >= mdays[month]) {
		days -= mdays[month];
		month++;
	}

	int day = (int)days + 1;
	month += 1;

	static const char *mon_names[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	snprintf(out, outsize, "%s %2d %02d:%02d",
		mon_names[month - 1], day, hour, min);
	(void)sec;
}

/* ── Entry storage for sorting ───────────────────────────────────── */

#define MAX_ENTRIES 512
#define NAME_MAX_LEN 256

typedef struct {
	char name[NAME_MAX_LEN];
	struct stat st;
	int stat_ok;
} entry_t;

static entry_t entries[MAX_ENTRIES];

static int cmp_entries(const void *a, const void *b)
{
	const entry_t *ea = (const entry_t *)a;
	const entry_t *eb = (const entry_t *)b;
	return strcmp(ea->name, eb->name);
}

static int list_dir(const char *path, int show_all, int long_fmt, int print_header)
{
	char buf[8192];

	errno = 0;
	int ret = listdir(path, buf, sizeof(buf));
	if (ret < 0 || errno != 0) {
		printf("ls: cannot access '%s' (errno=%d)\n", path, errno);
		return 1;
	}

	if (print_header)
		printf("%s:\n", path);

	/* parse newline-separated entries */
	int nentries = 0;
	char *p = buf;
	char *end = buf + ret;

	while (p < end && nentries < MAX_ENTRIES) {
		char *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);

		if (len > 0 && len < NAME_MAX_LEN) {
			/* skip hidden files unless -a */
			if (!show_all && p[0] == '.') {
				p = nl ? nl + 1 : end;
				continue;
			}

			memcpy(entries[nentries].name, p, len);
			entries[nentries].name[len] = '\0';

			if (long_fmt) {
				char fullpath[512];
				if (strcmp(path, "/") == 0)
					snprintf(fullpath, sizeof(fullpath), "/%s", entries[nentries].name);
				else
					snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entries[nentries].name);
				entries[nentries].stat_ok = (stat(fullpath, &entries[nentries].st) == 0);
			}
			nentries++;
		}
		p = nl ? nl + 1 : end;
	}

	/* sort alphabetically */
	if (nentries > 0)
		qsort(entries, nentries, sizeof(entry_t), cmp_entries);

	/* print */
	for (int i = 0; i < nentries; i++) {
		if (long_fmt) {
			if (entries[i].stat_ok) {
				char mode_str[12];
				char date_str[32];
				format_mode(entries[i].st.st_mode, mode_str);
				epoch_to_date(entries[i].st.st_mtim.tv_sec, date_str, sizeof(date_str));

				printf("%s %2u %4u %4u %8lu %s %s\n",
					mode_str,
					(unsigned)entries[i].st.st_nlink,
					(unsigned)entries[i].st.st_uid,
					(unsigned)entries[i].st.st_gid,
					(unsigned long)entries[i].st.st_size,
					date_str,
					entries[i].name);
			} else {
				printf("?????????? ? ? ? ? ? %s\n", entries[i].name);
			}
		} else {
			printf("%s  ", entries[i].name);
		}
	}

	if (!long_fmt && nentries > 0)
		printf("\n");

	return 0;
}

int main(int argc, char **argv)
{
	int show_all = 0;
	int long_fmt = 0;
	int status = 0;

	/* collect flags and paths */
	const char *paths[64];
	int npaths = 0;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] != '\0') {
			for (const char *f = argv[i] + 1; *f; f++) {
				switch (*f) {
				case 'a': show_all = 1; break;
				case 'l': long_fmt = 1; break;
				case '1': break; /* already default for long */
				default:
					printf("ls: unknown option '-%c'\n", *f);
					return 1;
				}
			}
		} else {
			if (npaths < 64)
				paths[npaths++] = argv[i];
		}
	}

	if (npaths == 0) {
		paths[0] = ".";
		npaths = 1;
	}

	int print_header = (npaths > 1);
	for (int i = 0; i < npaths; i++) {
		if (i > 0 && print_header) printf("\n");
		status |= list_dir(paths[i], show_all, long_fmt, print_header);
	}

	return status;
}

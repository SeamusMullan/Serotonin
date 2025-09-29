#ifndef _SYS_TIMESPEC
#define _SYS_TIMESPEC

#include "types.h"

struct timespec {
    time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* and nanoseconds */
};

#endif


/** \file timefmt.h
 *  \brief Header: time formating macroses
 */

#ifndef __UTIL_TIMEFMT_H
#define __UTIL_TIMEFMT_H

#include <sys/time.h>
#include <sys/types.h>

#define INVALID_TIME_TEXT	"(invalid)"

/* safe localtime formatting - strftime()-using version */
#define FMT_LOCALTIME(buffer, bufsize, fmt, when)			\
    {									\
	struct tm *whentm;						\
	whentm = localtime(&when);					\
	if (whentm == NULL)						\
	{								\
	    strncpy(buffer, INVALID_TIME_TEXT, bufsize);		\
	    buffer[bufsize-1] = 0;					\
	}								\
	else								\
	{								\
	    strftime(buffer, bufsize, fmt, whentm);			\
	}								\
    }									\

#define FMT_LOCALTIME_CURRENT(buffer, bufsize, fmt)		\
    {								\
	time_t __current_time;					\
	time(&__current_time);					\
	FMT_LOCALTIME(buffer,bufsize,fmt,__current_time);	\
    }

#endif				/* !__UTIL_H */

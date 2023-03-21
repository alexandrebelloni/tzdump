/*
** tzdump - display time zone info in TZ format
**
** Example:
**	tzdump Australia/ACT
**
** This program was almost named unzic.  However, since zic's input isn't
** fully recoverable from its output, that name would be a bit misleading.
*/

#if !defined(lint) && !defined(__lint)
static const char rcsid[] =
	"@(#)$Id$";
static const char rcsname[] =
	"@(#)$Name$";

/*
** Bugs:
**	o There should be a method for searching for timezone name and
**	  abbreviation aliases.
*/

# define RCSID rcsid
# define RCSNAME rcsname
#else
# define RCSID NULL
# define RCSNAME NULL
#endif

#ifdef ENABLETRACE
# define TRACE(args)	(void)printf args
#else
# define TRACE(args)
#endif

/* Ought to have an autoconf configure script */
#define HAVE_STRNCPY
#define HAVE_STRNCAT
#undef HAVE_SNPRINTF

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <ctype.h>
#include "tzfile.h"

#ifndef TZNAME_MAX
#endif
#ifndef TZDIR
# define TZDIR "/usr/share/lib/zoneinfo"
#endif

#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif

#ifndef SECSPERMIN
# define SECSPERMIN 60
#endif
#ifndef MINSPERHOUR
# define MINSPERHOUR 60
#endif
#ifndef SECSPERHOUR
# define SECSPERHOUR (SECSPERMIN * MINSPERHOUR)
#endif


#define TIMEBUFSIZE 16
#define ZIBUFSIZE 4096

#if 0
/* From Solaris 2.5 and 2.6 */
#define TZ_MAX_TIMES    370
#define TZ_MAX_TYPES    256 /* Limited by what (unsigned char)'s can hold */
#define TZ_MAX_TYPES    20      /* Maximum number of local time types */
#define TZ_MAX_CHARS    50      /* Maximum number of abbreviation characters */
#define TZ_MAX_LEAPS    50      /* Maximum number of leap second corrections */
#endif /* 0 */

#if 0
	/* Compute size required for ZoneInfoBufferSize */
		sizeof (struct tzhead)
			+ TZ_MAX_TIMES * (4 + 1) * sizeof (char)
			+ TZ_MAX_TYPES * (4 + 2) * sizeof (char)
			+ TZ_MAX_CHARS * sizeof (char)
			+ TZ_MAX_LEAPS * (4 + 4) * sizeof (char)
			+ TZ_MAX_TYPES * sizeof (char)
# ifdef STRUCT_TZHEAD_TTISGMTCNT
			+ TZ_MAX_TYPES * sizeof (char)
# endif

#endif /* 0 */


char	*usage = "[timezone ...]";
char	*help = "";

char	*progname, *filename;

char	*zoneinfopath = NULL;
int	quietflag = 0;
int	verboseflag = 0;
char *comment = "";

/*
**  TZ variable can either be:
**	:<characters>
**	("zoneinfo timezone")
**  or
**	<std><offset1>[<dst>[<offset2>]][,<start>[/<time>],<end>[/<time>]
**	("POSIX timezone" -> see elsie.nci.nih.gov for public domain info)
**
**  Solaris extends this in that a timezone without a leading colon that
**  doesn't parse as a POSIX timezone is treated as a zoneinfo timezone.
**  A zoneinfo timezone refers to a data file that contains a set of rules
**  for calculating time offsets from UTC:
**	/usr/share/lib/zoneinfo/$TZ
**
**	TZDIR -> /usr/share/lib/zoneinfo; defined in <tzfile.h>
**	See lib/libc/port/gen/time_comm.c:_tzload()
**
**	std/dst: timezone abbreviations (e.g. EST/EDT)
**	offset1/offset2: offset from GMT ([+-]hour:min:sec)
**	start/end: Mm.w.d
**		m - month (1-12)
**		w - week (1-5)
**			1 - week in which the d'th day first falls
**			5 - week in which the d'th day last falls (4 or 5)
**		d - day (0-6, 0=Sunday)
**		NB: Two other formats are defined, but not used here.
**	time: HH[:MM[:SS]]
**
*/

/* XXX Unsafe replacements */
#ifndef HAVE_STRNCPY
# define strncpy(dest, src, len)	strcpy(dest, src)
#endif

#ifndef HAVE_STRNCAT
# define strncat(dest, src, len)	strcat(dest, src)
#endif

#if 0
#ifdef HAVE_SNPRINTF
	(void)snprintf(p, len, fmt, ...);
#else
	(void)sprintf(p, fmt, ...);
#endif
#endif /* 0 */

/*
**	bcopy(a,b,c)	memcpy(b,a,c)
**	bzero(a,b)	memset(a,0,b)
**	index(a,b)	strchr(a,b)
**	rindex(a,b)	strrchr(a,b)
*/

/* XXX Replacements for memset() and memcpy()??? */
#if 0
#ifndef HAVE_MEMSET
  /* XXX Assumes that val=0 */
# define memset(dest, val, len)		bzero(dest, len)
#endif

#ifndef HAVE_MEMCPY
# define memcpy(dest, src, len)		bcopy(src, dest, len)
#endif
#endif /* 0 */


/*
** Determine n'th week holding wday.
*/
int
weekofmonth(int mday, int wday)
{
	int	tmp;

	TRACE(("weekofmonth(mday = %d, wday = %d)\n", mday, wday));

	tmp = 1 + --mday/7;
	/* Assume that the last week of the month is desired. */
	if ( tmp == 4 )
		tmp++;

	return tmp;
}


/*
** Convert seconds to hour:min:sec format.
*/
char *
timefmt(char *p, int len, int interval)
{
	int		hours=0, mins=0, secs=0;
	char		*fmt, *sign;
	static char	fmtbuf[TIMEBUFSIZE];

	TRACE(("timefmt(p = %p, len = %d, interval = %d)\n", p, len, interval));

	if ( p == NULL ) {
		p = &fmtbuf[0];
		len = sizeof fmtbuf;
	}

	/* XXX Verify for negative values of interval. */
	sign = "";
	if ( interval < 0 ) {
		sign = "-";
		interval = -interval;
	}

	secs = interval % SECSPERMIN;
	interval -= secs;
	interval /= SECSPERMIN;
	mins = interval % MINSPERHOUR;
	interval -= mins;
	hours = interval / MINSPERHOUR;

	fmt = ( (secs != 0)
			? "%s%d:%d:%d"
			: ( (mins != 0)
					? "%s%d:%d"
					: "%s%d" ) );

#ifdef HAVE_SNPRINTF
	(void)snprintf(p, len, fmt, sign, hours, mins, secs);
#else
	(void)sprintf(p, fmt, sign, hours, mins, secs);
#endif

	return p;
}


/*
**
*/
char *
ctimeGMT(time_t time)
{
	static char	buf[2*TIMEBUFSIZE];
	struct tm	*tmptr;

	TRACE(("ctimeGMT(time = %d)\n", time));

	tmptr = gmtime(&time);
	(void)strftime(buf, sizeof buf, "%a %b %d %T GMT %Y", tmptr);

	return &buf[0];
}


/*
** Generate an int from four chars.
*/
int
tzhdecode(char *p)
{
	int	tmp, i;

	TRACE(("tzhdecode(p = %p [%d])\n", p, (p) ? p : "(null)"));

	tmp = (*p++ & 0xff);
	for ( i = 1 ; i <= 3 ; i++ )
		tmp = (tmp << 8) | (*p++ & 0xff);
	return tmp;
}

char *
wrapabbrev(char *abbrev)
{
    static char wrapbuf[TZ_MAX_CHARS+2];
    int i;

    for(i = 0; abbrev[i] != '\0'; ++i)
    {
        if(isdigit(abbrev[i]) || (abbrev[i] == '+') || (abbrev[i] == '-'))
        {
            snprintf(wrapbuf, sizeof(wrapbuf), "<%s>", abbrev);
            wrapbuf[sizeof(wrapbuf) - 1] = '\0';
            return wrapbuf;
        }
    }
    return abbrev;
}

/*
** Read zoneinfo data file and generate expanded TZ value.
*/
int
dumptzdata(char *tzval)
{
	char		stdoffset[TIMEBUFSIZE], dstoffset[TIMEBUFSIZE];
	char		startdate[TIMEBUFSIZE], enddate[TIMEBUFSIZE];
	char		starttime[TIMEBUFSIZE], endtime[TIMEBUFSIZE];
	char		datafile[MAXPATHLEN];
	char		zibuf[ZIBUFSIZE];
	int		fd;
	struct tzhead	*tzhp;
	int		ttisstdcnt, leapcnt, timecnt, typecnt, charcnt;
#ifdef STRUCT_TZHEAD_TTISGMTCNT
	int		ttisgmtcnt;
#endif
	int		tzsize;
	struct {
		long	time;
		long	type;
	} transit[TZ_MAX_TIMES];
	struct {
		int	gmtoffset;	/* gmtoffs */
		char	isdst;		/* isdsts */
		char	abbrind;	/* abbrinds, unsigned char?? */
		int	stds;		/* char? */
#ifdef STRUCT_TZHEAD_TTISGMTCNT
		int	gmts;		/* (unsigned) char? */
#endif
	} lti[TZ_MAX_TYPES];
	struct {
		time_t	transit;		/* trans */
		long	correct;		/* corr */
		char	roll; 			/* ??? */
	} leaps[TZ_MAX_LEAPS];
	struct {
		time_t		time;
		int		index;
		unsigned char	type;
		struct tm	*tm;
	} tt[2] = {
		{ 0, 0, 0, NULL },
		{ 0, 0, 0, NULL }
	};
	char	chars[TZ_MAX_CHARS];
	time_t		now;
	int		startindex, endindex;
	struct tm	*starttm, *endtm;
	struct tm	tmbuf[2];
	int		i;
	char		*p;
	int		retcode;

	TRACE(("dumptzdata(tzval = %p [\"%s\"])\n",
			tzval, (tzval) ? tzval : "(null)"));

	/* initialize */
	memset(transit, 0, sizeof transit);
	memset(lti, 0, sizeof lti);
	memset(leaps, 0, sizeof leaps);

	now = time(NULL);

	/* If no timezone is specified, use the TZ environment variable. */
	if ( tzval == NULL ) {
		if ( (tzval = getenv("TZ")) == NULL ) {
			/* Nothing with which to work. */
			return -1;
		}
	}

	/*
	** Construct pathname of the zoneinfo data file.
	*/
	if ( *tzval == ':' ) {
		tzval++;
	}
	if ( *tzval == '/' ) {
		(void)fprintf(stderr, "timezone starts with '/': %s\n", tzval);
		return -1;
	}
	if ( zoneinfopath == NULL ) {
		(void)strncpy(datafile, TZDIR, sizeof datafile);
	}
	else {
		(void)strncpy(datafile, zoneinfopath, sizeof datafile);
	}

	(void)strncat(datafile, "/", sizeof datafile - 1);
	(void)strncat(datafile, tzval, sizeof datafile - strlen(tzval));

	/*
	** Open and read the zoneinfo data file.
	** XXX Ought to have better error reporting, i.e. errno.
	*/
	if ( access(datafile, R_OK) < 0 ) {
		(void)fprintf(stderr, "Cannot access %s\n", datafile);
		return -1;
	}
	if ( (fd = open(datafile, O_RDONLY)) < 0 ) {
		(void)fprintf(stderr, "Cannot open %s\n", datafile);
		return -1;
	}

	tzsize = read(fd, zibuf, sizeof zibuf);
	if ( close(fd) != 0 || tzsize < sizeof (*tzhp) ) {
		(void)fprintf(stderr, "Error reading %s\n", datafile);
		return -1;
	}

	/*
	** Parse the zoneinfo data file header.
	*/
	tzhp = (struct tzhead *)zibuf;
#ifdef STRUCT_TZHEAD_TTISGMTCNT
	ttisgmtcnt = tzhdecode(tzhp->tzh_ttisgmtcnt);
#endif
	ttisstdcnt = tzhdecode(tzhp->tzh_ttisstdcnt);
	leapcnt = tzhdecode(tzhp->tzh_leapcnt);
	timecnt = tzhdecode(tzhp->tzh_timecnt);
	typecnt = tzhdecode(tzhp->tzh_typecnt);
	charcnt = tzhdecode(tzhp->tzh_charcnt);

	if ( ! quietflag ) {
#ifdef STRUCT_TZHEAD_TTISGMTCNT
		(void)printf("ttisgmtcnt = %d\n", ttisgmtcnt);
#endif
		(void)printf("ttisstdcnt = %d\n", ttisstdcnt);
		(void)printf("leapcnt = %d\n", leapcnt);
		(void)printf("timecnt = %d\n", timecnt);
		(void)printf("typecnt = %d\n", typecnt);
		(void)printf("charcnt = %d\n", charcnt);
	}

	if ( timecnt > TZ_MAX_TIMES ) {
		(void)fprintf(stderr, "timecnt too large (%d)\n", timecnt);
		return -1;
	}
	if ( typecnt == 0 ) {
		(void)fprintf(stderr, "typecnt too small (%d)\n", typecnt);
		return -1;
	}
	if ( typecnt > TZ_MAX_TYPES ) {
		(void)fprintf(stderr, "typecnt too large (%d)\n", typecnt);
		return -1;
	}
	if ( charcnt > TZ_MAX_CHARS ) {
		(void)fprintf(stderr, "charcnt too large (%d)\n", charcnt);
		return -1;
	}
	if ( leapcnt > TZ_MAX_LEAPS ) {
		(void)fprintf(stderr, "leapcnt too large (%d)\n", leapcnt);
		return -1;
	}
	if ( ttisstdcnt > TZ_MAX_TYPES ) {
		(void)fprintf(stderr, "ttisstdcnt too large (%d)\n", ttisstdcnt);
		return -1;
	}
#ifdef STRUCT_TZHEAD_TTISGMTCNT
	if ( ttisgmtcnt > TZ_MAX_TYPES ) {
		(void)fprintf(stderr, "ttisgmtcnt too large (%d)\n", ttisgmtcnt);
		return -1;
	}
#endif
	if ( tzsize < sizeof (*tzhp)
			+ timecnt * (4 + 1) * sizeof (char)
			+ typecnt * (4 + 2) * sizeof (char)
			+ charcnt * sizeof (char)
			+ leapcnt * (4 + 4) * sizeof (char)
			+ ttisstdcnt * sizeof (char)
#ifdef STRUCT_TZHEAD_TTISGMTCNT
			+ ttisgmtcnt * sizeof (char)
#endif
		)
	{
		(void)fprintf(stderr, "tzsize too small (%d)\n", tzsize);
		return -1;
	}

	/*
	** Parse the remainder of the zoneinfo data file.
	*/

	p = zibuf + sizeof (*tzhp);

	/* transition times */
	for ( i = 0 ; i < timecnt ; i++ ) {
		transit[i].time = tzhdecode(p);
		p += 4;
		/* record the next two (future) transitions */
		if ( transit[i].time > now ) {
			if ( tt[0].time == 0 ) {
				tt[0].time = transit[i].time;
				tt[0].index = i;
			}
			else if ( tt[1].time == 0 ) {
				tt[1].time = transit[i].time;
				tt[1].index = i;
			}
		}
	}
	/* local time types for above: 0 = std, 1 = dst */
	for ( i = 0 ; i < timecnt ; i++ ) {
		transit[i].type = (unsigned char) *p++;
	}

	if ( verboseflag ) {
		for ( i = 0 ; i < timecnt ; i++ )
			(void)printf("transit[%d]: time=%ld (%s) type=%ld\n",
				i, transit[i].time, ctimeGMT(transit[i].time),
				transit[i].type);
	}

	/* GMT offset seconds, local time type, abbreviation index */
	for ( i = 0 ; i < typecnt ; i++ ) {
		lti[i].gmtoffset = tzhdecode(p);
		p += 4;
		lti[i].isdst = (unsigned char) *p++;
		lti[i].abbrind = (unsigned char) *p++;
	}

	/* timezone abbreviation strings */
	for ( i = 0 ; i < charcnt ; i++ ) {
		chars[i] = *p++;
		if ( ! quietflag ) {
			if ( isprint(chars[i]) )
				(void)printf("chars[%d] = '%c'\n", i, chars[i]);
			else
				(void)printf("chars[%d] = %x\n", i, chars[i]);
		}
	}
	chars[i] = '\0';	/* ensure '\0' at end */

	/* leap second transitions, accumulated correction */
	for ( i = 0 ; i < leapcnt ; i++ ) {
		leaps[i].transit = tzhdecode(p);
		p += 4;
		leaps[i].correct = tzhdecode(p);
		p += 4;
		if ( ! quietflag ) {
			(void)printf("leaps[%d]: transit=%ld correct=%ld\n",
					i, leaps[i].transit, leaps[i].correct);
		}
	}

	/*
	** indexed by type:
	**	0 = transition is wall clock time
	**	1 = transition time is standard time
	**	default (if absent) is wall clock time
	*/
	for ( i = 0 ; i < ttisstdcnt ; i++ ) {
		lti[i].stds = *p++;
	}

#ifdef STRUCT_TZHEAD_TTISGMTCNT
	/*
	** indexed by type:
	**	0 = transition is local time
	**	1 = transition time is GMT
	**	default (if absent) is local time
	*/
	for ( i = 0 ; i < ttisgmtcnt ; i++ ) {
		lti[i].gmts = *p++;
	}
#endif /* STRUCT_TZHEAD_TTISGMTCNT */


	if ( ! quietflag ) {
		for ( i = 0 ; i < typecnt ; i++ ) {
			(void)printf("lti[%d]: gmtoffset=%d isdst=%d abbrind=%d"
				" stds=%d"
#ifdef STRUCT_TZHEAD_TTISGMTCNT
				" gmts=%d"
#endif
				"\n",
				i, lti[i].gmtoffset, lti[i].isdst,
				lti[i].abbrind, lti[i].stds
#ifdef STRUCT_TZHEAD_TTISGMTCNT
				, lti[i].gmts
#endif
				);
		}
	}


	/* Simple case of no dst */
	if ( typecnt == 1 ) {
		(void)timefmt(stdoffset, sizeof stdoffset, -lti[0].gmtoffset);

		(void)printf("# %s\n", tzval);
		(void)printf("%s%s%s\n", comment, wrapabbrev(&chars[lti[0].abbrind]), stdoffset);

		return 0;
	}

	/*
	** XXX If no transitions exist in the future, should we assume
	** XXX that dst no longer applies, or should we assume the most
	** XXX recent rules continue to apply???
	** XXX For the moment, we assume the latter and proceed.
	*/
	if ( tt[0].time == 0 && tt[1].time == 0 ) {
#if 0
		tt[1].index = timecnt - 1;
		tt[0].index = tt[1].index - 1;
		tt[1].time = transit[tt[1].index].time;
		tt[0].time = transit[tt[0].index].time;
#endif
		(void)timefmt(stdoffset, sizeof stdoffset, -lti[transit[timecnt-1].type].gmtoffset);

		(void)printf("# %s\n", tzval);
		(void)printf("%s%s%s\n", comment, wrapabbrev(&chars[lti[transit[timecnt-1].type].abbrind]), stdoffset);

		return 0;
	}
	else if ( tt[1].time == 0 ) {
		tt[1].index = tt[0].index;
		tt[0].index--;
		tt[1].time = transit[tt[1].index].time;
		tt[0].time = transit[tt[0].index].time;
	}

	tt[0].type = transit[tt[0].index].type;
	tt[1].type = transit[tt[1].index].type;


	/*
	** Convert time_t values to struct tm values.
	*/
	for ( i = 0 ; i <= 1 ; i++ ) {
		time_t	tmptime;

#ifdef STRUCT_TZHEAD_TTISGMTCNT
		if ( lti[tt[i].type].gmts == 1 )
			tmptime = tt[i].time;
		else
#endif /* STRUCT_TZHEAD_TTISGMTCNT */
			tmptime = tt[i].time
					+ lti[tt[(i>0)?0:1].type].gmtoffset;
		if ( lti[i].stds != 0 && lti[tt[i].type].isdst != 0 )
			tmptime += lti[tt[i].type].gmtoffset
					- lti[tt[(i>0)?0:1].type].gmtoffset;
		tt[i].tm = gmtime(&tmptime);
		tt[i].tm = memcpy(&tmbuf[i], tt[i].tm, sizeof(struct tm));
	}

	if ( ! quietflag ) {
		(void)printf("tt[0]: time=%ld (%s) index=%d type=%d\n",
				tt[0].time, ctimeGMT(tt[0].time),
				tt[0].index, tt[0].type);
		(void)printf("tt[1]: time=%ld (%s) index=%d type=%d\n",
				tt[1].time, ctimeGMT(tt[1].time),
				tt[1].index, tt[1].type);
	}

#if 0
	if ( tt[0].type == tt[1].type ) {
		/* Ooooops */
		;
	}
#endif
	if ( lti[tt[0].type].isdst == 1 ) {
		startindex = 0;
		endindex = 1;
	}
	else {
		startindex = 1;
		endindex = 0;
	}
	starttm = tt[startindex].tm;
	endtm = tt[endindex].tm;

	/* XXX This calculation of the week is too simple-minded??? */
	/* XXX A hueristic would be to round 4 up to 5. */
	(void)sprintf(startdate, "M%d.%d.%d",
			1 + starttm->tm_mon,
			weekofmonth(starttm->tm_mday, starttm->tm_wday),
			starttm->tm_wday
			);
	if ((starttm->tm_min != 0) || (starttm->tm_sec != 0)) {
	(void)sprintf(starttime, "/%.2d:%.2d:%.2d",
			starttm->tm_hour,
			starttm->tm_min,
			starttm->tm_sec
			);
	} else {
		if (starttm->tm_hour != 2)
			(void)sprintf(starttime, "/%d", starttm->tm_hour);
		else
			starttime[0] = 0;
	}
	(void)sprintf(enddate, "M%d.%d.%d",
			1 + endtm->tm_mon,
			weekofmonth(endtm->tm_mday, endtm->tm_wday),
			endtm->tm_wday
			);
	if ((endtm->tm_min != 0) || (endtm->tm_sec != 0)) {
	(void)sprintf(endtime, "/%.2d:%.2d:%.2d",
			endtm->tm_hour,
			endtm->tm_min,
			endtm->tm_sec
			);
	} else {
		if (endtm->tm_hour != 2)
			(void)sprintf(endtime, "/%d", endtm->tm_hour);
		else
			endtime[0] = 0;
	}

	(void)timefmt(stdoffset, sizeof stdoffset,
			-lti[tt[endindex].type].gmtoffset);
	(void)timefmt(dstoffset, sizeof dstoffset,
			-lti[tt[startindex].type].gmtoffset);

	(void)printf("# %s\n", tzval);
	(void)printf("%s%s%s%s", comment, wrapabbrev(&chars[lti[tt[endindex].type].abbrind]), stdoffset,
	        wrapabbrev(&chars[lti[tt[startindex].type].abbrind]));
	if ((lti[tt[startindex].type].gmtoffset - lti[tt[endindex].type].gmtoffset) != 3600)
		(void)printf("%s", dstoffset);
	(void)printf(",%s%s,%s%s\n",
		startdate, starttime,
		enddate, endtime);

	return 0;
}


/*
*/
int
main(int argc, char *argv[])
{
	extern int	optind;
	int		ch, prev;
	int		i, val;
	int		retval = 0;

	TRACE(("main started\n")); fflush(stdout);
	/* Process the command line */
	if ( (progname = strrchr(argv[0], '/')) != NULL )
		progname++;
	else
		progname = argv[0];

	/* command line switches */
	/*
	** -p	use specified zoneinfo directory
	** -q	print only TZ
	** -v	print transition time data
	*/
	while ( (ch = getopt(argc, argv, "hp:qcvV")) != EOF ) {
		switch ( ch ) {
		case 'h':
			(void)printf("Usage: %s %s\n%s\n",
					progname, usage, help);
			return 0;
			/* NOTREACHED */
			break;
		case 'p':
			zoneinfopath = optarg;
			break;
		case 'q':
			quietflag = 1;
			verboseflag = 0;
			break;
		case 'c':
			comment = "#";
			break;
		case 'v':
			quietflag = 0;
			verboseflag = 1;
			break;
		case 'V':
# if RCSID == rcsid
			(void)printf("%s\n", RCSID);
# endif
# if RCSNAME == rcsname
			(void)printf("%s\n", RCSNAME);
# endif
			return 0;
			/* NOTREACHED */
			break;
		case '?':
			(void)fprintf(stderr, "Usage: %s %s\n",
					progname, usage);
			return 2;
			/* NOTREACHED */
			break;
		}
	}
	argc -= optind;
	argv += optind;


	/* Loop over given timezone names, using default if none. */
	if ( argc < 1 ) {
		retval = dumptzdata(NULL);
	}
	else {
		while ( *argv ) {
			retval |= dumptzdata(*argv);
			argv++;
		}
	}

	return retval;
}


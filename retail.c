/**
 *  retail.c -- ASCII file tail program that remembers last position.
 *
 *      Copyright (c) 1996 Craig H. Rowland <crowland@psionic.com>
 *      Copyright (c) Ross Moffatt <ross.stuff@telstra.com>
 *      Copyright (c) 2014 Mark Bucciarelli <mkbucc@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public License
 *      Version 2.1 as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *      Lesser General Public License for more details.
 *
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <zlib.h>

/*
 * PATH_MAX seems broken, here's a choice quote from
 *
 * 	http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
 *
 *	Since a path can be longer than PATH_MAX, the define is useless,
 *	writing code based off of it is wrong, and the functions that require
 *	it are broken.
 *
 * We set it to 1024 (OpenBSD's value).
 */
#define MY_PATH_MAX 1024
#define BUFSZ 4096
#define USAGE "Usage: retail [-o <offset filename>] <log filename>"


/*
 *  The regular basename behaves differently on different operating systems.
 */
static char    *
mybasename(const char *logfn)
{
	static char	buf [MY_PATH_MAX] = {0};

	if (!logfn || !strlen(logfn))
		return buf;

	if (strlen(logfn) > MY_PATH_MAX - 1)
		errx(EXIT_FAILURE, "can't get basename, filename too long: '%s'", logfn);

	strcpy(buf, logfn);

	return basename(buf);
}

/*
 * Same for dirname
 */
static char    *
mydirname(const char *logfn)
{
	static char	buf [MY_PATH_MAX] = {0};

	if (!logfn || !strlen(logfn))
		return buf;

	if (strlen(logfn) > MY_PATH_MAX - 1)
		errx(EXIT_FAILURE, "can't get dirname, filename too long: '%s'", logfn);

	strcpy(buf, logfn);

	return dirname(buf);
}

static char    *
build_offsetfn(const char *logfn, char *offsetfn)
{
	static char	rval[MY_PATH_MAX] = {0};
	size_t		sz = 0;

	/*
	 * The offset filename is given.
	 */
	if (offsetfn && strlen(offsetfn) && offsetfn[strlen(offsetfn) - 1] != '/') {
		if (strlen(offsetfn) > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "offset filename is too long");
		strcpy(rval, offsetfn);
	}

	/*
	 * The offset filename is given
	 * and is a directory.
	 */
	else if (offsetfn && strlen(offsetfn) && offsetfn[strlen(offsetfn) - 1] == '/') {
		if (!logfn || strlen(logfn) == 0)
			errx(EXIT_FAILURE, "log filename is empty");
		sz = strlen(offsetfn) + strlen("offset.") + strlen(mybasename(logfn));
		if (sz > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "offset filename is too long");
		strcpy(rval, offsetfn);
		strcat(rval, "offset.");
		strcat(rval, mybasename(logfn));
	}

	/*
	 * No offset filename specified.
	 */
	else if (!offsetfn || !strlen(offsetfn)) {
		sz = strlen(mydirname(logfn)) + strlen("/offset.") + strlen(mybasename(logfn));
		if (sz > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "offset filename is too long");
		strcpy(rval, mydirname(logfn));
		strcat(rval, "/offset.");
		strcat(rval, mybasename(logfn));
	}

	else
		errx(EXIT_FAILURE, "logic error in build_offsetfn()");
	return rval;
}

struct conditional_data {
	ino_t		loginode;
	ino_t		otherinode;
	long long	mostrecent_mtime;
	long long	other_mtime;
	const char     *logfn;
	const char     *otherfn;
};

/*
 * A conditional is a function that defines what the last log is.
 */
typedef int     (*conditional) (const struct conditional_data *);

static int
sameinode(const struct conditional_data *p)
{
	return p->loginode == p->otherinode
	&& strncmp(p->otherfn, p->logfn, strlen(p->logfn)) == 0;
}

static int
mostrecent(const struct conditional_data *p)
{
	return strncmp(p->otherfn, p->logfn, strlen(p->logfn)) == 0
	&& p->other_mtime > p->mostrecent_mtime;
}

static int
mostrecentgz(const struct conditional_data *p)
{
	return strncmp(p->otherfn, p->logfn, strlen(p->logfn)) == 0
	&& p->other_mtime > p->mostrecent_mtime
	&& strlen(p->otherfn) > 2
	&& !strcmp(p->otherfn + strlen(p->otherfn) - 3, ".gz");
}

/*
 * Find the rotated log.
 */
static char    *
find_lastlog(const char *logfn, ino_t logino, conditional update_lastlog)
{
	static char	rval[MY_PATH_MAX] = {0};
	char		fn        [MY_PATH_MAX] = {0};
	struct conditional_data state;
	struct dirent  *ep = 0;
	struct stat	fstat;
	DIR            *dp;
	char           *dir = 0;
	char           *base = 0;
	size_t		sz = 0;

	dir = mydirname(logfn);
	base = mybasename(logfn);
	if (NULL == (dp = opendir(dir)))
		err(EXIT_FAILURE, "can't open directory '%s'", dir, NULL);

	memset(&state, 0, sizeof(state));
	state.logfn = base;
	state.mostrecent_mtime = 0;
	state.loginode = logino;
	while ((ep = readdir(dp))) {
		if (!strcmp(ep->d_name, ".")
		    || !strcmp(ep->d_name, "..")
		    || strlen(ep->d_name) <= strlen(base))
			continue;

		sz = strlen(dir) + strlen(ep->d_name) + 2;
		if (sz > sizeof(fn))
			errx(EXIT_FAILURE, "filename too big:	 '%s/%s'", dir, ep->d_name);
		strcpy(fn, dir);
		strcat(fn, "/");
		strcat(fn, ep->d_name);

		if ((stat(fn, &fstat)) != 0)
			err(EXIT_FAILURE, "can't stat '%s'", fn);
		state.otherfn = ep->d_name;
		state.other_mtime = fstat.st_mtime;
		state.otherinode = fstat.st_ino;

		/*
			If this is a "more recent" log file,
			update current pointer to the last
			log.  I put "more recent" in quotes
			because the specific conditional
			depends on what kind of rotation
			was used.
		*/
		if ((*update_lastlog) (&state)) {
			state.mostrecent_mtime = fstat.st_mtime;
			strcpy(rval, fn);
		}
	}
	if (-1 == closedir(dp))
		err(EXIT_FAILURE, "can't close directory '%s'", dir);

	return rval;
}

__attribute__((format(printf, 2, 3)))
__attribute__((noreturn))
	static void
			gzdie         (gzFile * fp, const char *fmt,...)
{
	va_list		args;
	const char     *emsg = 0;
	int		emsgno;

	emsg = gzerror(fp, &emsgno);
	va_start(args, fmt);
	if (emsgno == Z_ERRNO)
		verr(EXIT_FAILURE, fmt, args);
	else
		verrx(EXIT_FAILURE, fmt, args);
	va_end(args);
}


/*
 * Given a filename and an offset, print all bytes after offset to stdout.
 * Works for gzipped and ungzipped files.
 */
static		z_off_t
dump_changes(const char *fn, const z_off_t pos)
{
	char		buf       [BUFSZ] = {0};
	z_off_t		rval = 0;
	gzFile         *fp = 0;
	int		charsread = 0;
	int		rc = 0;

	syslog(LOG_DEBUG, "dump_changes('%s', %lu)", fn, pos);

	if (NULL == (fp = gzopen(fn, "rb")))
		err(EXIT_FAILURE, "can't dump changes in '%s'", fn);

	if (-1 == gzseek(fp, pos, SEEK_SET))
		gzdie(fp, "can't gzseek to %ld in %s", pos, fn);

	do {
		buf[0] = 0;
		charsread = gzread(fp, buf, BUFSZ);
		if (charsread < 0)
			gzdie(fp, "can't gzread from %s", fn);
		rval += (unsigned int)charsread;
		if ((unsigned int)charsread != fwrite(buf, 1, (unsigned int)charsread, stdout))
			err(EXIT_FAILURE, "error writing changes to stdout");

	} while (charsread == BUFSZ);

	rc = gzclose(fp);
	/*
	 * On Ubuntu Lucid, the zlib.h in zlib1g-dev returns Z_BUF_ERROR
	 * when gzclose'ing an empty file.
	 *
	 * This behavior does not manifest on OSX.
	 */
	if (!(rc == 0 || (rc == Z_BUF_ERROR && rval == 0)))
		err(EXIT_FAILURE, "failed to close '%s'", fn);

	syslog(LOG_DEBUG, "dump_changes returns %lu (bytes read)", rval);

	return rval;
}



/*
 * Output any new lines added to log since last run,
 * and update offset.
 */
static int
check_log(const char *logfn, const char *offsetfn)
{
	FILE           *logfp,
	               *offsetfp;
	struct stat	logfstat;
	char           *lastlog = 0;
	z_off_t		lastoffset;
	ino_t		lastinode = 0;
	z_off_t		lastsize = 0;

	/*
	 *  Check if the file exists in specified directory.  Open as
	 *  a binary in case the user reads in non-text files.
	 */
	if ((logfp = fopen(logfn, "rb")) == NULL)
		err(EXIT_FAILURE, "can't check log '%s'", logfn);
	if ((stat(logfn, &logfstat)) != 0)
		err(EXIT_FAILURE, "can't stat '%s'", logfn);

	syslog(LOG_DEBUG, "check_log: st_size of '%s' = %lld", logfn, (long long int)logfstat.st_size);


	/*
	 * If we are on a 32-bit system,
	 * exit if the file is too big.
	 * XXX: Can this really happen? -- Mark
	 */
	if ((sizeof(fpos_t) == 4) || sizeof(logfstat.st_size) == 4) {
		if ((logfstat.st_size > 2147483646) || (logfstat.st_size < 0))
			errx(EXIT_FAILURE, "log file, %s, is too large at %lld bytes.\n",
			     logfn, (long long)logfstat.st_size);
	}

	/*
	 * Load offset data.
	 */
	if ((offsetfp = fopen(offsetfn, "rb")) != NULL) {
		if (1 != fread(&lastinode, sizeof(lastinode), 1, offsetfp))
			errx(EXIT_FAILURE, "error reading last inode from '%s'", offsetfn);
		if (1 != fread(&lastoffset, sizeof(lastoffset), 1, offsetfp))
			errx(EXIT_FAILURE, "error reading last offset from '%s'", offsetfn);
		if (1 != fread(&lastsize, sizeof(lastsize), 1, offsetfp))
			errx(EXIT_FAILURE, "error reading last size from '%s'", offsetfn);
		if (0 != fclose(offsetfp))
			err(EXIT_FAILURE, "can't close '%s'", offsetfn);
		if (lastoffset > lastsize)
			errx(EXIT_FAILURE, "last offset (%ld) greater than last size (%ld) in '%s'",
			     lastoffset, lastsize, offsetfn);

	}
	else {
		lastoffset = 0;
		lastinode = logfstat.st_ino;
	}

	/*
	 * If the current file lastinode is the same,
	 * but the file lastsize has
	 * grown SMALLER than the last time we checked,
	 * then assume the log file has been rolled
	 * via a copy and delete.
	 */

	if ((lastinode == logfstat.st_ino) && (lastsize > logfstat.st_size)) {
		lastlog = find_lastlog(logfn, lastinode, &mostrecent);

		/*
		 * If we can't find the old log file,
		 * reset the last offset to zero
		 * so we dump the entire current log.
		 */
		if (!lastlog || !strlen(lastlog))
			lastoffset = 0;
	}

	/*
	 * If the lastinode of the current log file
	 * is different than the one store in the offset file,
	 * then assume the log file was rotated
	 * by a mv and then recreated.
	 */
	else if (lastinode != logfstat.st_ino) {
		lastlog = find_lastlog(logfn, lastinode, &sameinode);
		if (!lastlog || !strlen(lastlog))
			lastlog = find_lastlog(logfn, lastinode, &mostrecentgz);

		if (!lastlog || !strlen(lastlog))
			lastoffset = 0;
	}
	else
		lastlog = 0;

	if (lastlog && strlen(lastlog)) {
		dump_changes(lastlog, lastoffset);
		lastoffset = 0;
	}

	lastoffset += dump_changes(logfn, lastoffset);
	if (lastoffset > logfstat.st_size) {
		syslog(LOG_ERR, "offset > st_size (%lu > %lld), setting size to offset", lastoffset, (long long int)logfstat.st_size);
		logfstat.st_size = lastoffset;
	}

	/*
	 * Write the new offset data.
	 */
	if ((offsetfp = fopen(offsetfn, "w")) == NULL)
		err(EXIT_FAILURE, "can't write offset to '%s'", offsetfn);

	if ((chmod(offsetfn, 00660)) != 0)
		err(EXIT_FAILURE, "Cannot set permissions on file %s", offsetfn);

	if (1 != fwrite(&logfstat.st_ino, sizeof(logfstat.st_ino), 1, offsetfp))
		err(EXIT_FAILURE, "can't write last inode to '%s'", offsetfn);

	if (1 != fwrite(&lastoffset, sizeof(lastoffset), 1, offsetfp))
		err(EXIT_FAILURE, "can't write last offset to '%s'", offsetfn);

	if (1 != fwrite(&logfstat.st_size, sizeof(logfstat.st_size), 1, offsetfp))
		err(EXIT_FAILURE, "can't write last size to '%s'", offsetfn);

	if (0 != fclose(offsetfp))
		err(EXIT_FAILURE, "can't close '%s'", offsetfn);

	return (0);

}

int
main(int argc, char *argv[])
{
	char		logfn     [MY_PATH_MAX] = {0};
	char           *offsetfn = 0;
	char           *p;

	openlog("retail", LOG_PID, LOG_USER);

	switch (argc) {
	case 2:
		p = argv[1];
		if (*p == '-')
			errx(EXIT_FAILURE, USAGE);
		if (strlen(argv[1]) < MY_PATH_MAX - 1)
			strcpy(logfn, argv[1]);
		else
			errx(EXIT_FAILURE, "log file name too long");
		offsetfn = build_offsetfn(argv[1], NULL);
		break;
	case 4:
		p = argv[1];
		if (*p != '-' || (*p && *(p + 1) != 'o'))
			errx(EXIT_FAILURE, USAGE);
		if (strlen(argv[3]) < MY_PATH_MAX - 1)
			strcpy(logfn, argv[3]);
		else
			errx(EXIT_FAILURE, "log file name too long");
		offsetfn = build_offsetfn(argv[3], argv[2]);
		break;
	default:
		errx(EXIT_FAILURE, USAGE);
	}

	check_log(logfn, offsetfn);

	closelog();

	return 0;
}

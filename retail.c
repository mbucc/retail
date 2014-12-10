/**
 *  retail.c -- ASCII file tail program that remembers last position.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sysexits.h>
#include <libgen.h>

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
#define USAGE "retail [-o <offset filename>] <log filename>"

static char    *
build_offsetfn(char *logfn, char *offsetfn)
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
		sz = strlen(offsetfn) + strlen("offset.") + strlen(basename(logfn));
		if (sz > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "offset filename is too long");
		strcpy(rval, offsetfn);
		strcat(rval, "offset.");
		strcat(rval, basename(logfn));
	}

	/*
	 * No offset filename specified.
	 */
	else if (!offsetfn || !strlen(offsetfn)) {
		sz = strlen(dirname(logfn)) + strlen("/offset.") + strlen(basename(logfn));
		if (sz > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "offset filename is too long");
		strcpy(rval, dirname(logfn));
		strcat(rval, "/offset.");
		strcat(rval, basename(logfn));
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

typedef int     (*conditional) (const struct conditional_data *);

static int
sameinode(const struct conditional_data *p)
{
	return p->loginode == p->otherinode;
}

static int
mostrecent(const struct conditional_data *p)
{
	return strncmp(p->otherfn, p->logfn, strlen(p->logfn)) == 0
	&& strlen(p->otherfn) > strlen(p->logfn)
	&& p->other_mtime > p->mostrecent_mtime;
}

static char    *
find_lastlog(char *logfn, ino_t logino, conditional update_lastlog)
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

	dir = dirname(logfn);
	base = basename(logfn);
	if (NULL == (dp = opendir(dir)))
		err(EXIT_FAILURE, "'%s'", dir, NULL);

	memset(&state, 0, sizeof(state));
	state.logfn = base;
	state.mostrecent_mtime = 0;
	state.loginode = logino;
	while ((ep = readdir(dp))) {
		if (strcmp(ep->d_name, ".") != 0 || strcmp(ep->d_name, "..") != 0)
			continue;
		sz = strlen(dir) + strlen(ep->d_name) + 2;
		if (sz > sizeof(fn))
			errx(EXIT_FAILURE, "filename too big:	 '%s/%s'", dir, ep->d_name);
		strcpy(fn, dir);
		strcat(fn, "/");
		strcat(fn, ep->d_name);

		if ((stat(fn, &fstat)) != 0)
			err(EXIT_FAILURE, NULL);
		state.otherfn = ep->d_name;
		state.other_mtime = fstat.st_mtime;
		state.otherinode = fstat.st_ino;
		if ((*update_lastlog) (&state)) {
			state.mostrecent_mtime = fstat.st_mtime;
			strcpy(rval, fn);
		}
	}
	closedir(dp);
	return rval;
}


static void
dump_changes(const char *fn, const fpos_t pos)
{
	char		buf       [BUFSZ] = {0};
	FILE           *fp = 0;
	size_t		charsread = 0;

	if (NULL == (fp = fopen(fn, "rb")))
		err(EXIT_FAILURE, "'%s'", fn, NULL);

	fsetpos(fp, &pos);

	do {
		buf[0] = 0;
		charsread = fread(buf, 1, BUFSZ, fp);
		fwrite(buf, 1, charsread, stdout);
	} while (charsread == BUFSZ);

	if (0 != fclose(fp))
		err(EXIT_FAILURE, NULL);
}



/*
 * Output any new lines added to log since last run,
 * and update offset.
 */
static int
check_log(char *logfn, const char *offsetfn)
{
	FILE           *logfp,
	               *offsetfp;
	struct stat	logfstat;
	conditional	lastlog_finder;
	char           *lastlog;
	char           *buf = 0;
	fpos_t		offset_position;
	ino_t		inode = 0;
	off_t		size = 0;

	/*
	 *  Check if the file exists in specified directory.  Open as
	 *  a binary in case the user reads in non-text files.
	 */
	if ((logfp = fopen(logfn, "rb")) == NULL)
		err(EXIT_FAILURE, "'%s'", logfn, NULL);
	if ((stat(logfn, &logfstat)) != 0)
		err(EXIT_FAILURE, "'%s'", logfn, NULL);

	/*
	 * If we are on a 32-bit system,
	 * exit if the file is too big.
	 * XXX: Can this really happen? -- Mark
	 */
	if ((sizeof(fpos_t) == 4) || sizeof(logfstat.st_size) == 4) {
		if ((logfstat.st_size > 2147483646) || (logfstat.st_size < 0))
			errx(EXIT_FAILURE, "log file, %s, is too large at %lld bytes.\n", logfn, (long long)logfstat.st_size);
	}

	/*
	 * Load offset data.
	 */
	if ((offsetfp = fopen(offsetfn, "rb")) != NULL) {
		fread(&inode, sizeof(inode), 1, offsetfp);
		fread(&offset_position, sizeof(offset_position), 1, offsetfp);
		fread(&size, sizeof(size), 1, offsetfp);
		if (0 != fclose(offsetfp))
			err(EXIT_FAILURE, NULL);
	}
	else {
		fgetpos(logfp, &offset_position);
		inode = logfstat.st_ino;
	}



	/*
	 * If the current file inode is the same,
	 * but the file size has
	 * grown SMALLER than the last time we checked,
	 * then assume the log file has been rolled
	 * via a copy and delete.
	 */
	if ((inode == logfstat.st_ino) && (size > logfstat.st_size))
		lastlog_finder = &mostrecent;
	/*
	 * If the inode of the current log file
	 * is different than the one store in the offset file,
	 * then assume the log file was rotated
	 * by a mv and then recreated.
	 */
	else if (inode != logfstat.st_ino)
		lastlog_finder = &sameinode;
	else
		lastlog_finder = 0;
	if (lastlog_finder) {
		lastlog = find_lastlog(logfn, logfstat.st_ino, lastlog_finder);
		if (strlen(lastlog)) {
			dump_changes(lastlog, offset_position);
			offset_position = 0;
		}
	}

	dump_changes(logfn, offset_position);

	/* after we are done we need to write the new offset */
	if ((offsetfp = fopen(offsetfn, "w")) == NULL)
		err(EXIT_FAILURE, "'%s'", offsetfn, NULL);
	/* Don't let everyone read offset */
	if ((chmod(offsetfn, 00660)) != 0)
		errx(EXIT_FAILURE, "Cannot set permissions on file %s\n", offsetfn);
	fwrite(&logfstat.st_ino, sizeof(logfstat.st_ino), 1, offsetfp);
	fwrite(&offset_position, sizeof(offset_position), 1, offsetfp);
	if (1 != fwrite(&logfstat.st_size, sizeof(logfstat.st_size), 1, offsetfp))
		errx(EXIT_FAILURE, "write failed");
	if (0 != fclose(offsetfp))
		err(EXIT_FAILURE, NULL);
	free(buf);
	return (0);		/* everything A-OK */
}

int
main(int argc, char *argv[])
{
	char		logfn     [MY_PATH_MAX] = {0};
	char           *offsetfn = 0;
	char           *p;

	switch (argc) {
	case 2:
		p = argv[1];
		if (*p == '-')
			errx(EXIT_FAILURE, USAGE);
		offsetfn = build_offsetfn(argv[1], NULL);
		break;
	case 4:
		p = argv[1];
		if (*p != '-' || (*p && *(p + 1) != 'o'))
			errx(EXIT_FAILURE, USAGE);
		offsetfn = build_offsetfn(argv[3], argv[2]);
		break;
	default:
		errx(EXIT_FAILURE, USAGE);
	}

	check_log(logfn, offsetfn);

	return 0;
}

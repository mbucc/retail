/**
 *  retail.c -- ASCII file tail program that remembers last position.
 *
 *  Author:
 *  Ross Moffatt <ross.stuff@telstra.com>
 *  Mark Bucciarelli <mkbucc@gmail.com>
 *
 * Modified from the utility: retail
 * Written by Craig H. Rowland <crowland@psionic.com>
 * Based upon original utility: retail (c)Trusted Information Systems
 *
 * This program covered by the GNU License. This program is free to use as long as
 * the above copyright notices are left intact.
 * This program has no warranty of any kind.
 *
 * To compile:
 * Unix: gcc -o retail retail.c
 * add -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 for large file aware build
 *
 *  VERSION 2.0 : Log roll aware
 *  VERSION 2.1 : Fixes and Windows compatibility
 *  VERSION 2.11: Minor help text and layout update
 *  VERSION 3.0 : Path related fixes
 *  VERSION 3.1 : Changed -? to -h, DOS_ to D_OS_; added -t, 32bit file too big chk
 *  VERSION 3.11: Fixed 32bit file too big chk bug
 *  VERSION 3.2 : fseek(o),ftell(o),fgets to fgetpos,fsetpos,fgetc
 *                fgets,fprintf to fread,fwrite added -b -r -s options
 *                fixed win extra line feed out bug
 *  VERSION 3.21 : usage function, 32/64 bit aware, added debug, changed the logic.
 *  VERSION 4.0.0  Remove _OS_DOS support.
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
 * http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html:
 *
 *	Since a path can be longer than PATH_MAX, the define is useless,
 *	writing code based off of it is wrong, and the functions that require
 *	it are broken.
 *
 * We set it to 1024 (OpenBSD's value).
 */
#define MY_PATH_MAX 1024
#define VERSION "4.0.0"
#define BUFSZ 4096
#define USAGE "retail [-o <offset filename>] <log filename>"

/* Prototypes for functions */
static char    *nondirname(char *path);
static int	check_log(const char *logfn, const char *offsetfn);
static char    *right_string(char *my_path_file, int start_pos);

/*
 * Generate the offset filename for the given log file.
 *
 * Assumes the caller has already made sure log filename is not too long.
 */
char           *
logfn_to_offsetfn(const char *logfn)
{
	static char	rval[MY_PATH_MAX] = {0};
	char           *buf = 0;

	/*
	 * On Solaris, HP and AIX dirname() and basename() modify their argument.
	 */
	if (NULL == (buf = strdup(logfn)))
		errx(EXIT_FAILURE, "strdup failed copying log filename");
	strcpy(rval, dirname(buf));
	free(buf);
	strcat(rval, "/offset.");
	if (NULL == (buf = strdup(logfn)))
		errx(EXIT_FAILURE, "strdup failed copying log filename");
	strcat(rval, basename(buf));
	free(buf);
	return rval;
}

int
main(int argc, char *argv[])
{
	char		logfn     [MY_PATH_MAX] = {0};
	char		offsetfn  [MY_PATH_MAX] = {0};
	char           *p;
	char		*buf;
	int		i;

	switch (argc) {
	case 2:
		p = argv[1];
		if (*p == '-')
			errx(EXIT_FAILURE, USAGE);
		if (strlen(p) > MY_PATH_MAX - strlen("/offset.") - 1)
			errx(EXIT_FAILURE, "log file name is too long, max is %lu", MY_PATH_MAX - strlen("/offset.") - 1);
		strcpy(logfn, p);
		strcpy(offsetfn, logfn_to_offsetfn(logfn));
		break;
		/*
		 * retail -o $HOME/var/db/retail/ /var/log/syslog
		 */
	case 4:
		p = argv[1];
		if (*p != '-' || (*p && *(p + 1) != 'o'))
			errx(EXIT_FAILURE, USAGE);
		p = argv[3];
		if (strlen(p) > MY_PATH_MAX - 1)
			errx(EXIT_FAILURE, "log filename is too long, max is %d", MY_PATH_MAX);
		strcpy(logfn, p);
		p = argv[2];
		if (p[strlen(p) - 1] == '/') {
			strcpy(offsetfn, logfn_to_offsetfn(logfn));
			if (NULL == (buf = strdup(offsetfn)))
				err(EXIT_FAILURE, "can't strdup '%s'", offsetfn);
			if (strlen(p) > MY_PATH_MAX - strlen(basename(buf)) - 1)
				errx(EXIT_FAILURE, "log file name is too long, max is %lu", MY_PATH_MAX - strlen(basename(buf)) - 1);
			strcpy(offsetfn, p);
			strcat(offsetfn, basename(buf));
		}
		else {
			strcpy(offsetfn, p);
			if (strlen(p) > MY_PATH_MAX - 1)
				errx(EXIT_FAILURE, "offset filename too long, max is %d", MY_PATH_MAX);
		}
		break;
	default:
		errx(EXIT_FAILURE, USAGE);
	}

	check_log(logfn, offsetfn);

	return 0;
}

/* Called functions follow . . .  */

/* a function to return the right part of a string from a */
/* given number of character into the string */
char           *
right_string(char *my_path_file, int start_pos)
{
	char           *resultstr;
	char           *tempstr;
	int		counter = 0;

	if (start_pos <= 0)
		start_pos = 1;	/* Minimum length */
	resultstr = (char *)malloc(start_pos + 1);
	tempstr = resultstr;

	if (my_path_file) {
		while (*my_path_file++)
			counter++;
		while (start_pos-- >= 0 && counter-- >= 0)
			my_path_file--;
		if (*my_path_file) {
			while (*my_path_file)
				*tempstr++ = *my_path_file++;
		}
	}
	*tempstr = '\0';
	return resultstr;
}


char           *
nondirname(char *path)
{
	int		i;
	char           *tempstr_ptr;

	i = strlen(path) + 1;
	while ((path[i - 1] != '/') && (i > 1))
		i--;
	if ((path[i - 1] != '/') && (i == 1))
		i = i - 1;
	tempstr_ptr = right_string(path, strlen(path) - i);
	strcpy(path, tempstr_ptr);
	return path;
}

/*
 * Output any new lines added to log since last run,
 * and update offset.
 */
int
check_log(const char *logfn, const char *offsetfn)
{
	FILE           *input,	/* Value user supplies for input file */
	               *old_input,	/* Found filename log rolled to */
	               *offset_output;	/* name of the offset output file */

	struct stat	logfile_stat,
			logfile_stat_old;

	char		old_logfn [MY_PATH_MAX];
	char		tmpfn     [MY_PATH_MAX];
	char		logdir    [MY_PATH_MAX] = {0};
	char		logbasefn [MY_PATH_MAX] = {0};

	char           *buf = 0;

	DIR            *dp;
	struct dirent  *ep;

	fpos_t		offset_position;	/* position in the file to
						 * offset */

	long		file_mod_time;
	int		charsread = 0;

#if _FILE_OFFSET_BITS == 64
	long long	inode = 0,
			size = 0;

#else
	long		inode = 0,
			size = 0;

#endif

	/*
	 *  Check if the file exists in specified directory.  Open as
	 *  a binary in case the user reads in non-text files.
	 */
	if ((input = fopen(logfn, "rb")) == NULL)
		errx(EXIT_FAILURE, "File %s cannot be read.\n", logfn);
	if ((stat(logfn, &logfile_stat)) != 0)
		errx(EXIT_FAILURE, "Cannot get %s file size.\n", logfn);

	/*
	 * If we are on a 32-bit system,
	 * exit if the file is too big.
	 * XXX: Can this really happen? -- Mark
	 */
	if ((sizeof(fpos_t) == 4) || sizeof(logfile_stat.st_size) == 4) {
		if ((logfile_stat.st_size > 2147483646) || (logfile_stat.st_size < 0))
			errx(EXIT_FAILURE, "log file, %s, is too large at %lld bytes.\n", logfn, (long long)logfile_stat.st_size);
	}
	/*
	 * See if we can open an existing offset file and read in the inode.
	 */
	if ((offset_output = fopen(offsetfn, "rb")) != NULL) {
		fread(&inode, sizeof(inode), 1, offset_output);
		fread(&offset_position, sizeof(offset_position), 1, offset_output);
		fread(&size, sizeof(size), 1, offset_output);
		if (0 != fclose(offset_output))
			err(EXIT_FAILURE, NULL);
	}
	/*
	 * If we can't read the offset file,
	 * assume it doesn't exist.
	 */
	else {
		fgetpos(input, &offset_position);
		/* set the old inode number to the current file */
		inode = logfile_stat.st_ino;
	}


	if (NULL == (buf = strdup(logfn)))
		errx(EXIT_FAILURE, "can't make a copy of log file name");
	strcpy(logdir, dirname(buf));
	free(buf);
	buf = 0;

	if (NULL == (buf = strdup(logfn)))
		errx(EXIT_FAILURE, "can't make a copy of log file name");
	strcpy(logbasefn, basename(buf));
	free(buf);
	buf = 0;

	if (NULL == (buf = (char *)malloc(BUFSZ + 1)))
		errx(EXIT_FAILURE, "can't allocate memory");

	/*
	 * If the current file inode is the same,
	 * but the file size has
	 * grown SMALLER than the last time we checked,
	 * then assume the log file has been rolled
	 * via a copy and delete.
	 *
	 * Look for the old file,
	 * and if found
	 * output any lines added to it
	 * before it was rotated.
	 */
	if ((inode == logfile_stat.st_ino) && (size > logfile_stat.st_size)) {
		/*
		 * look in the log file directory for the most recent
		 */
		strcpy(old_logfn, "NoFileFound");
		if (NULL == (dp = opendir(logdir)))
			err(EXIT_FAILURE, NULL);
		file_mod_time = 0;
		while ((ep = readdir(dp))) {
			if (strcmp(ep->d_name, ".") != 0 || strcmp(ep->d_name, "..") != 0)
				continue;
			strcpy(tmpfn, logdir);
			strcat(tmpfn, "/");
			strcat(tmpfn, ep->d_name);

			if ((stat(tmpfn, &logfile_stat_old)) != 0)
				errx(EXIT_FAILURE, NULL);
			if ((strncmp(ep->d_name, logbasefn, strlen(logbasefn)) == 0)
			    && (strlen(ep->d_name) > strlen(logbasefn))
			    && (logfile_stat_old.st_mtime > file_mod_time)) {
				file_mod_time = logfile_stat_old.st_mtime;
				strcpy(old_logfn, ep->d_name);
			}
		}
		(void)closedir(dp);


		/* if we found a file, then deal with it, or bypass */
		if (strcmp(old_logfn, "NoFileFound") != 0) {
			/* put the directory and old filename back together */
			strcpy(tmpfn, logdir);
			strcat(tmpfn, "/");
			strcat(tmpfn, old_logfn);

			/* open the found filename */
			if ((old_input = fopen(tmpfn, "rb")) == NULL) {
				fprintf(stderr, "ERROR 512 - File %s cannot be read.\n", tmpfn);
				exit(EXIT_FAILURE);
			}
			/* print out the old log stuff */
			fsetpos(old_input, &offset_position);

			/* Print the file */
			do {
				buf[0] = 0;
				charsread = fread(buf, 1, BUFSZ, old_input);
				fwrite(buf, 1, charsread, stdout);
			} while (charsread == BUFSZ);
			if (0 != fclose(old_input))
				err(EXIT_FAILURE, NULL);
		}
		/*
		 * set the offset back to zero and off we go looking at the
		 * current file
		 */
		/* reset offset and report everything */
		fgetpos(input, &offset_position);
	}
	/* if the current file inode is different than that in the */
	/* offset file then assume it has been rotated via mv and recreated, */
	/*
	 * find the orig file and output latest from it, then set offset to
	 * zero
	 */
	if (inode != logfile_stat.st_ino) {

		/*
		 * look in the current log file directory for the same inode
		 * number
		 */
		strcpy(old_logfn, "NoFileFound");
		dp = opendir(logdir);
		if (dp != NULL) {
			while ((ep = readdir(dp)))
				if (strcmp(ep->d_name, ".") != 0 && strcmp(ep->d_name, "..") != 0) {
					/*
					 * put the directory and old filename
					 * back together
					 */
					strcpy(tmpfn, logdir);
					strcat(tmpfn, "/");
					strcat(tmpfn, ep->d_name);
					if ((stat(tmpfn, &logfile_stat_old)) != 0) {	/* load struct */
						fprintf(stderr, "ERROR 557 - Cannot get file %s details.\n", tmpfn);
						exit(EXIT_FAILURE);
					}
					if (inode == logfile_stat_old.st_ino) {
						strcpy(old_logfn, ep->d_name);
					}
				}
			(void)closedir(dp);
		}
		else {
			fprintf(stderr, "ERROR 570 - Couldn't open the directory: %s", logdir);
			exit(EXIT_FAILURE);
		}

		/*
		 * If we found the old file,
		 * print out any lines added
		 * after the offset we have on file.
		 */
		if (strcmp(old_logfn, "NoFileFound") != 0) {
			strcpy(tmpfn, logdir);
			strcat(tmpfn, "/");
			strcat(tmpfn, old_logfn);

			if ((old_input = fopen(tmpfn, "rb")) == NULL)
				err(EXIT_FAILURE, "%s", tmpfn, NULL);

			fsetpos(old_input, &offset_position);

			do {
				buf[0] = 0;
				charsread = fread(buf, 1, BUFSZ, old_input);
				fwrite(buf, 1, charsread, stdout);
			} while (charsread == BUFSZ);
			if (0 != fclose(old_input))
				err(EXIT_FAILURE, NULL);
		}
		/*
		 * set the offset back to zero and off we go looking at the
		 * current file
		 */
		fgetpos(input, &offset_position);
	}
	/* print out the current log stuff */
	fsetpos(input, &offset_position);

	/* Print the file */
	do {
		buf[0] = 0;
		charsread = fread(buf, 1, BUFSZ, input);
		fwrite(buf, 1, charsread, stdout);
	} while (charsread == BUFSZ);
	fgetpos(input, &offset_position);
	if (0 != fclose(input))
		err(EXIT_FAILURE, NULL);

	/* after we are done we need to write the new offset */
	if ((offset_output = fopen(offsetfn, "w")) == NULL)
		errx(EXIT_FAILURE, "File %s cannot be created. Check your permissions.\n", offsetfn);
	/* Don't let everyone read offset */
	if ((chmod(offsetfn, 00660)) != 0)
		errx(EXIT_FAILURE, "Cannot set permissions on file %s\n", offsetfn);
	fwrite(&logfile_stat.st_ino, sizeof(logfile_stat.st_ino), 1, offset_output);
	fwrite(&offset_position, sizeof(offset_position), 1, offset_output);
	if (1 != fwrite(&logfile_stat.st_size, sizeof(logfile_stat.st_size), 1, offset_output))
		errx(EXIT_FAILURE, "write failed");
	if (0 != fclose(offset_output))
		err(EXIT_FAILURE, NULL);
	free(buf);
	return (0);		/* everything A-OK */
}





/* ------------------------------------------------------------------ */
/* retail.c -- ASCII file tail program that remembers last position. */
/* */
/* Author:                                                           */
/* Craig H. Rowland <crowland@psionic.com> 15-JAN-96                 */
/* <crowland@vni.net>                               */
/* */
/* Please send me any hacks/bug fixes you make to the code. All      */
/* comments are welcome!                                             */
/* */
/* Idea for program based upon the retail utility featured in the    */
/* Gauntlet(tm) firewall protection package published by Trusted     */
/* Information Systems Inc. <info@tis.com>                           */
/* */
/* This program will read in a standard text file and create an      */
/* offset marker when it reads the end. The offset marker is read    */
/* the next time retail is run and the text file pointer is moved   */
/* to the offset location. This allows retail to read in the next   */
/* lines of data following the marker. This is good for marking log  */
/* files for automatic log file checkers to monitor system events.   */
/* */
/* This program covered by the GNU License. This program is free to  */
/* use as long as the above copyright notices are left intact. This  */
/* program has no warranty of any kind.                              */
/* */
/* To compile as normal:                                             */
/* gcc -o retail retail.c                                           */
/* */
/* To compile as large file aware:                                   */
/* gcc -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -o retail retail.c */
/* */
/* VERSION 1.1: Initial release                                      */
/* */
/* 1.11: Minor typo fix. Fixed NULL comparison.              */
/* 1.2 : Modified to be large file aware, jhb & rjm          */
/* ------------------------------------------------------------------ */

/*
 * dirname Copyright (c) 2003 Piotr Domagalski <szalik@szalik.net>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License Version 2.1 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

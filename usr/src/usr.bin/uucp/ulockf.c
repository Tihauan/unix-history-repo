#ifndef lint
static char sccsid[] = "@(#)ulockf.c	5.1 (Berkeley) 7/2/83";
#endif

#include "uucp.h"
#include <sys/types.h>
#include <sys/stat.h>


extern time_t	time();

/* File mode for lock files */
#define	LCKMODE	0444

/*******
 *	ulockf(file, atime)
 *	char *file;
 *	time_t atime;
 *
 *	ulockf  -  this routine will create a lock file (file).
 *	If one already exists, the create time is checked for
 *	older than the age time (atime).
 *	If it is older, an attempt will be made to unlink it
 *	and create a new one.
 *
 *	return codes:  0  |  FAIL
 */

ulockf(file, atime)
register char *file;
time_t atime;
{
	struct stat stbuf;
	time_t ptime;
	register int ret;
	static int pid = -1;
	static char tempfile[NAMESIZE];

	if (pid < 0) {
		pid = getpid();
		sprintf(tempfile, "LTMP.%d", pid);
	}
	if (onelock(pid, tempfile, file) == -1) {
		/* lock file exists */
		/* get status to check age of the lock file */
		ret = stat(file, &stbuf);
		if (ret != -1) {
			time(&ptime);
			if ((ptime - stbuf.st_ctime) < atime) {
				/* file not old enough to delete */
				return(FAIL);
			}
		}
		ret = unlink(file);
		ret = onelock(pid, tempfile, file);
		if (ret != 0)
			return(FAIL);
	}
	stlock(file);
	return(0);
}


#define MAXLOCKS 10	/* maximum number of lock files */
char *Lockfile[MAXLOCKS];
int Nlocks = 0;

/***
 *	stlock(name)	put name in list of lock files
 *	char *name;
 *
 *	return codes:  none
 */

stlock(name)
register char *name;
{
	register char *p;
	register int i;

	for (i = 0; i < Nlocks; i++) {
		if (Lockfile[i] == NULL)
			break;
	}
	ASSERT(i < MAXLOCKS, "TOO MANY LOCKS", "", i);
	if (i >= Nlocks)
		i = Nlocks++;
	p = calloc((unsigned)(strlen(name)+1), sizeof (char));
	ASSERT(p != NULL, "CAN NOT ALLOCATE FOR", name, 0);
	strcpy(p, name);
	Lockfile[i] = p;
	return;
}


/***
 *	rmlock(name)	remove all lock files in list
 *	char *name;	or name
 *
 *	return codes: none
 */

rmlock(name)
register char *name;
{
	register int i;

	for (i = 0; i < Nlocks; i++) {
		if (Lockfile[i] == NULL)
			continue;
		if (name == NULL
		|| strcmp(name, Lockfile[i]) == SAME) {
			unlink(Lockfile[i]);
			free(Lockfile[i]);
			Lockfile[i] = NULL;
		}
	}
	return;
}


/*
 *  this stuff from pjw 
 *  /usr/pjw/bin/recover - Check pids to remove unnecessary locks.
 *	isalock(name) returns 0 if the name is a lock.
 *	unlock(name)  unlocks name if it is a lock.
 *	onelock(pid,tempfile,name) makes lock a name
 *	on behalf of pid.  Tempfile must be in the same
 *	file system as name.
 *	lock(pid,tempfile,names) either locks all the
 *	names or none of them.
 */
isalock(name) char *name;
{
	struct stat xstat;
	if(stat(name,&xstat)<0) return(0);
	if(xstat.st_size!=sizeof(int)) return(0);
	return(1);
}
unlock(name) char *name;
{
	if(isalock(name)) return(unlink(name));
	else return(-1);
}
onelock(pid,tempfile,name) char *tempfile,*name;
{	register int fd;
	fd=creat(tempfile,LCKMODE);
	if(fd<0) return(-1);
	write(fd,(char *) &pid,sizeof(int));
	close(fd);
	if(link(tempfile,name)<0)
	{	unlink(tempfile);
		return(-1);
	}
	unlink(tempfile);
	return(0);
}
lock(pid,tempfile,names) char *tempfile;
register char **names;
{	register int i,j;
	for(i=0;names[i]!=0;i++)
	{	if(onelock(pid,tempfile,names[i])==0) continue;
		for(j=0;j<i;j++) unlink(names[j]);
		return(-1);
	}
	return(0);
}

#define LOCKPRE "LCK."

/***
 *	delock(s)	remove a lock file
 *	char *s;
 *
 *	return codes:  0  |  FAIL
 */

delock(s)
char *s;
{
	char ln[30];

	sprintf(ln, "%s.%s", LOCKPRE, s);
	rmlock(ln);
}


/***
 *	mlock(sys)	create system lock
 *	char *sys;
 *
 *	return codes:  0  |  FAIL
 */

mlock(sys)
char *sys;
{
	char lname[30];
	sprintf(lname, "%s.%s", LOCKPRE, sys);
	return(ulockf(lname, (time_t) SLCKTIME ) < 0 ? FAIL : 0);
}


/***
 *	ultouch()	update 'change' time for lock files
 *
 *	-- mod by rti!trt --
 *	Only update ctime, not mtime or atime.
 *	The 'chmod' method permits cu(I)-like programs
 *	to determine how long uucp has been on the line.
 *	The old "change access, mod, and change time" method
 *	can be had by defining OLDTOUCH
 *
 *	return code - none
 */

ultouch()
{
	time_t time();
	static time_t lasttouch = 0;
	register int i;
	struct ut {
		time_t actime;
		time_t modtime;
	} ut;

	ut.actime = time(&ut.modtime);
	/* Do not waste time touching locking files too often */
	/* (But, defend against backward time changes) */
	if (ut.actime >= lasttouch && ut.actime < lasttouch+60)
		return;
	lasttouch = ut.actime;
	DEBUG(4, "ultouch\n", 0);

	for (i = 0; i < Nlocks; i++) {
		if (Lockfile[i] == NULL)
			continue;
#ifdef	OLDTOUCH
		utime(Lockfile[i], &ut);
#else
		chmod(Lockfile[i], LCKMODE);
#endif
	}
}

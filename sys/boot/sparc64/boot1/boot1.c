/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/diskslice.h>
#include <sys/disklabel.h>
#include <sys/dirent.h>
#include <machine/elf.h>
#include <machine/stdarg.h>

#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>

#define _PATH_LOADER	"/boot/loader"
#define _PATH_KERNEL	"/boot/kernel/kernel"

#define BSIZEMAX	8192

/*
 * This structure will be refined along with the addition of a bootpath
 * parsing routine when it is necessary to cope with bootpaths that are
 * not in the exact <devpath>@<controller>,<disk>:<partition> format and
 * for which we need to evaluate the disklabel ourselves.
 */ 
struct disk {
	int meta;
};
struct disk dsk;

extern uint32_t _end;

static char bname[1024];	/* name of the binary to load */
static uint32_t fs_off;

int main(void);
void exit(int);
static void load(const char *);
static ino_t lookup(const char *);
static int xfsread(ino_t, void *, size_t);
static ssize_t fsread(ino_t, void *, size_t);
static int dskread(void *, u_int64_t, int);
static int printf(const char *, ...);
static int putchar(int);

static void *memcpy(void *, const void *, size_t);
static void *memset(void *, int, size_t);

/*
 * Open Firmware interface functions
 */
typedef u_int64_t	ofwcell_t;
typedef int32_t		ofwh_t;
typedef u_int32_t	u_ofwh_t;
typedef int (*ofwfp_t)(ofwcell_t []);
ofwfp_t ofw;			/* the prom Open Firmware entry */

void ofw_init(int, int, int, int, ofwfp_t);
ofwh_t ofw_finddevice(const char *);
ofwh_t ofw_open(const char *);
int ofw_getprop(ofwh_t, const char *, void *, size_t);
int ofw_read(ofwh_t, void *, size_t);
int ofw_write(ofwh_t, const void *, size_t);
int ofw_seek(ofwh_t, u_int64_t);

ofwh_t bootdevh;
ofwh_t stdinh, stdouth;
char bootpath[64];

/*
 * This has to stay here, as the PROM seems to ignore the
 * entry point specified in the a.out header.  (or elftoaout is broken)
 */

void
ofw_init(int d, int d1, int d2, int d3, ofwfp_t ofwaddr)
{
	ofwh_t chosenh;

	ofw = ofwaddr;

	chosenh = ofw_finddevice("/chosen");
	ofw_getprop(chosenh, "stdin", &stdinh, sizeof(stdinh));
	ofw_getprop(chosenh, "stdout", &stdouth, sizeof(stdouth));
	ofw_getprop(chosenh, "bootpath", bootpath, sizeof(bootpath));

	if ((bootdevh = ofw_open(bootpath)) == -1) {
		printf("Could not open boot device.\n");
	}	

	main();
	d = d1 = d2 = d3;	/* make GCC happy */
}

ofwh_t
ofw_finddevice(const char *name)
{
	ofwcell_t args[] = {
		(ofwcell_t)"finddevice",
		1,
		1,
		(ofwcell_t)name,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_finddevice: name=\"%s\"\n", name);
		return (1);
	}
	return (args[4]);
}

int
ofw_getprop(ofwh_t ofwh, const char *name, void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"getprop",
		4,
		1,
		(u_ofwh_t)ofwh,
		(ofwcell_t)name,
		(ofwcell_t)buf,
		len,
	0
	};

	if ((*ofw)(args)) {
		printf("ofw_getprop: ofwh=0x%x buf=%p len=%u\n",
			ofwh, buf, len);
		return (1);
	}
	return (0);
}

ofwh_t
ofw_open(const char *path)
{
	ofwcell_t args[] = {
		(ofwcell_t)"open",
		1,
		1,
		(ofwcell_t)path,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_open: path=\"%s\"\n", path);
		return (-1);
	}
	return (args[4]);
}

int
ofw_close(ofwh_t devh)
{
	ofwcell_t args[] = {
		(ofwcell_t)"close",
		1,
		0,
		(u_ofwh_t)devh
	};

	if ((*ofw)(args)) {
		printf("ofw_close: devh=0x%x\n", devh);
		return (1);
	}
	return (0);
}

int
ofw_read(ofwh_t devh, void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"read",
		4,
		1,
		(u_ofwh_t)devh,
		(ofwcell_t)buf,
		len,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_read: devh=0x%x buf=%p len=%u\n", devh, buf, len);
		return (1);
	}
	return (0);
}

int
ofw_write(ofwh_t devh, const void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"write",
		3,
		1,
		(u_ofwh_t)devh,
		(ofwcell_t)buf,
		len,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_write: devh=0x%x buf=%p len=%u\n", devh, buf, len);
		return (1);
	}
	return (0);
}

int
ofw_seek(ofwh_t devh, u_int64_t off)
{
	ofwcell_t args[] = {
		(ofwcell_t)"seek",
		4,
		1,
		(u_ofwh_t)devh,
		off >> 32,
		off,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_seek: devh=0x%x off=0x%lx\n", devh, off);
		return (1);
	}
	return (0);
}

static int
strcmp(const char *s1, const char *s2)
{
	for (; *s1 == *s2 && *s1; s1++, s2++)
		;
	return ((u_char)*s1 - (u_char)*s2);
}

static void *
memset(void *dst, int val, size_t len)
{
	void *ret;

	ret = dst;
	while (len) {
		*((char *)dst)++ = val;
		len--;
	}
	return (ret);
}

static int
fsfind(const char *name, ino_t * ino)
{
	char buf[DEV_BSIZE];
	struct dirent *d;
	char *s;
	ssize_t n;

	fs_off = 0;
	while ((n = fsread(*ino, buf, DEV_BSIZE)) > 0) {
		for (s = buf; s < buf + DEV_BSIZE;) {
			d = (void *)s;
			if (!strcmp(name, d->d_name)) {
				*ino = d->d_fileno;
				return (d->d_type);
			}
			s += d->d_reclen;
		}
	}
	return (0);
}

static void
putc(int c)
{
	char d;

	d = c;
	ofw_write(stdouth, &d, 1);
}

int
main(void)
{
	if (bname[0] == '\0')
		memcpy(bname, _PATH_LOADER, sizeof(_PATH_LOADER));

	printf(" \n>> FreeBSD/sparc64 boot block\n"
	"   Boot path:   %s\n"
	"   Boot loader: %s\n", bootpath, _PATH_LOADER);
	load(bname);
	return (1);
}

static void
load(const char *fname)
{
	Elf64_Ehdr eh;
	Elf64_Phdr ep[2];
	Elf64_Shdr es[2];
	caddr_t p;
	ino_t ino;
	vm_offset_t entry;
	int i, j;

	if ((ino = lookup(fname)) == 0) {
		printf("File %s not found\n", fname);
		return;
	}
	if (xfsread(ino, &eh, sizeof(eh)))
		return;
	if (!IS_ELF(eh)) {
		printf("Not an ELF file\n");
		return;
	}
	fs_off = eh.e_phoff;
	for (j = i = 0; i < eh.e_phnum && j < 2; i++) {
		if (xfsread(ino, ep + j, sizeof(ep[0])))
			return;
		if (ep[j].p_type == PT_LOAD)
			j++;
	}
	for (i = 0; i < j; i++) {
		p = (caddr_t)ep[i].p_vaddr;
		fs_off = ep[i].p_offset;
		if (xfsread(ino, p, ep[i].p_filesz))
			return;
		/*
		 * Assume the second program header table entry
		 * to contain data and bss.  Clear out the .bss section.
		 */
		if (i == 1) {
			memset(p + ep[i].p_filesz, 0,
			    ep[i].p_memsz - ep[i].p_filesz);
		}
	}
	p += roundup2(ep[1].p_memsz, PAGE_SIZE);
	if (eh.e_shnum == eh.e_shstrndx + 3) {
		fs_off = eh.e_shoff + sizeof(es[0]) * (eh.e_shstrndx + 1);
		if (xfsread(ino, &es, sizeof(es)))
			return;
		for (i = 0; i < 2; i++) {
			memcpy(p, &es[i].sh_size, sizeof(es[i].sh_size));
			p += sizeof(es[i].sh_size);
			fs_off = es[i].sh_offset;
			if (xfsread(ino, p, es[i].sh_size))
				return;
			p += es[i].sh_size;
		}
	}
	entry = eh.e_entry;
	ofw_close(bootdevh);
	(*(void (*)(int, int, int, int, ofwfp_t))entry)(0, 0, 0, 0, ofw);
}

static ino_t
lookup(const char *path)
{
	char name[MAXNAMLEN + 1];
	const char *s;
	ino_t ino;
	ssize_t n;
	int dt;

	ino = ROOTINO;
	dt = DT_DIR;
	name[0] = '/';
	name[1] = '\0';
	for (;;) {
		if (*path == '/')
			path++;
		if (!*path)
			break;
		for (s = path; *s && *s != '/'; s++)
			;
		if ((n = s - path) > MAXNAMLEN)
			return (0);
		memcpy(name, path, n);
		name[n] = 0;
		if (dt != DT_DIR) {
			printf("%s: not a directory.\n", name);
			return (0);
		}
		if ((dt = fsfind(name, &ino)) <= 0)
			break;
		path = s;
	}
	return (dt == DT_REG ? ino : 0);
}

static int
xfsread(ino_t inode, void *buf, size_t nbyte)
{
	if (fsread(inode, buf, nbyte) != (ssize_t)nbyte) {
		printf("Invalid %s\n", "format");
		return (-1);
	}
	return (0);
}

static ssize_t
fsread(ino_t inode, void *buf, size_t nbyte)
{
	static struct fs fs;
	static struct dinode din;
	static char blkbuf[BSIZEMAX];
	static ufs_daddr_t indbuf[BSIZEMAX / sizeof(ufs_daddr_t)];
	static ino_t inomap;
	static ufs_daddr_t blkmap, indmap;
	static unsigned int fsblks;
	char *s;
	ufs_daddr_t lbn, addr;
	size_t n, nb, off;

	if (!dsk.meta) {
		inomap = 0;
		if (dskread(blkbuf, SBOFF / DEV_BSIZE, SBSIZE / DEV_BSIZE))
			return (-1);
		memcpy(&fs, blkbuf, sizeof(fs));
		if (fs.fs_magic != FS_MAGIC) {
			printf("Not ufs\n");
			return (-1);
		}
		fsblks = fs.fs_bsize >> DEV_BSHIFT;
		dsk.meta++;
	}
	if (!inode)
		return (0);
	if (inomap != inode) {
		if (dskread(blkbuf, fsbtodb(&fs, ino_to_fsba(&fs, inode)),
		    fsblks))
			return (-1);
		din = ((struct dinode *)blkbuf)[inode % INOPB(&fs)];
		inomap = inode;
		fs_off = 0;
		blkmap = indmap = 0;
	}
	s = buf;
	if (nbyte > (n = din.di_size - fs_off))
		nbyte = n;
	nb = nbyte;
	while (nb) {
		lbn = lblkno(&fs, fs_off);
		if (lbn < NDADDR)
			addr = din.di_db[lbn];
		else {
			if (indmap != din.di_ib[0]) {
				if (dskread(indbuf, fsbtodb(&fs, din.di_ib[0]),
				    fsblks))
					return (-1);
				indmap = din.di_ib[0];
			}
			addr = indbuf[(lbn - NDADDR) % NINDIR(&fs)];
		}
		n = dblksize(&fs, &din, lbn);
		if (blkmap != addr) {
			if (dskread(blkbuf, fsbtodb(&fs, addr),
			    n >> DEV_BSHIFT)) {
				return (-1);
			}
			blkmap = addr;
		}
		off = blkoff(&fs, fs_off);
		n -= off;
		if (n > nb)
			n = nb;
		memcpy(s, blkbuf + off, n);
		s += n;
		fs_off += n;
		nb -= n;
	}
	return (nbyte);
}

static int
dskread(void *buf, u_int64_t lba, int nblk)
{
	/*
	 * The OpenFirmware should open the correct partition for us.
	 * That means, if we read from offset zero on an open instance handle,
	 * we should read from offset zero of that partition.
	 */
	ofw_seek(bootdevh, lba * DEV_BSIZE);
	ofw_read(bootdevh, buf, nblk * DEV_BSIZE);
	return (0);
}

static int
printf(const char *fmt,...)
{
	static const char digits[16] = "0123456789abcdef";
	va_list ap;
	char buf[10];
	char *s;
	unsigned long int r, u;
	int c, longp;

	va_start(ap, fmt);
	longp = 0;
	while ((c = *fmt++)) {
		if (c == '%' || longp) {
			if (c == '%')
				c = *fmt++;
			switch (c) {
			case 'c':
				if (longp)
					break;
				putchar(va_arg(ap, int));
				continue;
			case 's':
				if (longp)
					break;
				for (s = va_arg(ap, char *); *s; s++)
					putchar(*s);
				continue;
			case 'p':
				if (longp)
					break;
				if (c == 'p') {
					putchar('0');
					putchar('x');
				}
			case 'u':
			case 'x':
				r = c == 'u' ? 10U : 16U;
				u = (c == 'p' || longp) ?
				    va_arg(ap, unsigned long) :
				    va_arg(ap, unsigned int);
				s = buf;
				do
					*s++ = digits[u % r];
				while (u /= r);
				while (--s >= buf)
					putchar(*s);
				longp = 0;
				continue;
			case 'l':
				if (longp)
					break;
				longp = 1;
				continue;
			}
			longp = 0;
		}
		putchar(c);
	}
	va_end(ap);
	return (0);
}

static int
putchar(int c)
{
	if (c == '\n')
		putc('\r');
	putc(c);
	return (c);
}

static void *
memcpy(void *dst, const void *src, size_t size)
{
	const char *s;
	char *d;

	for (d = dst, s = src; size; size--)
		*d++ = *s++;
	return (dst);
}

/* vi: set sw=4 ts=4: */
/*
 * chattr.c		- Change file attributes on an ext2 file system
 *
 * Copyright (C) 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                           Laboratoire MASI, Institut Blaise Pascal
 *                           Universite Pierre et Marie Curie (Paris VI)
 *
 * This file can be redistributed under the terms of the GNU General
 * Public License
 */
//config:config CHATTR
//config:	bool "chattr (3.8 kb)"
//config:	default y
//config:	help
//config:	chattr changes the file attributes on a second extended file system.

//applet:IF_CHATTR(APPLET_NOEXEC(chattr, chattr, BB_DIR_BIN, BB_SUID_DROP, chattr))

//kbuild:lib-$(CONFIG_CHATTR) += chattr.o e2fs_lib.o

//usage:#define chattr_trivial_usage
//usage:       "[-R] [-v VERSION] [-p PROJID] [-+=AacDdijsStTu] FILE..."
//usage:#define chattr_full_usage "\n\n"
//usage:       "Change ext2 file attributes\n"
//usage:     "\n	-R	Recurse"
//usage:     "\n	-v NUM	Set version/generation number"
//usage:     "\n	-p NUM	Set project number"
//-V, -f accepted but ignored
//usage:     "\nModifiers:"
//usage:     "\n	-,+,=	Remove/add/set attributes"
//usage:     "\nAttributes:"
//usage:     "\n	A	No atime"
//usage:     "\n	a	Append only"
//usage:     "\n	c	Compressed"
//usage:     "\n	D	Synchronous dir updates"
//usage:     "\n	d	Don't backup with dump"
//usage:     "\n	i	Immutable"
//usage:     "\n	j	Write data to journal first"
//usage:     "\n	s	Zero storage when deleted"
//usage:     "\n	S	Synchronous file updates"
//usage:     "\n	t	Don't tail-merge with other files"
//usage:     "\n	u	Allow undelete"

#include "libbb.h"
#include "e2fs_lib.h"

#define OPT_ADD      (1 << 0)
#define OPT_REM      (1 << 1)
#define OPT_SET      (1 << 2)
#define OPT_SET_VER  (1 << 3)
#define OPT_SET_PROJ (1 << 4)

struct globals {
	unsigned long version;
	unsigned long af;
	unsigned long rf;
	int flags;
	uint32_t projid;
	smallint recursive;
};

static unsigned long get_flag(char c)
{
	const char *fp = strchr(e2attr_flags_sname_chattr, c);
	if (fp)
		return e2attr_flags_value_chattr[fp - e2attr_flags_sname_chattr];
	bb_show_usage();
}

static char** decode_arg(char **argv, struct globals *gp)
{
	unsigned long *fl;
	const char *arg = *argv;
	char opt = *arg;

	fl = &gp->af;
	if (opt == '-') {
		gp->flags |= OPT_REM;
		fl = &gp->rf;
	} else if (opt == '+') {
		gp->flags |= OPT_ADD;
	} else { /* if (opt == '=') */
		gp->flags |= OPT_SET;
	}

	while (*++arg) {
		if (opt == '-') {
//e2fsprogs-1.43.1 accepts:
// "-RRR", "-RRRv VER" and even "-ARRRva VER" and "-vvv V1 V2 V3"
// but not "-vVER".
// IOW: options are parsed as part of "remove attrs" strings,
// if "v" is seen, next argv[] is VER, even if more opts/attrs follow in this argv[]!
			if (*arg == 'R') {
				gp->recursive = 1;
				continue;
			}
			if (*arg == 'V') {
				/*"verbose and print program version" (nop for now) */;
				continue;
			}
			if (*arg == 'f') {
				/*"suppress most error messages" (nop) */;
				continue;
			}
			if (*arg == 'v') {
				if (!*++argv)
					bb_show_usage();
				gp->version = xatoul(*argv);
				gp->flags |= OPT_SET_VER;
				continue;
			}
			if (*arg == 'p') {
				if (!*++argv)
					bb_show_usage();
				gp->projid = xatou32(*argv);
				gp->flags |= OPT_SET_PROJ;
				continue;
			}
			/* not a known option, try as an attribute */
		}
		*fl |= get_flag(*arg);
	}

	return argv;
}

static void change_attributes(const char *name, struct globals *gp);

static int FAST_FUNC chattr_dir_proc(const char *dir_name, struct dirent *de, void *gp)
{
	char *path = concat_subpath_file(dir_name, de->d_name);
	/* path is NULL if de->d_name is "." or "..", else... */
	if (path) {
		change_attributes(path, gp);
		free(path);
	}
	return 0;
}

static void change_attributes(const char *name, struct globals *gp)
{
	unsigned long fsflags;
	struct stat st;

	if (lstat(name, &st) != 0) {
		bb_perror_msg("stat %s", name);
		return;
	}
	if (S_ISLNK(st.st_mode) && gp->recursive)
		return;

	/* Don't try to open device files, fifos etc.  We probably
	 * ought to display an error if the file was explicitly given
	 * on the command line (whether or not recursive was
	 * requested).  */
	if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) && !S_ISDIR(st.st_mode))
		return;

	if (gp->flags & OPT_SET_VER)
		if (fsetversion(name, gp->version) != 0)
			bb_perror_msg("setting %s on %s", "version", name);

	if (gp->flags & OPT_SET_PROJ)
		if (fsetprojid(name, gp->projid) != 0)
			bb_perror_msg("setting %s on %s", "project ID", name);

	if (gp->flags & OPT_SET) {
		fsflags = gp->af;
	} else {
		if (fgetflags(name, &fsflags) != 0) {
			bb_perror_msg("reading flags on %s", name);
			goto skip_setflags;
		}
		/*if (gp->flags & OPT_REM) - not needed, rf is zero otherwise */
			fsflags &= ~gp->rf;
		/*if (gp->flags & OPT_ADD) - not needed, af is zero otherwise */
			fsflags |= gp->af;
// What is this? And why it's not done for SET case?
		if (!S_ISDIR(st.st_mode))
			fsflags &= ~EXT2_DIRSYNC_FL;
	}
	if (fsetflags(name, fsflags) != 0)
		bb_perror_msg("setting flags on %s", name);

 skip_setflags:
	if (gp->recursive && S_ISDIR(st.st_mode))
		iterate_on_dir(name, chattr_dir_proc, gp);
}

int chattr_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chattr_main(int argc UNUSED_PARAM, char **argv)
{
	struct globals g;

	memset(&g, 0, sizeof(g));

	/* parse the args */
	for (;;) {
		char *arg = *++argv;
		if (!arg)
			bb_show_usage();
		if (arg[0] != '-' && arg[0] != '+' && arg[0] != '=')
			break;

		argv = decode_arg(argv, &g);
	}
	/* note: on loop exit, remaining argv[] is never empty */

	/* run sanity checks on all the arguments given us */
	if ((g.flags & OPT_SET) && (g.flags & (OPT_ADD|OPT_REM)))
		bb_simple_error_msg_and_die("= is incompatible with - and +");
	if (g.rf & g.af)
		bb_simple_error_msg_and_die("can't set and unset a flag");
	if (!g.flags)
		bb_simple_error_msg_and_die("must use '-v', =, - or +");

	/* now run chattr on all the files passed to us */
	do change_attributes(*argv, &g); while (*++argv);

	return EXIT_SUCCESS;
}

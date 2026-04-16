/*
 * Copyright (C) 2002-2003  David McCullough <davidm@snapgear.com>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *                          The Silver Hammer Group, Ltd.
 *
 * This file provides the definitions and structures needed to
 * support uClinux flat-format executables.
 */

#ifndef _FROSTED_FLAT_H
#define _FROSTED_FLAT_H

#ifdef __KERNEL__
#include <asm/flat.h>
#endif

#define	FLAT_VERSION			0x00000004L
#define	OLD_FLAT_VERSION		0x00000002L

#ifdef CONFIG_BINFMT_SHARED_FLAT
#define	MAX_SHARED_LIBS			(4)
#else
#define	MAX_SHARED_LIBS			(1)
#endif

/*
 * To make everything easier to port and manage cross platform
 * development,  all fields are in network byte order.
 */

struct flat_hdr {
	char magic[4];
	unsigned long rev;          /* version (as above) */
	unsigned long entry;        /* Offset of first executable instruction
	                               with text segment from beginning of file */
	unsigned long data_start;   /* Offset of data segment from beginning of
	                               file */
	unsigned long data_end;     /* Offset of end of data segment
	                               from beginning of file */
	unsigned long bss_end;      /* Offset of end of bss segment from beginning
	                               of file */

	/* (It is assumed that data_end through bss_end forms the bss segment.) */

	unsigned long stack_size;   /* Size of stack, in bytes */
	unsigned long reloc_start;  /* Offset of relocation records from
	                               beginning of file */
	unsigned long reloc_count;  /* Number of relocation records */
	unsigned long flags;
	unsigned long build_date;   /* When the program/library was built */
	unsigned long filler[5];    /* Reservered, set to zero */
};

#define FLAT_FLAG_RAM    0x0001 /* load program entirely into RAM */
#define FLAT_FLAG_GOTPIC 0x0002 /* program is PIC with GOT */
#define FLAT_FLAG_GZIP   0x0004 /* all but the header is compressed */
#define FLAT_FLAG_GZDATA 0x0008 /* only data/relocs are compressed (for XIP) */
#define FLAT_FLAG_KTRACE 0x0010 /* output useful kernel trace for debugging */
#define FLAT_FLAG_SHLIB  0x0040 /* shared library with export table */

/*
 * Shared library extensions (FLAT_FLAG_SHLIB):
 *
 * filler[0] = export table offset from file start (big-endian)
 * filler[1] = export count (big-endian)
 * filler[2] = library ID, 1-255 (big-endian)
 *
 * The export table is stored after the relocation records:
 *
 *   struct shlib_export_hdr {
 *       uint32_t version;      // API version (big-endian)
 *       uint32_t count;        // number of exports (big-endian)
 *       uint32_t offsets[];    // .text-relative offsets (big-endian)
 *   };
 *
 * Applications reference library functions via GOT entries tagged
 * with (lib_id << 24) | ordinal.  The kernel resolves these at load
 * time using the export table.
 */
#define FLAT_SHLIB_EXPORT_OFF  0  /* filler[] index for export table offset */
#define FLAT_SHLIB_EXPORT_CNT  1  /* filler[] index for export count */
#define FLAT_SHLIB_LIB_ID      2  /* filler[] index for library ID */

#endif /* _FROSTED_FLAT_H */

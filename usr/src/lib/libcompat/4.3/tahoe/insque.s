/*
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Computer Consoles Inc.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#if defined(LIBC_SCCS) && !defined(lint)
_sccsid:.asciz	"@(#)insque.s	1.2 (Berkeley) %G%"
#endif /* LIBC_SCCS and not lint */

/* insque(new, pred) */

#include "DEFS.h"

ENTRY(insque, 0)
	insque	*4(fp), *8(fp)
	ret

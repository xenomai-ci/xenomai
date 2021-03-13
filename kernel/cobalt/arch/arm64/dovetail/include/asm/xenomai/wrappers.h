/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2005 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_ARM64_ASM_WRAPPERS_H
#define _COBALT_ARM64_ASM_WRAPPERS_H

#include <asm-generic/xenomai/wrappers.h> /* Read the generic portion. */

#define __put_user_inatomic __put_user
#define __get_user_inatomic __get_user

#endif /* _COBALT_ARM64_ASM_WRAPPERS_H */

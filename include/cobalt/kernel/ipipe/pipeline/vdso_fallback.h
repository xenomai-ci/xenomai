/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) Siemens AG, 2021
 */

#ifndef _COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H
#define _COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H

static __always_inline bool 
pipeline_handle_vdso_fallback(int nr, struct pt_regs *regs)
{
	return false;
}

#endif /* !_COBALT_KERNEL_PIPELINE_VDSO_FALLBACK_H */

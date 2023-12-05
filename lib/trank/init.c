/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <xenomai/init.h>
#include "internal.h"

/**
 * @defgroup trank Transition Kit
 *
 * A set of wrappers and services easing the transition from Xenomai
 * 2.x to 3.x.
 *
 * This interface provides a source compatibility layer for building
 * applications based on the Xenomai 2.x @a posix and @a native APIs
 * over Xenomai 3.x.
 */

static struct setup_descriptor trank_interface = {
	.name = "trank",
	.init = trank_init_interface,
};

post_setup_call(trank_interface);

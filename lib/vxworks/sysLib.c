/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

#include <vxworks/errnoLib.h>
#include <vxworks/sysLib.h>

#include "tickLib.h"

int sysClkRateGet(void)
{
	unsigned int resolution;
	struct service svc;

	CANCEL_DEFER(svc);
	resolution = clockobj_get_resolution(&wind_clock);
	CANCEL_RESTORE(svc);

	return 1000000000 / resolution;
}

STATUS sysClkRateSet(int hz)
{
	struct service svc;
	int ret;

	/*
	 * This is BSP level stuff, so we don't set errno upon error,
	 * but only return the ERROR status.
	 */
	if (hz <= 0)
		return ERROR;

	CANCEL_DEFER(svc);
	ret = clockobj_set_resolution(&wind_clock, 1000000000 / hz);
	CANCEL_RESTORE(svc);

	return ret ? ERROR : OK;
}

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

#include <stdio.h>
#include <string.h>
#include <vxworks/errnoLib.h>

#include "taskLib.h"

void printErrno(int status)
{
	const char *msg;
	char buf[64];

	switch (status) {
	case S_objLib_OBJ_ID_ERROR:
		msg = "S_objLib_OBJ_ID_ERROR";
		break;
	case S_objLib_OBJ_UNAVAILABLE:
		msg = "S_objLib_OBJ_UNAVAILABLE";
		break;
	case S_objLib_OBJ_DELETED:
		msg = "S_objLib_OBJ_DELETED";
		break;
	case S_objLib_OBJ_TIMEOUT:
		msg = "S_objLib_OBJ_TIMEOUT";
		break;
	case S_taskLib_NAME_NOT_FOUND:
		msg = "S_taskLib_NAME_NOT_FOUND";
		break;
	case S_taskLib_TASK_HOOK_NOT_FOUND:
		msg = "S_taskLib_TASK_HOOK_NOT_FOUND";
		break;
	case S_taskLib_ILLEGAL_PRIORITY:
		msg = "S_taskLib_ILLEGAL_PRIORITY";
		break;
	case S_taskLib_TASK_HOOK_TABLE_FULL:
		msg = "S_taskLib_TASK_HOOK_TABLE_FULL";
		break;
	case S_semLib_INVALID_STATE:
		msg = "S_semLib_INVALID_STATE";
		break;
	case S_semLib_INVALID_OPTION:
		msg = "S_semLib_INVALID_OPTION";
		break;
	case S_semLib_INVALID_QUEUE_TYPE:
		msg = "S_semLib_INVALID_QUEUE_TYPE";
		break;
	case S_semLib_INVALID_OPERATION:
		msg = "S_semLib_INVALID_OPERATION";
		break;
	case S_msgQLib_INVALID_MSG_LENGTH:
		msg = "S_msgQLib_INVALID_MSG_LENGTH";
		break;
	case S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL:
		msg = "S_msgQLib_NON_ZERO_TIMEOUT_AT_INT_LEVEL";
		break;
	case S_msgQLib_INVALID_QUEUE_TYPE:
		msg = "S_msgQLib_INVALID_QUEUE_TYPE";
		break;
	case S_intLib_NOT_ISR_CALLABLE:
		msg = "S_intLib_NOT_ISR_CALLABLE";
		break;
	case S_memLib_NOT_ENOUGH_MEMORY:
		msg = "S_memLib_NOT_ENOUGH_MEMORY";
		break;
	default:
		if (strerror_r(status, buf, sizeof(buf)))
			msg = "Unknown error";
		else
			msg = buf;
	}

	fprintf(stderr, "Error code %d: %s\n", status, msg);
}

STATUS errnoOfTaskSet(TASK_ID task_id, int status)
{
	struct wind_task *task;
	struct service svc;
	STATUS ret = OK;

	CANCEL_DEFER(svc);

	task = get_wind_task_or_self(task_id);
	if (task == NULL) {
		ret = ERROR;
		goto out;
	}

	*task->thobj.errno_pointer = status;
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

STATUS errnoOfTaskGet(TASK_ID task_id)
{
	struct wind_task *task;
	struct service svc;
	STATUS status = OK;

	CANCEL_DEFER(svc);

	task = get_wind_task_or_self(task_id);
	if (task == NULL) {
		status = ERROR;
		goto out;
	}

	status = *task->thobj.errno_pointer;
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);

	return status;
}

STATUS errnoSet(int status)
{
	errno = status;
	return OK;
}

int errnoGet(void)
{
	return errno;
}

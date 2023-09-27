/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <pthread.h>
#include <memory.h>

COBALT_IMPL(int, pthread_attr_init, (pthread_attr_t *attr))
{
	__STD(pthread_attr_init)(attr);
	return pthread_attr_setstacksize(attr, PTHREAD_STACK_DEFAULT);
}

int pthread_attr_init_ex(pthread_attr_ex_t *attr_ex)
{
	struct sched_param param;
	int policy;

	/* Start with defaulting all fields to null. */
	memset(attr_ex, 0, sizeof(*attr_ex));
	/* Merge in the default standard attribute set. */
	__COBALT(pthread_attr_init)(&attr_ex->std);
	pthread_attr_getschedpolicy(&attr_ex->std, &policy);
	attr_ex->nonstd.sched_policy = policy;
	pthread_attr_getschedparam(&attr_ex->std, &param);
	attr_ex->nonstd.sched_param.sched_priority = param.sched_priority;

	return 0;
}

int pthread_attr_destroy_ex(pthread_attr_ex_t *attr_ex)
{
	return pthread_attr_destroy(&attr_ex->std);
}

int pthread_attr_setschedpolicy_ex(pthread_attr_ex_t *attr_ex,
				   int policy)
{
	attr_ex->nonstd.sched_policy = policy;

	return 0;
}

int pthread_attr_getschedpolicy_ex(const pthread_attr_ex_t *attr_ex,
				   int *policy)
{
	*policy = attr_ex->nonstd.sched_policy;

	return 0;
}

int pthread_attr_setschedparam_ex(pthread_attr_ex_t *attr_ex,
				  const struct sched_param_ex *param_ex)
{
	attr_ex->nonstd.sched_param = *param_ex;

	return 0;
}

int pthread_attr_getschedparam_ex(const pthread_attr_ex_t *attr_ex,
				  struct sched_param_ex *param_ex)
{
	*param_ex = attr_ex->nonstd.sched_param;

	return 0;
}

int pthread_attr_setinheritsched_ex(pthread_attr_ex_t *attr_ex,
				    int inheritsched)
{
	return pthread_attr_setinheritsched(&attr_ex->std, inheritsched);
}

int pthread_attr_getinheritsched_ex(const pthread_attr_ex_t *attr_ex,
				    int *inheritsched)
{
	return pthread_attr_getinheritsched(&attr_ex->std, inheritsched);
}

int pthread_attr_getdetachstate_ex(const pthread_attr_ex_t *attr_ex,
				   int *detachstate)
{
	return pthread_attr_getdetachstate(&attr_ex->std, detachstate);
}

int pthread_attr_setdetachstate_ex(pthread_attr_ex_t *attr_ex,
				   int detachstate)
{
	return pthread_attr_setdetachstate(&attr_ex->std, detachstate);
}

int pthread_attr_getstacksize_ex(const pthread_attr_ex_t *attr_ex,
				 size_t *stacksize)
{
	return pthread_attr_getstacksize(&attr_ex->std, stacksize);
}

int pthread_attr_setstacksize_ex(pthread_attr_ex_t *attr_ex,
				 size_t stacksize)
{
	return pthread_attr_setstacksize(&attr_ex->std, stacksize);
}

int pthread_attr_getscope_ex(const pthread_attr_ex_t *attr_ex,
			     int *scope)
{
	return pthread_attr_getscope(&attr_ex->std, scope);
}

int pthread_attr_setscope_ex(pthread_attr_ex_t *attr_ex,
			     int scope)
{
	return pthread_attr_setscope(&attr_ex->std, scope);
}

int pthread_attr_getpersonality_ex(const pthread_attr_ex_t *attr_ex,
				   int *personality)
{
	*personality = attr_ex->nonstd.personality;

	return 0;
}

int pthread_attr_setpersonality_ex(pthread_attr_ex_t *attr_ex,
				   int personality)
{
	attr_ex->nonstd.personality = personality;

	return 0;
}

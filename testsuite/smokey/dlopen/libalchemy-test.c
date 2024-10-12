/*
 * Copyright (C) Siemens AG, 2018
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <alchemy/queue.h>
#include <string.h>

#include <xenomai/init.h>
#include <xenomai/tunables.h>

static int ran_init = 0;
static size_t def_mem_pool_size = SIZE_MAX;

static int alchemy_tune(void)
{
	if (ran_init)
		return 0;
	def_mem_pool_size = get_config_tunable(mem_pool_size);
	set_config_tunable(mem_pool_size, 2*def_mem_pool_size);
	ran_init = 1;
	return 0;
}

static struct setup_descriptor alchemy_setup = {
	.name = "setup-name",
	.tune = alchemy_tune,
};

user_setup_call(alchemy_setup);

int libalchemy_func(void);

int libalchemy_func(void)
{
	RT_QUEUE queue;
	int ret;

	ret = rt_queue_create(&queue, "q0", def_mem_pool_size,
			      Q_UNLIMITED, Q_FIFO);
	if (ret)
		return ret;
	ret = rt_queue_delete(&queue);

	return ret;
}

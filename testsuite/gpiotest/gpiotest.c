/*
 * Copyright (C) 2016 Philippe Gerum <rpm@xenomai.org>
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
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <smokey/smokey.h>
#include <rtdm/gpio.h>
#include <boilerplate/time.h>

smokey_test_plugin(interrupt,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
			   SMOKEY_STRING(trigger),
			   SMOKEY_BOOL(select),
			   SMOKEY_BOOL(timestamp),
		   ),
   "Wait for interrupts from a GPIO pin.\n"
   "\tdevice=<device-path>\n"
   "\trigger={edge[-rising/falling/both], level[-low/high]}\n"
   "\tselect, wait on select(2)."
   "\ttimestamp, enable timestamping."
);

smokey_test_plugin(read_value,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
		   ),
   "Read GPIO value.\n"
   "\tdevice=<device-path>."
);

smokey_test_plugin(write_value,
		   SMOKEY_ARGLIST(
			   SMOKEY_STRING(device),
		   ),
   "Write GPIO value.\n"
   "\tdevice=<device-path>."
);

static int run_interrupt(struct smokey_test *t, int argc, char *const argv[])
{
	static struct {
		const char *name;
		int flag;
	} trigger_types[] = {
		{ .name = "edge", .flag = GPIO_TRIGGER_EDGE_RISING },
		{ .name = "edge-rising", .flag = GPIO_TRIGGER_EDGE_RISING },
		{ .name = "edge-falling", .flag = GPIO_TRIGGER_EDGE_FALLING },
		{ .name = "edge-both", .flag = GPIO_TRIGGER_EDGE_FALLING|GPIO_TRIGGER_EDGE_RISING },
		{ .name = "level", .flag = GPIO_TRIGGER_LEVEL_LOW },
		{ .name = "level-low", .flag = GPIO_TRIGGER_LEVEL_LOW },
		{ .name = "level-high", .flag = GPIO_TRIGGER_LEVEL_HIGH },
		{ NULL, 0 },
	};
	int do_select = 0, fd, ret, trigger, n, value, do_timestamp = 0;
	const char *device = NULL, *trigname;
	struct rtdm_gpio_readout rdo;
	struct timespec now;
	fd_set set;
	
	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(interrupt, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(interrupt, device);
	fd = open(device, O_RDWR);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	if (SMOKEY_ARG_ISSET(interrupt, select))
		do_select = SMOKEY_ARG_BOOL(interrupt, select);

	if (SMOKEY_ARG_ISSET(interrupt, timestamp))
		do_timestamp = SMOKEY_ARG_BOOL(interrupt, timestamp);

	trigger = GPIO_TRIGGER_NONE;
	if (SMOKEY_ARG_ISSET(interrupt, trigger)) {
		trigname = SMOKEY_ARG_STRING(interrupt, trigger);
		for (n = 0; trigger_types[n].name; n++) {
			if (strcmp(trigger_types[n].name, trigname) == 0) {
				trigger = trigger_types[n].flag;
				break;
		    }
		}
		if (trigger == GPIO_TRIGGER_NONE) {
			warning("invalid trigger type %s", trigname);
			return -EINVAL;
		}
	}

	ret = ioctl(fd, GPIO_RTIOC_IRQEN, &trigger);
	if (ret) {
		ret = -errno;
		warning("GPIO_RTIOC_IRQEN failed on %s [%s]",
			device, symerror(ret));
		return ret;
	}

	FD_ZERO(&set);
	FD_SET(fd, &set);
	
	for (;;) {
		if (do_select) {
			ret = select(fd + 1, &set, NULL, NULL, NULL);
			if (ret < 0) {
				ret = -errno;
				warning("failed listening to %s [%s]",
					device, symerror(ret));
				return ret;
			}
		}
		if (do_timestamp) {
			ret = read(fd, &rdo, sizeof(rdo));
			if (ret < 0) {
				ret = -errno;
				warning("failed reading from %s [%s]",
					device, symerror(ret));
				return ret;
			}
			clock_gettime(CLOCK_MONOTONIC, &now);
			printf("received irq %llu us from now, GPIO state=%d\n",
			       (timespec_scalar(&now) - rdo.timestamp) / 1000ULL,
			       rdo.value);
		} else {
			ret = read(fd, &value, sizeof(value));
			if (ret < 0) {
				ret = -errno;
				warning("failed reading from %s [%s]",
					device, symerror(ret));
				return ret;
			}
			printf("received irq, GPIO state=%d\n", value);
		}
	}

	close(fd);

	return 0;
}

static int run_read_value(struct smokey_test *t, int argc, char *const argv[])
{
	const char *device = NULL;
	int fd, ret, value = -1;

	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(read_value, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(read_value, device);
	fd = open(device, O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	if (!__Terrno(ret, ioctl(fd, GPIO_RTIOC_DIR_IN)))
		return ret;

	ret = read(fd, &value, sizeof(value));
	close(fd);

	if (!__Tassert(ret == sizeof(value)))
		return -EINVAL;

	smokey_trace("value=%d", value);

	return 0;
}

static int run_write_value(struct smokey_test *t, int argc, char *const argv[])
{
	const char *device = NULL;
	int fd, ret, value;
	
	smokey_parse_args(t, argc, argv);

	if (!SMOKEY_ARG_ISSET(write_value, device)) {
		warning("missing device= specification");
		return -EINVAL;
	}

	device = SMOKEY_ARG_STRING(write_value, device);
	fd = open(device, O_WRONLY);
	if (fd < 0) {
		ret = -errno;
		warning("cannot open device %s [%s]",
			device, symerror(ret));
		return ret;
	}

	value = 1;
	if (!__Terrno(ret, ioctl(fd, GPIO_RTIOC_DIR_OUT, &value)))
		return ret;
	
	ret = write(fd, &value, sizeof(value));
	close(fd);

	if (!__Tassert(ret == sizeof(value)))
		return -EINVAL;

	return 0;
}

int main(int argc, char *const argv[])
{
	struct smokey_test *t;
	int ret, fails = 0;

	if (pvlist_empty(&smokey_test_list))
		return 0;

	for_each_smokey_test(t) {
		ret = t->run(t, argc, argv);
		if (ret) {
			if (ret == -ENOSYS) {
				smokey_note("%s skipped (no kernel support)",
					    t->name);
				continue;
			}
			fails++;
			if (smokey_keep_going)
				continue;
			if (smokey_verbose_mode)
				error(1, -ret, "test %s failed", t->name);
			return 1;
		}
		smokey_note("%s OK", t->name);
	}

	return fails != 0;
}

#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <rtdm/gpiopwm.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <time.h>

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/netdevice.h>

#define MIN_DUTY_CYCLE				(0)
#define MAX_DUTY_CYCLE				(100)

typedef void *(*gpiopwm_control_thread)(void *cookie);
#define DEVICE_NAME "/dev/rtdm/gpiopwm"
char *device_name;
int dev;

static sem_t synch;
static sem_t setup;
static int stop;
static int step = 1;
static int port = 66666;

#define MAX_IP_INTERFACES 			(9)
static char *ip_str[MAX_IP_INTERFACES + 1];
static int last_ip;


#define GPIO_PWM_SERVO_CONFIG			\
{						\
	.duty_cycle	=	50,		\
	.range_min	=	950,		\
	.range_max	=	2050,		\
	.period		=	20000000,	\
	.gpio		=	1,		\
}

static struct gpiopwm config = GPIO_PWM_SERVO_CONFIG;

static void fail(const char *reason)
{
	perror(reason);
	exit(EXIT_FAILURE);
}

static void sem_sync(sem_t *sem)
{
	int ret;

	for (;;) {
		ret = sem_wait(sem);
		if (ret == 0)
			return;
		if (errno != EINTR)
			fail("sem_wait");
	}
}

static inline void clear_screen(void)
{
	const char* cmd = "\e[1;1H\e[2J";
	int ret;

	ret = write(2, cmd, strlen(cmd));
	if (!ret)
		error(1, ret, "clear screen error");
}

static inline void print_config(char *str)
{
	int i;

	printf("Config: %s\n", str);
	for (i = 0; i < last_ip ; i++)
		printf("%s", ip_str[i]);
	printf(" device     : %s\n", device_name);
	printf(" range      : [%d, %d]\n", config.range_min, config.range_max);
	printf(" period     : %d nsec\n", config.period);
	printf(" gpio pin   : %d\n", config.gpio);
	printf(" duty cycle : %d\n", config.duty_cycle);
}

static inline void input_message(void)
{
	print_config("");
	printf("\n GPIO PWM Control\n");
	printf( "  Enter duty_cycle [0-100] : ");
}

static void get_ip_addresses(void)
{
	char ip[INET_ADDRSTRLEN];
	struct sockaddr_in *s_in;
	struct ifconf ifconf;
	struct ifreq ifr[10];
	int ret;
	int ifs;
	int i;
	int s;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return;

	ifconf.ifc_buf = (char *) ifr;
	ifconf.ifc_len = sizeof(ifr);

	if (ioctl(s, SIOCGIFCONF, &ifconf) == -1)
		return;

	ifs = ifconf.ifc_len / sizeof(ifr[0]);

	/* we wont _display_ more than MAX_IP_INTERFACES */
	if (ifs > MAX_IP_INTERFACES)
		ifs = MAX_IP_INTERFACES;

	last_ip = ifs + 1;

	for (i = 0; i < ifs; i++) {
		s_in = (struct sockaddr_in *) &ifr[i].ifr_addr;
		if (!inet_ntop(AF_INET, &s_in->sin_addr, ip, sizeof(ip)))
			return;
		ret = asprintf(&ip_str[i]," ip      : %s\n", ip);
		if (ret)
			perror("asprintf");
	}

	ret = asprintf(&ip_str[i]," port    : %d\n\n", port);
	if (ret)
		perror("asprintf");

	close(s);
}

static void setup_sched_parameters(pthread_attr_t *attr, int prio)
{
	struct sched_param p;
	int ret;

	ret = pthread_attr_init(attr);
	if (ret)
		error(1, ret, "pthread_attr_init()");

	ret = pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
	if (ret)
		error(1, ret, "pthread_attr_setinheritsched()");

	ret = pthread_attr_setschedpolicy(attr, prio ? SCHED_FIFO : SCHED_OTHER);
	if (ret)
		error(1, ret, "pthread_attr_setschedpolicy()");

	p.sched_priority = prio;
	ret = pthread_attr_setschedparam(attr, &p);
	if (ret)
		error(1, ret, "pthread_attr_setschedparam()");
}

static void *gpiopwm_init_thread(void *cookie)
{
	int ret;

	pthread_setname_np(pthread_self(), "gpio-pwm-handler");
	ret = ioctl(dev, GPIOPWM_RTIOC_SET_CONFIG, config);
	if (ret)
		error(1, ret, "failed to set config");

	ioctl(dev, GPIOPWM_RTIOC_START);

	/* setup completed: allow handler to run */
	sem_post(&setup);

	/* wait for completion */
	sem_sync(&synch);
	ioctl(dev, GPIOPWM_RTIOC_STOP);

	return NULL;
}

/*
 * Controls the motor receving the duty cycle sent over UDP
 * ie: echo -n <duty_cycle> | nc  -w1 -u <ipaddr> <port>
 */
static void *gpiopwm_udp_ctrl_thread(void *cookie)
{
	struct sockaddr_in saddr;
	struct sockaddr_in caddr;
	unsigned int duty_cycle;
	const int blen = 4;
	int optval = 1;
	socklen_t clen;
	char buf[blen];
	int sockfd;
	int ret;

	pthread_setname_np(pthread_self(), "gpio-pwm.netcat");

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
		perror("socket");

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));

	bzero((char *) &saddr, sizeof(saddr));
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);
	saddr.sin_family = AF_INET;

	if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
		perror("bind");

	clen = sizeof(caddr);
	sem_sync(&setup);
	for (;;) {

		clear_screen();
		print_config("UDP Server\n");

		memset(buf,'\0', blen);
		ret = recvfrom(sockfd, buf, blen - 1, 0, (struct sockaddr *)&caddr, &clen);
		if (ret < 0)
			perror("recvfrom");

		duty_cycle = strtol(buf, NULL, 10);
		if (duty_cycle < MIN_DUTY_CYCLE || duty_cycle > MAX_DUTY_CYCLE)
			continue;

		ret = ioctl(dev, GPIOPWM_RTIOC_CHANGE_DUTY_CYCLE, duty_cycle);
		if (ret)
			break;

		config.duty_cycle = duty_cycle;
	}

	return NULL;
}

/*
 * Manual control of the pwm duty cycle.
 */
static void *gpiopwm_manual_ctrl_thread(void *cookie)
{
	unsigned int duty_cycle;
	size_t len = 4;
	char *in;
	int ret;

	pthread_setname_np(pthread_self(), "gpio-pwm.manual");

	in = malloc(len * sizeof(*in));
	if (!in)
		goto err;

	sem_sync(&setup);
	for (;;) {
		clear_screen();
		input_message();

		len = getline(&in, &len, stdin);
		if (len == -1 || len == 1)
			break;

		duty_cycle = atoi(in);
		if (!duty_cycle && strncmp(in, "000", len - 1) != 0)
			break;

		ret = ioctl(dev, GPIOPWM_RTIOC_CHANGE_DUTY_CYCLE, duty_cycle);
		if (ret) {
			fprintf(stderr, "invalid duty cycle %d\n", duty_cycle);
			break;
		}

		config.duty_cycle = duty_cycle;
	}

	free(in);
err:
	sem_post(&synch);

	return NULL;
}

/*
 * Continuously sweep all duty cycles 0..100 and 100..0.
 * No mode switches should occur.
 */
static void *gpiopwm_sweep_ctrl_thread(void *cookie)
{
	struct timespec delay;
	struct duty_values {
		enum { fwd, bck} direction;
		int x;
	} values;
	int ret;

	pthread_setname_np(pthread_self(), "gpio-pwm.sweep");

	delay = (struct timespec) {.tv_sec = 0, .tv_nsec = 10 * config.period};
	values = (struct duty_values) {.direction = fwd, .x = MIN_DUTY_CYCLE};

	sem_sync(&setup);
	for (;;) {
		if (stop)
			break;

		ret = ioctl(dev, GPIOPWM_RTIOC_CHANGE_DUTY_CYCLE, values.x);
		if (ret) {
			fprintf(stderr, "invalid duty cycle %d\n", values.x);
			break;
		}

		nanosleep(&delay, NULL);

		if (values.direction == bck) {
			if (values.x - (step - 1) > MIN_DUTY_CYCLE)
				values.x -= step;
			else {
				values.direction = fwd;
				values.x = MIN_DUTY_CYCLE;
				continue;
			}
		}

		if (values.direction == fwd) {
			if (values.x + (step - 1) < MAX_DUTY_CYCLE)
				values.x += step;
			else {
				values.direction = bck;
				values.x = MAX_DUTY_CYCLE;
			}
		}
	}
	sem_post(&synch);

	return NULL;
}

static void gpiopwm_sweep_sig_handler(int sig)
{
	stop = 1;
}

static const struct option options[] = {
	{
#define help_opt		0
		.name = "help",
		.has_arg = 0,
		.flag = NULL,
	},
	{
#define sweep_range_opt		1
		.name = "sweep",
		.has_arg = 1,
		.flag = NULL,
	},
	{
#define manual_opt		2
		.name = "manual",
		.has_arg = 0,
		.flag = NULL,
	},
	{
#define config_opt		3
		.name = "config",
		.has_arg = 1,
		.flag = NULL,
	},
	{
#define udp_opt			4
		.name = "udp",
		.has_arg = 1,
		.flag = NULL,
	},
	{
		.name = NULL,
	}
};

static void usage(void)
{
	fprintf(stderr, "Usage:\n"
		"gpiopwm --config=dev:min:max:period:gpio:duty [--sweep=<step> | --udp=<port> | --manual]\n\n"
		"--config=<..>\n"
		"	dev:		/dev/rtdm/gpio-pwm id [0..7]\n"
		"	min:		min active period in usec\n"
		"	max:		max active period in usec\n"
		"	period:		base signal period in nsec\n"
		"	gpio:		gpio pin number\n"
		"	duty:		default duty cycle [0..100]\n"
		"--sweep=<step>\n"
		"			sweep all duty cycle ranges in a loop\n"
		"			in step increments [default 1]\n"
		"--manual		input duty cycle from the command line\n"
		"--udp=<port>		receive duty cycle from the network\n"
		"			ie: echo -n <duty_cycle> | nc  -w1 -u <ipaddr> <port>\n"
		);
}

int main(int argc, char *argv[])
{
	gpiopwm_control_thread handler = NULL;
	pthread_t pwm_task, ctrl_task;
	int opt, lindex, device = 0;
	pthread_attr_t tattr;
	char *p;
	int ret;

	for (;;) {
		lindex = -1;
		opt = getopt_long_only(argc, argv, "", options, &lindex);
		if (opt == EOF)
			break;

		switch (lindex) {
		case sweep_range_opt:
			handler = gpiopwm_sweep_ctrl_thread;
			signal(SIGINT, gpiopwm_sweep_sig_handler);
			step = atoi(optarg);
			step = step < 1 ? 1 : step;
			break;
		case manual_opt:
			handler = gpiopwm_manual_ctrl_thread;
			signal(SIGINT, SIG_IGN);
			break;
		case udp_opt:
			handler = gpiopwm_udp_ctrl_thread;
			port = atoi(optarg);
			get_ip_addresses();
			break;
		case config_opt:
			p = strtok(optarg,":");
			device = p ? atoi(p): -1;
			p = strtok(NULL,":");
			config.range_min = p ? atoi(p): -1;
			p = strtok(NULL,":");
			config.range_max = p ? atoi(p): -1;
			p = strtok(NULL,":");
			config.period = p ? atoi(p): -1;
			p = strtok(NULL,":");
			config.gpio = p ? atoi(p): -1;
			p = strtok(NULL,"");
			config.duty_cycle = p ? atoi(p): -1;
			break;
		case help_opt:
		default:
			usage();
			exit(1);
		}
	}

	if (handler == NULL) {
		usage();
		exit(1);
	}

	ret = sem_init(&synch, 0, 0);
	if (ret < 0)
		error(1, errno, "can't create synch semaphore");

	ret = sem_init(&setup, 0, 0);
	if (ret < 0)
		error(1, errno, "can't create setup semaphore");

	ret = asprintf(&device_name, "%s%d", DEVICE_NAME, device);
	if (ret < 0)
		error(1, EINVAL, "can't create device name");

	dev = open(device_name, O_RDWR);
	if (dev < 0)
		error(1, EINVAL, "can't open %s", device_name);

	setup_sched_parameters(&tattr, 99);
	ret = pthread_create(&ctrl_task, &tattr, handler, NULL);
	if (ret)
		error(1, ret, "pthread_create(ctrl_handler)");

	setup_sched_parameters(&tattr, 98);
	ret = pthread_create(&pwm_task, &tattr, gpiopwm_init_thread, NULL);
	if (ret)
		error(1, ret, "pthread_create(init thread)");

	pthread_join(pwm_task, NULL);
	pthread_join(ctrl_task, NULL);

	pthread_attr_destroy(&tattr);

	ret = close(dev);
	if (ret < 0)
		error(1, EINVAL, "can't close");

	return 0;
}

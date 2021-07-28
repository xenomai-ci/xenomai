/***
 *
 *  tools/rtifconfig.c
 *  ifconfig replacement for RTnet
 *
 *  rtnet - real-time networking subsystem
 *  Copyright (C) 1999, 2000 Zentropic Computing, LLC
 *                2004, 2005 Jan Kiszka <jan.kiszka@web.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <rtnet_chrdev.h>

/* Some old toolchains don't ARPHRD_IEEE1394 defined */
#ifndef ARPHRD_IEEE1394
#define ARPHRD_IEEE1394 24
#endif


#define PRINT_FLAG_ALL          1
#define PRINT_FLAG_INACTIVE     2


int                     f;
struct rtnet_core_cmd   cmd;

struct user_net_device_stats {
    unsigned long long rx_packets;      /* total packets received       */
    unsigned long long tx_packets;      /* total packets transmitted    */
    unsigned long long rx_bytes;        /* total bytes received         */
    unsigned long long tx_bytes;        /* total bytes transmitted      */
    unsigned long rx_errors;            /* bad packets received         */
    unsigned long tx_errors;            /* packet transmit problems     */
    unsigned long rx_dropped;           /* no space in linux buffers    */
    unsigned long tx_dropped;           /* no space available in linux  */
    unsigned long rx_multicast;         /* multicast packets received   */
    unsigned long rx_compressed;
    unsigned long tx_compressed;
    unsigned long collisions;

    /* detailed rx_errors: */
    unsigned long rx_length_errors;
    unsigned long rx_over_errors;       /* receiver ring buff overflow  */
    unsigned long rx_crc_errors;        /* recved pkt with crc error    */
    unsigned long rx_frame_errors;      /* recv'd frame alignment error */
    unsigned long rx_fifo_errors;       /* recv'r fifo overrun          */
    unsigned long rx_missed_errors;     /* receiver missed packet     */
    /* detailed tx_errors */
    unsigned long tx_aborted_errors;
    unsigned long tx_carrier_errors;
    unsigned long tx_fifo_errors;
    unsigned long tx_heartbeat_errors;
    unsigned long tx_window_errors;
};

struct itf_stats {
    char name[IFNAMSIZ];
    struct user_net_device_stats stats;
    struct itf_stats *next;
};

static struct itf_stats *itf_stats_head;

void parse_stats(void)
{
    struct itf_stats *itf;
    char buf[512];
    FILE *fh;
    int ret;

    fh = fopen("/proc/rtnet/stats", "r");
    if (!fh)
        return;

    fgets(buf, sizeof buf, fh); /* eat headers */
    fgets(buf, sizeof buf, fh);

    while (fgets(buf, sizeof buf, fh)) {
        char *name, *p;

        itf = malloc(sizeof(*itf));
        if (!itf)
            return;

        name = buf;
        while (isspace(*name))
            name++;
        p = name;
        while (*p && *p != ':')
            p++;
        *p = '\0';
        snprintf(itf->name, sizeof(itf->name), "%s", name);

        p++;
        ret = sscanf(p,
               "%llu %llu %lu %lu %lu %lu %lu %lu %llu %llu %lu %lu %lu %lu %lu %lu",
               &itf->stats.rx_bytes,
               &itf->stats.rx_packets,
               &itf->stats.rx_errors,
               &itf->stats.rx_dropped,
               &itf->stats.rx_fifo_errors,
               &itf->stats.rx_frame_errors,
               &itf->stats.rx_compressed,
               &itf->stats.rx_multicast,
               &itf->stats.tx_bytes,
               &itf->stats.tx_packets,
               &itf->stats.tx_errors,
               &itf->stats.tx_dropped,
               &itf->stats.tx_fifo_errors,
               &itf->stats.collisions,
               &itf->stats.tx_carrier_errors,
               &itf->stats.tx_compressed);
        if (ret < 16) {
            free(itf);
            continue;
        }

        itf->next = itf_stats_head;
        itf_stats_head = itf;
    }

    fclose(fh);
}

struct itf_stats *find_stats(const char *itf_name)
{
    struct itf_stats *itf;

    for(itf = itf_stats_head; itf; itf = itf->next)
        if(!strcmp(itf->name, itf_name))
            break;

    return itf;
}


void help(void)
{
    fprintf(stderr, "Usage:\n"
        "\trtifconfig [-a] [<dev>]\n"
        "\trtifconfig <dev> up [<addr> [netmask <mask>]] "
            "[hw <HW> <address>] [[-]promisc]\n"
        "\trtifconfig <dev> down\n"
        );

    exit(1);
}



void print_dev(void)
{
    struct in_addr  ip_addr;
    struct in_addr  broadcast_ip;
    unsigned int    flags;
    struct itf_stats *itf;


    cmd.head.if_name[IFNAMSIZ - 1] = 0;

    printf("%-9s Medium: ", cmd.head.if_name);

    if ((cmd.args.info.flags & IFF_LOOPBACK) != 0)
        printf("Local Loopback\n");
    else
        switch (cmd.args.info.type) {
            case ARPHRD_ETHER:
            case ARPHRD_IEEE1394:
                printf("%s Hardware address: "
                       "%02X:%02X:%02X:%02X:%02X:%02X\n",
                       (cmd.args.info.type == ARPHRD_ETHER) ?
                           "Ethernet " : "Eth1394 ",
                       cmd.args.info.dev_addr[0], cmd.args.info.dev_addr[1],
                       cmd.args.info.dev_addr[2], cmd.args.info.dev_addr[3],
                       cmd.args.info.dev_addr[4], cmd.args.info.dev_addr[5]);
                break;

            default:
                printf("unknown (%X)\n", cmd.args.info.type);
        }

    if (cmd.args.info.ip_addr != 0) {
        ip_addr.s_addr      = cmd.args.info.ip_addr;
        broadcast_ip.s_addr = cmd.args.info.broadcast_ip;
        printf("          IP address: %s  ", inet_ntoa(ip_addr));
        if (cmd.args.info.flags & IFF_BROADCAST)
            printf("Broadcast address: %s", inet_ntoa(broadcast_ip));
        printf("\n");
    }

    flags = cmd.args.info.flags &
        (IFF_UP | IFF_BROADCAST | IFF_LOOPBACK | IFF_RUNNING | IFF_PROMISC);
    printf("          %s%s%s%s%s%s MTU: %d\n",
           ((flags & IFF_UP) != 0) ? "UP " : "",
           ((flags & IFF_BROADCAST) != 0) ? "BROADCAST " : "",
           ((flags & IFF_LOOPBACK) != 0) ? "LOOPBACK " : "",
           ((flags & IFF_RUNNING) != 0) ? "RUNNING " : "",
           ((flags & IFF_PROMISC) != 0) ? "PROMISC " : "",
           (flags == 0) ? "[NO FLAGS] " : "", cmd.args.info.mtu);

    if ((itf = find_stats(cmd.head.if_name))) {
        unsigned long long rx, tx, short_rx, short_tx;
        char Rext[5]="b";
        char Text[5]="b";

        printf("          ");
        printf("RX packets:%llu errors:%lu dropped:%lu overruns:%lu frame:%lu\n",
               itf->stats.rx_packets, itf->stats.rx_errors,
               itf->stats.rx_dropped, itf->stats.rx_fifo_errors,
               itf->stats.rx_frame_errors);

        printf("          ");
        printf("TX packets:%llu errors:%lu dropped:%lu overruns:%lu carrier:%lu\n",
               itf->stats.tx_packets, itf->stats.tx_errors,
               itf->stats.tx_dropped, itf->stats.tx_fifo_errors,
               itf->stats.tx_carrier_errors);

        printf("          collisions:%lu ", itf->stats.collisions);
        printf("\n          ");

        rx = itf->stats.rx_bytes;  
        tx = itf->stats.tx_bytes;
        short_rx = rx * 10;  
        short_tx = tx * 10;
        if (rx > 1048576) {
            short_rx /= 1048576;
            strcpy(Rext, "Mb");
        } else if (rx > 1024) {
            short_rx /= 1024;
            strcpy(Rext, "Kb");
        }
        if (tx > 1048576) {
            short_tx /= 1048576;
            strcpy(Text, "Mb");
        } else if (tx > 1024) {
            short_tx /= 1024;
            strcpy(Text, "Kb");
        }

        printf("RX bytes:%llu (%lu.%lu %s)  TX bytes:%llu (%lu.%lu %s)\n",
               rx, (unsigned long)(short_rx / 10),
               (unsigned long)(short_rx % 10), Rext, 
               tx, (unsigned long)(short_tx / 10), 
               (unsigned long)(short_tx % 10), Text);
    }
    printf("\n");
}



void do_display(int print_flags)
{
    int i;
    int ret;


    parse_stats();

    if ((print_flags & PRINT_FLAG_ALL) != 0)
        for (i = 1; i <= MAX_RT_DEVICES; i++) {
            cmd.args.info.ifindex = i;

            ret = ioctl(f, IOC_RT_IFINFO, &cmd);
            if (ret == 0) {
                if (((print_flags & PRINT_FLAG_INACTIVE) != 0) ||
                    ((cmd.args.info.flags & IFF_UP) != 0))
                    print_dev();
            } else if (errno != ENODEV) {
                perror("ioctl");
                exit(1);
            }
        }
    else {
        cmd.args.info.ifindex = 0;

        ret = ioctl(f, IOC_RT_IFINFO, &cmd);
        if (ret < 0) {
            perror("ioctl");
            exit(1);
        }

        print_dev();
    }

    exit(0);
}



void do_up(int argc, char *argv[])
{
    int                 ret;
    int                 i;
    struct in_addr      addr;
    __u32               ip_mask;
    struct ether_addr   hw_addr;


    if ((argc > 3) && (inet_aton(argv[3], &addr))) {
        i = 4;
        cmd.args.up.ip_addr = addr.s_addr;
        if (addr.s_addr == 0xFFFFFFFF) {
            fprintf(stderr, "Invalid IP address!\n");
            exit(1);
        }
    } else {
        i = 3;
        /* don't change ip settings */
        cmd.args.up.ip_addr = 0xFFFFFFFF;
    }

    /* set default netmask */
    if (ntohl(cmd.args.up.ip_addr) <= 0x7FFFFFFF)       /* 127.255.255.255  */
        ip_mask = 0x000000FF;                           /* 255.0.0.0        */
    else if (ntohl(cmd.args.up.ip_addr) <= 0xBFFFFFFF)  /* 191.255.255.255  */
        ip_mask = 0x0000FFFF;                           /* 255.255.0.0      */
    else
        ip_mask = 0x00FFFFFF;                           /* 255.255.255.0    */

    /* default: don't change flags, don't set dev_addr */
    cmd.args.up.set_dev_flags   = 0;
    cmd.args.up.clear_dev_flags = 0;
    cmd.args.up.dev_addr_type   = 0xFFFF;

    /* parse optional parameters */
    for ( ; i < argc; i++) {
        if (strcmp(argv[i], "netmask") == 0) {
            if ((++i >= argc) || (cmd.args.up.ip_addr == 0) ||
                (!inet_aton(argv[i], &addr)))
                help();
            ip_mask = addr.s_addr;
        } else if (strcmp(argv[i], "hw") == 0) {
            if ((++i >= argc) || (strcmp(argv[i], "ether") != 0) ||
                (++i >= argc) || (ether_aton_r(argv[i], &hw_addr) == NULL))
                help();
            memcpy(cmd.args.up.dev_addr, hw_addr.ether_addr_octet,
                   sizeof(hw_addr.ether_addr_octet));
            cmd.args.up.dev_addr_type = ARPHRD_ETHER;
        } else if (strcmp(argv[i], "promisc") == 0) {
            cmd.args.up.set_dev_flags   |= IFF_PROMISC;
            cmd.args.up.clear_dev_flags &= ~IFF_PROMISC;
        } else if (strcmp(argv[i], "-promisc") == 0) {
            cmd.args.up.set_dev_flags   &= ~IFF_PROMISC;
            cmd.args.up.clear_dev_flags |= IFF_PROMISC;
        } else
            help();
    }

    cmd.args.up.broadcast_ip = cmd.args.up.ip_addr | (~ip_mask);

    ret = ioctl(f, IOC_RT_IFUP, &cmd);
    if (ret < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void do_down(int argc,char *argv[])
{
    int r;

    if (argc > 3)
        help();

    r = ioctl(f, IOC_RT_IFDOWN, &cmd);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



int main(int argc, char *argv[])
{
    if ((argc > 1) && (strcmp(argv[1], "--help") == 0))
        help();

    f = open("/dev/rtnet", O_RDWR);

    if (f < 0) {
        perror("/dev/rtnet");
        exit(1);
    }

    if (argc == 1)
        do_display(PRINT_FLAG_ALL);

    if (strcmp(argv[1], "-a") == 0) {
        if (argc == 3) {
            strncpy(cmd.head.if_name, argv[2], IFNAMSIZ);
            do_display(PRINT_FLAG_INACTIVE);
        } else
            do_display(PRINT_FLAG_INACTIVE | PRINT_FLAG_ALL);
    } else
        strncpy(cmd.head.if_name, argv[1], IFNAMSIZ);

    if (argc < 3)
        do_display(0);

    if (strcmp(argv[2], "up") == 0)
        do_up(argc,argv);
    if (strcmp(argv[2], "down") == 0)
        do_down(argc,argv);

    help();

    return 0;
}

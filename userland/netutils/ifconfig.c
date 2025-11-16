/*
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */

#define _BSD_SOURCE
#include <stdlib.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <ioctl.h>
#include <sys/ioctl.h>
#include <sys/frosted-io.h>
#include <net/if.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>      /* for fprintf etc */
#include <fcntl.h>

#include "net_compat.h"

#ifndef htonl
#define htonl(x) __builtin_bswap32(x)
#define ntohl(x) __builtin_bswap32(x)
#define htons(x) __builtin_bswap16(x)
#define ntohs(x) __builtin_bswap16(x)
#endif


static void copy_ifname(struct ifreq *ifr, const char *ifname)
{
    memset(ifr->ifr_name, 0, sizeof(ifr->ifr_name));
    if (!ifname)
        return;
    strncpy(ifr->ifr_name, ifname, IFNAMSIZ - 1);
    ifr->ifr_name[IFNAMSIZ - 1] = '\0';
}

static int ifconf_getproperties_fd(int sck, const char *ifname, uint8_t *macaddr,
                                   struct sockaddr_in *address,
                                   struct sockaddr_in *netmask,
                                   struct sockaddr_in *broadcast);

int
ifdown(char *ifname)
{
	int sck;
	struct ifreq eth;
	int retval = -1;


	sck = socket(AF_INET, SOCK_DGRAM, 0);
	if(sck < 0){
		return retval;
	}
	memset(&eth, 0, sizeof(struct ifreq));
	copy_ifname(&eth, ifname);
	eth.ifr_flags = 0;
	if(ioctl(sck, SIOCSIFFLAGS, &eth) < 0)
	{
		return retval;
	}
	close(sck);
	return 0;
}

int
ifup(char *ifname)
{
	int sck;
	struct ifreq eth;
	int retval = -1;


	sck = socket(AF_INET, SOCK_DGRAM, 0);
	if(sck < 0){
		return retval;
	}
	memset(&eth, 0, sizeof(struct ifreq));
	copy_ifname(&eth, ifname);
	eth.ifr_flags = IFF_UP|IFF_RUNNING|IFF_MULTICAST|IFF_BROADCAST;
	if(ioctl(sck, SIOCSIFFLAGS, &eth) < 0)
	{
		return retval;
	}
	close(sck);
	return 0;
}

int
ifconfig(char *ifname, char *address, char *netmask)
{
	int sck;
	struct ifreq eth;
	struct sockaddr_in addr = {.sin_family = 0}, nmask = {.sin_family = 0};
	struct in_addr tmpaddr;
	int retval = -1;
	int e;

	sck = socket(AF_INET, SOCK_DGRAM, 0);
	if(sck < 0){
		return retval;
	}
	memset(&eth, 0, sizeof(struct ifreq));
	copy_ifname(&eth, ifname);
	eth.ifr_flags = IFF_UP|IFF_RUNNING|IFF_MULTICAST|IFF_BROADCAST;

	if(ioctl(sck, SIOCSIFFLAGS, &eth) < 0)
	{
		goto ipfail;
	}

	addr.sin_family = AF_INET;
	inet_aton(address, &tmpaddr);
	addr.sin_addr.s_addr = tmpaddr.s_addr;
	//addr = (struct sockaddr_in *) &eth.ifr_addr;
	memcpy (&eth.ifr_addr, &addr, sizeof (struct sockaddr_in));

	if(ioctl(sck, SIOCSIFADDR, &eth) < 0){
	       	goto ipfail;
	}

	nmask.sin_family = AF_INET;
	inet_aton(netmask, &tmpaddr);
	nmask.sin_addr.s_addr =  tmpaddr.s_addr;
	memcpy (&eth.ifr_netmask, &nmask, sizeof (struct sockaddr_in));

	if(ioctl(sck, SIOCSIFNETMASK, &eth) < 0){
	       	goto ipfail;
	}
	close(sck);
	return 0;

ipfail:
	e = errno;
	close(sck);
	errno = e;
	return -1;
}


static int ifconf_getproperties_fd(int sck, const char *ifname, uint8_t *macaddr,
                                   struct sockaddr_in *address,
                                   struct sockaddr_in *netmask,
                                   struct sockaddr_in *broadcast)
{
	struct ifreq ifr;
	struct sockaddr_in addr = {.sin_family = 0}, nmask = {.sin_family = 0}, bcast = {.sin_family = 0};
	uint8_t pmac[6] = {0};

	memset(&ifr, 0, sizeof(ifr));
	// save interface name
	copy_ifname(&ifr, ifname);

	// IP Address
	if (ioctl(sck, SIOCGIFADDR, &ifr) == 0) {
		memcpy( &addr, (struct sockaddr_in *) &ifr.ifr_addr, sizeof(struct sockaddr_in));
	} else {
		memset(&addr, 0, sizeof(addr));
        }

	// Broadcast Address
	if (ioctl(sck, SIOCGIFBRDADDR, &ifr) < 0) {
		memset(&bcast, 0, sizeof(bcast));
	} else {
		memcpy( &bcast, (struct sockaddr_in *) &ifr.ifr_broadaddr, sizeof(struct sockaddr_in));
	}

	// NetMask Address
	if (ioctl(sck, SIOCGIFNETMASK, &ifr) < 0) {
		memset(&nmask, 0, sizeof(nmask));
	} else {
		memcpy( &nmask, (struct sockaddr_in *) &ifr.ifr_netmask, sizeof(struct sockaddr_in));
	}

	if (macaddr) {
		struct ifreq hw;
		memset(&hw, 0, sizeof(hw));
		copy_ifname(&hw, ifname);
		if (ioctl(sck, SIOCGIFHWADDR, &hw) == 0)
			memcpy(pmac, hw.ifr_addr.sa_data, 6);
		memcpy(macaddr, pmac, 6);
	}
	if (address)
		memcpy(address, &addr, sizeof(struct sockaddr_in));
	if (netmask)
		memcpy(netmask, &nmask, sizeof(struct sockaddr_in));
	if (broadcast)
		memcpy(broadcast, &bcast, sizeof(struct sockaddr_in));


	if (ioctl(sck, SIOCGIFFLAGS, &ifr) < 0){
		return -1;
	}

	return  (ifr.ifr_flags & IFF_UP);

}
int ifconf_getproperties(char *ifname, uint8_t *macaddr, struct sockaddr_in *address, struct sockaddr_in *netmask, struct sockaddr_in *broadcast)
{
	int sck;
	int ret;

	sck = socket(AF_INET, SOCK_DGRAM, 0);
	if(sck < 0){
		perror("socket");
		return -1;
	}
	ret = ifconf_getproperties_fd(sck, ifname, macaddr, address, netmask, broadcast);
	close(sck);
	return ret;
}
int ifconf_status(char *ifname) { return ifconf_getproperties(ifname, NULL, NULL, NULL, NULL); }
int ifconf_getmac(char *ifname, uint8_t *mac) { return ifconf_getproperties(ifname, mac, NULL, NULL, NULL); }
int ifconf_getaddress(char *ifname, struct sockaddr_in *address) { return ifconf_getproperties(ifname, NULL, address, NULL, NULL); }
int ifconf_getnetmask(char *ifname, struct sockaddr_in *netmask) { return ifconf_getproperties(ifname, NULL, NULL, netmask, NULL); }
int ifconf_getbroadcast(char *ifname, struct sockaddr_in *broadcast) { return ifconf_getproperties(ifname, NULL, NULL, NULL, broadcast); }
int iflinksense(char *ifname);

#define MAX_IFCONFIG_IFACES 8

static void ifconf_show_fd(int sck, const char *name)
{
    struct sockaddr_in a,n,b;
    int ret;

    ret = ifconf_getproperties_fd(sck, name, NULL, &a, &n, &b);
    if (ret < 0) {
        fprintf(stderr, "ifconfig: cannot query %s (%s)\r\n", name, strerror(errno));
        return;
    }
    printf("%s: flags:%s mtu 1500\r\n", name, (ret > 0)?"<UP,BROADCAST,MULTICAST,RUNNING>":"<DOWN>");
    printf("        inet %s ", inet_ntoa(a.sin_addr));
    printf("netmask %s ", inet_ntoa(n.sin_addr));
    printf("broadcast %s\r\n", inet_ntoa(b.sin_addr));
    printf("\r\n");
}

void ifconf_show(char *name)
{
    int sck;
    sck = socket(AF_INET, SOCK_DGRAM, 0);
    if (sck < 0) {
        perror("socket");
        return;
    }
    ifconf_show_fd(sck, name);
    close(sck);
}

static void usage(char *name)
{
    printf("Usage: %s [DEV [ADDR [netmask NMASK]]]\r\n", name);
    exit(1);
}

#ifndef APP_IFCONFIG_MODULE
int main(int argc, char *argv[])
#else
int icebox_ifconfig(int argc, char *argv[])
#endif
{
    int i;
    int ret;
    struct ifreq ifr;
    if (argc == 1) {
        int sock;
        struct ifconf ifc;
        struct ifreq ifreqs[MAX_IFCONFIG_IFACES];

        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("socket");
            exit(1);
        }

        memset(ifreqs, 0, sizeof(ifreqs));
        ifc.ifc_len = sizeof(ifreqs);
        ifc.ifc_req = ifreqs;
        if (ioctl(sock, SIOCGIFCONF, &ifc) < 0) {
            perror("ioctl(SIOCGIFCONF)");
            close(sock);
            exit(1);
        }
        ret = ifc.ifc_len / sizeof(struct ifreq);
        for (i = 0; i < ret; i++) {
            if (ifreqs[i].ifr_name[0] == '\0')
                continue;
            ifconf_show_fd(sock, ifreqs[i].ifr_name);
        }
        close(sock);
        exit(0);
    }
    memset(&ifr, 0, sizeof(struct ifreq));

    if (argc == 2) {
        ifconf_show(argv[1]);
        exit(0);
    }

    if (argc == 3) {
        ifconfig(argv[1], argv[2], "255.255.255.0");
        exit(0);
    }
    if (argc == 5) {
        if (strcmp(argv[3], "netmask") != 0) {
            usage(argv[0]);
        }
        ifconfig(argv[1], argv[2], argv[4]);
        exit(0);
    }

    usage(argv[0]);
}

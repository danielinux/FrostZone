
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>

#include "net_compat.h"
#define DEFAULT_LEN (64)

struct icmp_hdr {
    uint8_t type;
    uint8_t code;
    uint16_t chksum;
    uint16_t id;
    uint16_t seq;
};

static uint8_t payload[DEFAULT_LEN];

/* Use sys_clock_gettime directly — libgloss _gettimeofday_r in syscalls.c
 * has a broken signature (missing reent pointer), so gettimeofday() returns
 * uninitialized stack garbage. */
extern int sys_clock_gettime(int clock_id, struct timespec *tp);

static uint32_t gettime_ms(void)
{
    struct timespec tp;
    sys_clock_gettime(CLOCK_REALTIME, &tp);
    return (uint32_t)(tp.tv_sec * 1000 + tp.tv_nsec / 1000000);
}

int ping(struct sockaddr_in *dst, int count, int len)
{
    struct icmphdr *icmp_hdr = (struct icmphdr *)payload;
    uint32_t t_send, t_recv;
    int i;
    int sequence = 0;
    int sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_ICMP);
    struct sockaddr_in reply_from;
    socklen_t sockaddr_in_len = sizeof(struct sockaddr_in);

    if (sock < 0) {
        perror("socket");
        return -1;
    }
    if (len > DEFAULT_LEN)
        len = DEFAULT_LEN;

    printf("PING %s: %d data bytes\r\n", inet_ntoa(dst->sin_addr), len);

    i = 0;
    while((count == 0) || (i < count)) {
        memset(payload, 0, DEFAULT_LEN);
        icmp_hdr->type = ICMP_ECHO;
        icmp_hdr->un.echo.id = 1234;

        for(i = sizeof(struct icmphdr); i < DEFAULT_LEN && i < (int)(sizeof(struct icmphdr) + len); i++) {
            payload[i] = i & 0xFF;
        }
        icmp_hdr->un.echo.sequence = sequence++;
        t_send = gettime_ms();
        {
            int sr = sendto(sock, payload, len, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            if (sr < 0) {
                sleep(1);
                continue;
            }
        }
        {
            int r;
            r = recvfrom(sock, payload, DEFAULT_LEN, 0, (struct sockaddr *)&reply_from, &sockaddr_in_len);
            if (r <= 0) {
                sleep(1);
            } else {
                uint32_t triptime;
                t_recv = gettime_ms();
                triptime = (t_recv >= t_send) ? (t_recv - t_send) : 0;
                printf("%d bytes from %s: icmp_seq=%d time=%lu ms\r\n",
                       r, inet_ntoa(reply_from.sin_addr),
                       icmp_hdr->un.echo.sequence,
                       (unsigned long)triptime);
                sleep(1);
            }
        }
    }
    return 0;
}


#ifndef APP_PING_MODULE
int main(int argc, char *argv[])
#else
int icebox_ping(int argc, char *argv[])
#endif
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(struct sockaddr_in));
    dst.sin_family = AF_INET;
    if (argc != 2) {
        printf("usage: %s destination_ip\n", argv[0]);
        return 1;
    }

    if (inet_aton(argv[1], &dst.sin_addr) == 0) {
        perror("inet_aton");
        printf("%s isn't a valid IP address\n", argv[1]);
        return 1;
    }
    ping(&dst, 0, DEFAULT_LEN);
    return 0;
}

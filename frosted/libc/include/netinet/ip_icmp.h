/*********************************************************************
   . IP_ICMP: struct declaration for icmphdr, etc.
   .

 *********************************************************************/
#ifndef __INCLUDE_IP_ICMP
#define __INCLUDE_IP_ICMP

#define IPPROTO_ICMP        (1)

struct __attribute__((packed)) icmphdr {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union
    {
        struct {
            uint16_t	id;
            uint16_t	sequence;
        } echo;			/* echo datagram */
        uint32_t	gateway;	/* gateway address */
        struct {
            uint16_t	zero;
            uint16_t	mtu;
        } frag;			/* path mtu discovery */
    } un;
};


#define ICMP_ECHOREPLY    0
#define ICMP_DEST_UNREACH 3
#define ICMP_SOURCE_QUENCH  4
#define ICMP_REDIRECT   5
#define ICMP_ECHO   8
#define ICMP_TIME_EXCEEDED  11
#define ICMP_PARAMETERPROB  12
#define ICMP_TIMESTAMP    13
#define ICMP_TIMESTAMPREPLY 14
#define ICMP_INFO_REQUEST 15
#define ICMP_INFO_REPLY   16
#define ICMP_ADDRESS    17
#define ICMP_ADDRESSREPLY 18


#define ICMP_UNREACH    3
#define ICMP_SOURCEQUENCH  4
#define ICMP_ROUTERADVERT  9
#define ICMP_ROUTERSOLICIT  10
#define ICMP_TIMXCEED    11
#define ICMP_PARAMPROB    12
#define ICMP_TSTAMP    13
#define ICMP_TSTAMPREPLY  14
#define ICMP_IREQ    15
#define ICMP_IREQREPLY    16
#define ICMP_MASKREQ    17
#define ICMP_MASKREPLY    18

#define ICMP_MAXTYPE    18


#define ICMP_UNREACH_NET          0
#define ICMP_UNREACH_HOST          1
#define ICMP_UNREACH_PROTOCOL          2
#define ICMP_UNREACH_PORT          3
#define ICMP_UNREACH_NEEDFRAG          4
#define ICMP_UNREACH_SRCFAIL          5
#define ICMP_UNREACH_NET_UNKNOWN        6
#define ICMP_UNREACH_HOST_UNKNOWN       7
#define ICMP_UNREACH_ISOLATED          8
#define ICMP_UNREACH_NET_PROHIB          9
#define ICMP_UNREACH_HOST_PROHIB        10
#define ICMP_UNREACH_TOSNET          11
#define ICMP_UNREACH_TOSHOST          12
#define ICMP_UNREACH_FILTER_PROHIB      13
#define ICMP_UNREACH_HOST_PRECEDENCE    14
#define ICMP_UNREACH_PRECEDENCE_CUTOFF  15


#define ICMP_REDIRECT_NET  0
#define ICMP_REDIRECT_HOST  1
#define ICMP_REDIRECT_TOSNET  2
#define ICMP_REDIRECT_TOSHOST  3


#define ICMP_TIMXCEED_INTRANS  0
#define ICMP_TIMXCEED_REASS  1


#define ICMP_PARAMPROB_OPTABSENT 1

#endif


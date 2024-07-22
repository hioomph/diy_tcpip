#ifndef _NETIF_PCAP_H_
#define _NETIF_PCAP_H_

// netif - network interface 网络接口
#include "net_err.h"
#include "netif.h"

typedef struct _pcap_data_t {
    const char *ip;         // 使用的网卡
    const uint8_t *hwaddr;  // 网卡的物理地址
}pcap_data_t;

extern const netif_ops_t netdev_ops;

#endif // _NETIF_PCAP_H_
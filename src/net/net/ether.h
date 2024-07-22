/**
 * @file ether.h
 * @brief 以太网协议支持，不包含ARP协议处理
 */

#ifndef _ETHER_H_
#define _ETHER_H_

#include <stdint.h>
#include "ether.h"
#include "net_err.h"

#define ETHER_HWA_SIZE      6
#define ETHER_MTU           1500

// sizeof(ether_hdr_t) -> 14
// sizeof(ether_pkt_t) -> 1514

#pragma pack(1)
// 以太网帧的包头：目的地址 + 源地址 + 类型
typedef struct _ether_hdr_t {
    uint8_t dest[ETHER_HWA_SIZE];
    uint8_t src[ETHER_HWA_SIZE];
    uint16_t protocol;
}ether_hdr_t;

typedef struct _ether_pkt_t {
    ether_hdr_t hdr;  // 以太帧头部
    uint8_t data[ETHER_MTU];   // 以太帧数据负载
}ether_pkt_t;
#pragma pack()


net_err_t ether_init(void);

#endif // _ETHER_H_
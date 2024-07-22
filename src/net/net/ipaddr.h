/**
 * @file ipaddr.h
 * @brief IP地址定义及接口函数
 */

#ifndef _IPADDR_H_
#define _IPADDR_H_

#include "net_err.h"
#include <stdint.h>

#define IPV4_ADDR_SIZE             4            // IPv4地址长度

// 192.168.74.2
typedef struct _ipaddr_t {
    enum {
        IPADDR_V4,
    }type;

    // 注意，IP地址总是按大端存放
    union {
        uint32_t q_addr;
        uint8_t a_addr[IPV4_ADDR_SIZE];
    };
}ipaddr_t;

void ipaddr_set_any(ipaddr_t * ip);
net_err_t ipaddr_from_str(ipaddr_t *ip, const char *str);
void ipaddr_copy(ipaddr_t *dest, const ipaddr_t *src);
ipaddr_t *ipaddr_get_any(void);

#endif // _IPADDR_H_
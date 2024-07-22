/**
 * @file ipaddr.c
 * @brief IP地址定义及接口函数
 */

#include "ipaddr.h"
#include "dbg.h"
#include "net_err.h"

/**
 * @brief 设置ip为任意ip地址
 * @param ip 待设置的ip地址
 */
void ipaddr_set_any(ipaddr_t * ip) {
    ip->type = IPADDR_V4;
    ip->q_addr = 0;
}

/**
 * @brief 根据传入的字符串设置ip地址
 */
net_err_t ipaddr_from_str(ipaddr_t *ip, const char *str) {
    if (!ip || !str) {
        return NET_ERR_PARAM;
    }

    ip->type = IPADDR_V4;
    ip->q_addr = 0;

    // 192.168.74.1
    // "192" -> 192 -> ip->a_addr[0]
    uint8_t *p = ip->a_addr;
    uint8_t sub_addr = 0;
    char c;
    int dot_count = 0;       // 点的个数
    int digit_count = 0;     // 每一组数字的位数

    while ((c = *str++) != '\0') {
        if ((c >= '0') && (c <= '9')) {
            // 1           = 1
            // 1 * 10 + 9  = 19
            // 19 * 10 + 2 = 192
            if (digit_count == 1 && sub_addr * 10 == sub_addr) {
                dbg_error(DBG_NETIF, "number begin with 0");
                return NET_ERR_PARAM;                
            }
            sub_addr = sub_addr * 10 + (c - '0');  
            if (sub_addr > 255) {
                dbg_error(DBG_NETIF, "number exceed 255");
                return NET_ERR_PARAM;
            }
            digit_count++;
        } else if (c == '.') {
            *p++ = sub_addr;  // 向a_addr中保存一组完整的数字
            sub_addr = 0;
            digit_count = 0;
            dot_count++;
            if (dot_count > 3) {
                dbg_error(DBG_NETIF, "too many number of dots");
                return NET_ERR_PARAM;
            }
        } else {
            dbg_error(DBG_NETIF, "invalid character in str");
            return NET_ERR_PARAM;
        }
    }

    if (dot_count != 3) {
        dbg_error(DBG_NETIF, "incorrect number of dots");
        return NET_ERR_PARAM;
    }
    *p = sub_addr;

    return NET_ERR_OK;
}

/**
 * @brief 复制ip地址
 */
void ipaddr_copy(ipaddr_t *dest, const ipaddr_t *src) {
    if (!dest || !src) {
        return;
    }

    dest->type = src->type;
    dest->q_addr = src->q_addr;
}

/**
 * @brief 获取缺省地址
 */
ipaddr_t *ipaddr_get_any(void) {
    static const ipaddr_t ipaddr_any = {.type = IPADDR_V4, .q_addr = 0};

    return (ipaddr_t *)&ipaddr_any;
}
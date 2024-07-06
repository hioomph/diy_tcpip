/**
 * @brief 使用pcap_device创建的虚拟网络接口
 */

#include "netif_pcap.h"
#include "net_err.h"
#include "sys_plat.h"

/**
 * @brief 接收线程
*/
void recv_thread(void *arg) {
    plat_printf("recv thread is running...\n");

    while (1) {
        sys_sleep(1);
    }
}

/**
 * @brief 发送线程
*/
void xmit_thread(void *arg) {
    plat_printf("xmit thread is running...\n");

    while (1) {
        sys_sleep(1);
    }
}

/**
 * @brief 网卡使用pcap驱动时的初始化
 *        注意，当包含多张网卡时，每个网卡都包含接收/发送两个线程
*/
net_err_t netif_pcap_open(void) {
    sys_thread_create(recv_thread, (void *)0);
    sys_thread_create(xmit_thread, (void *)0);

    return NET_ERR_OK;
}
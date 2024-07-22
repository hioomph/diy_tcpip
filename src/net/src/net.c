/**
 * @brief TCP/IP协议栈初始化
 */

#include "net.h"
#include "dbg.h"
#include "ether.h"
#include "exmsg.h"
#include "net_plat.h"
#include "netif.h"
#include "pktbuf.h"
#include "loop.h"
#include "tools.h"

/**
 * @brief 协议栈初始化
 */
net_err_t net_init(void) {
    dbg_info(DBG_INIT, "init net...");

    // 各模块初始化
    net_plat_init();  // 初始化硬件资源
    tools_init();
    exmsg_init();
    pktbuf_init();
    netif_init();
    loop_init();
    ether_init();
    
    return NET_ERR_OK;
}

/**
 * @brief 协议栈启动
 */
net_err_t net_start(void) {
    exmsg_start();

    dbg_info(DBG_INIT, "net is running.");
    return NET_ERR_OK;
}
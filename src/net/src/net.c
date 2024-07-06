#include "net.h"
#include "exmsg.h"
#include "net_plat.h"

/**
 * @brief 协议栈初始化
*/
net_err_t net_init(void) {
    net_plat_init();  // 初始化硬件资源

    exmsg_init();
    return NET_ERR_OK;
}

/**
 * @brief 协议栈启动
*/
net_err_t net_start(void) {
    exmsg_start();
    return NET_ERR_OK;
}
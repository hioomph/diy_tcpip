/**
 * @brief TCP/IP核心线程通信模块。
 *        此处运行了一个核心线程，所有TCP/IP中相关的事件都交由该线程处理
*/

#include "exmsg.h"
#include "sys_plat.h"

net_err_t exmsg_init(void) {
    return NET_ERR_OK;
}

static void work_thread(void *arg) {
    plat_printf("exmsg id running...\n");

    while (1) {
        sys_sleep(1);
    }
}

net_err_t exmsg_start(void) {
    sys_thread_t thread = sys_thread_create(work_thread, (void*)0);
    if (thread == SYS_THREAD_INVALID) {
        return NET_ERR_SYS;
    }
}
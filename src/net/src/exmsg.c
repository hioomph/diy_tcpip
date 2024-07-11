/**
 * @brief TCP/IP核心线程通信模块。
 *        此处运行了一个核心线程，所有TCP/IP中相关的事件都交由该线程处理
 */

#include "exmsg.h"
#include "dbg.h"
#include "fixq.h"
#include "mblock.h"
#include "net_err.h"
#include "sys_plat.h"

static void *msg_tbl[EXMSG_MSG_CNT];        // 消息缓冲区
static fixq_t msg_queue;                    // 消息队列

// 通过msg_block从msg_buffer中申请一个消息，写入后再发给msg_queue
// msg_queue处理完毕后，再返回给msg_block
static exmsg_t msg_buffer[EXMSG_MSG_CNT];   // 消息块
static mblock_t msg_block;                  // 消息块分配器

net_err_t exmsg_init(void) {
    dbg_info(DBG_MSG, "exmsg init");

    // 初始化消息队列
    net_err_t err = fixq_init(&msg_queue, msg_tbl, EXMSG_MSG_CNT, EXMSG_LOCKER);
    if (err < 0) {
        dbg_error(DBG_MSG, "fixq init failed.");
        return err;
    }

    // 初始化消息块分配器
    err = mblock_init(&msg_block, msg_buffer, sizeof(exmsg_t), EXMSG_MSG_CNT, EXMSG_LOCKER);
    if (err < 0) {
        dbg_error(DBG_MSG, "mblock init failed.");
        return err;
    }

    dbg_info(DBG_MSG, "init done");

    return NET_ERR_OK;
}

/**
 * @brief 接收网卡发来的数据包，并加入消息队列
 */
net_err_t exmsg_netif_in(void) {
    // 由于后续要用中断处理，因此此处不应该等，这样无可避免会出现数据包丢失，属于正常情况，不是协议栈需要去考虑的
    exmsg_t *msg = mblock_alloc(&msg_block, -1);  
    if (!msg) {
        dbg_warning(DBG_MSG, "no free exmsg");
        return NET_ERR_MEM;
    }

    static int id = 0;
    msg->type = NET_EXMSG_NETIF_IN;
    msg->id = id++;

    net_err_t err = fixq_send(&msg_queue, msg, -1);
    if (err < 0) {
        dbg_warning(DBG_MSG, "fixq full");
        mblock_free(&msg_block, msg);
        return err;
    }

    return NET_ERR_OK;
}

static void work_thread(void *arg) {
    dbg_info(DBG_MSG, "exmsg is running...\n");

    while (1) {
        exmsg_t *msg = (exmsg_t *)fixq_recv(&msg_queue, 0);
        if(msg) {
            plat_printf("receive a msg type: %d, id: %d\n", msg->type, msg->id);
            mblock_free(&msg_block, msg);
        }
    }
}

net_err_t exmsg_start(void) {
    sys_thread_t thread = sys_thread_create(work_thread, (void*)0);
    if (thread == SYS_THREAD_INVALID) {
        return NET_ERR_SYS;
    }
}
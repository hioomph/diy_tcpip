/**
 * @brief TCP/IP核心线程通信模块。
 *        此处运行了一个核心线程，所有TCP/IP中相关的事件都交由该线程处理
 */

#include "exmsg.h"
#include "dbg.h"
#include "fixq.h"
#include "ipaddr.h"
#include "mblock.h"
#include "net_err.h"
#include "netif.h"
#include "pktbuf.h"
#include "sys_plat.h"

static void *msg_tbl[EXMSG_MSG_CNT];        // 消息缓冲区
static fixq_t msg_queue;                    // 消息队列

// 通过msg_block从msg_buffer中申请一个消息，写入后再发给msg_queue
// msg_queue处理完毕后，再返回给msg_block
static exmsg_t msg_buffer[EXMSG_MSG_CNT];   // 消息块
static mblock_t msg_block;                  // 消息块分配器


/**
 * @brief 核心线程通信初始化
 */
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
net_err_t exmsg_netif_in(netif_t *netif) {
    // 由于后续要用中断处理，因此此处不应该等，这样无可避免会出现数据包丢失，属于正常情况，不是协议栈需要去考虑的
    exmsg_t *msg = mblock_alloc(&msg_block, -1);  
    if (!msg) {
        dbg_warning(DBG_MSG, "no free exmsg");
        return NET_ERR_MEM;
    }

    msg->type = NET_EXMSG_NETIF_IN;
    msg->netif.netif = netif;

    net_err_t err = fixq_send(&msg_queue, msg, -1);
    if (err < 0) {
        dbg_warning(DBG_MSG, "fixq full");
        mblock_free(&msg_block, msg);
        return err;
    }

    return NET_ERR_OK;
}

/**
 * @brief 网络接口有数据到达时的相关处理
 */
static net_err_t do_netif_in(exmsg_t *msg) {
    netif_t *netif = msg->netif.netif;

    pktbuf_t *buf;
    while ((buf = netif_get_in(netif, -1))) {
        dbg_info(DBG_MSG, "recv a packet");

        // pktbuf_fill(buf, 0x11, 6);
        // net_err_t err = netif_out(netif, (ipaddr_t *)0, buf);
        // /**
        //  * 在netif_out中包含两步：1）将数据包加入就绪队列（输出队列）中；2）启动发送过程。
        //  * 如果数据包已经加入就绪队列中，那么此时若出错，不应该对这个数据包进行释放。只有当
        //  * 数据包还未加入就绪队列时出错，才应该进行释放工作。
        //  * 
        //  * 当err<0时，说明数据包还未交给上层协议栈进行处理，因此在此处进行buf的释放工作；
        //  * 否则，说明数据包已经到达上层协议栈，因此由上层协议栈内部自行负责释放工作。
        //  */
        // if (err < 0) {
        //     pktbuf_free(buf);
        // }

        
    }

    return NET_ERR_OK;
}

/**
 * @brief 核心线程功能
 */
static void work_thread(void *arg) {
    dbg_info(DBG_MSG, "exmsg is running...\n");

    while (1) {
        // 阻塞接收消息
        exmsg_t *msg = (exmsg_t *)fixq_recv(&msg_queue, 0);
        if (msg == (exmsg_t*)0) {
            continue;
        }

        // 打印接收到消息的具体信息
        dbg_info(DBG_MSG, "recieve a msg(%p): %d", msg, msg->type);
        switch (msg->type) {
        case NET_EXMSG_NETIF_IN:          // 网络接口消息
            do_netif_in(msg);
            break;
        }

        // 释放消息
        mblock_free(&msg_block, msg);
    }
}

/**
 * @brief 启动核心线程通信机制
 */
net_err_t exmsg_start(void) {
    sys_thread_t thread = sys_thread_create(work_thread, (void*)0);
    if (thread == SYS_THREAD_INVALID) {
        return NET_ERR_SYS;
    }
}
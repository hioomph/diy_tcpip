/**
 * @file fixq.c
 * @brief 定长消息队列
 *        在整个协议栈中，数据包需要排队，线程之间的通信消息也需要排队，因此需要
 *        借助于消息队列实现。该消息队列长度是定长的，以避免消息数量太多耗费太多资源
 * 
 *        对工作线程来说，消息实际上就是一块内存
 */

#include "fixq.h"
#include "dbg.h"
#include "net_err.h"
#include "nlocker.h"
#include "sys.h"
#include "sys_plat.h"

/**
 * @brief 初始化定长消息队列
 */
net_err_t fixq_init(fixq_t *q, void **buf, int size, nlocker_type_t type) {
    q->size = size;
    q->in = q->out = q->cnt = 0;
    q->buf = (void *)0;
    q->send_sem = SYS_SEM_INVALID;
    q->recv_sem = SYS_SEM_INVALID;

    net_err_t err = nlocker_init(&q->locker, type);
    if (err < 0) {
        dbg_error(DBG_QUEUE, "init locker failed.");
        return err;
    }

    q->send_sem = sys_sem_create(size);
    if (q->send_sem == SYS_SEM_INVALID) {
        dbg_error(DBG_QUEUE, "init send sem failed.");
        goto init_failed;  // 不能直接return，因为需要对已经初始化的锁进行回收
    }

    q->recv_sem = sys_sem_create(size);
    if (q->recv_sem == SYS_SEM_INVALID) {
        dbg_error(DBG_QUEUE, "init recv sem failed.");
        goto init_failed;  // 不能直接return，因为需要对已经初始化的锁进行回收
    }

    q->buf = buf;

    return NET_ERR_OK;

init_failed:
    if (q->recv_sem != SYS_SEM_INVALID) {
        sys_sem_free(q->recv_sem);
    }

    if (q->send_sem != SYS_SEM_INVALID) {
        sys_sem_free(q->send_sem);
    }

    nlocker_destroy(&q->locker);

    return err;
}

/**
 * @brief 向消息队列写入一个消息
 *        如果消息队列满，则看tmo，如果tmo < 0则不等待
 */
net_err_t fixq_send(fixq_t *q, void *msg, int tmo) {
    // 第一组锁定，检查是否满
    nlocker_lock(&q->locker);
    if ((q->cnt >= q->size) && (tmo < 0)) {
        nlocker_unlock(&q->locker);
        return NET_ERR_FULL;
    }
    nlocker_unlock(&q->locker);

    // 等待信号量，有空闲单元写入缓存
    if (sys_sem_wait(q->send_sem, tmo) < 0) {
        return NET_ERR_TMO;
    }

    // 第二组锁定，执行实际的消息发送
    nlocker_lock(&q->locker);
    if (q->cnt >= q->size) {  // 再次检查以防止并发问题
        nlocker_unlock(&q->locker);
        sys_sem_notify(q->send_sem);  // 释放信号量，因为没有使用空闲单元
        return NET_ERR_FULL;
    }

    q->buf[q->in++] = msg;
    if (q->in >= q->size) {
        q->in = 0;
    }
    q->cnt++;
    nlocker_unlock(&q->locker);

    // 通知其它进程消息队列中有消息可用
    sys_sem_notify(q->recv_sem);

    return NET_ERR_OK;
}


/**
 * @brief 从消息队列中取一个消息
 */
void *fixq_recv(fixq_t *q, int tmo) {
    // 第一组锁定，检查当前队列中是否有消息
    nlocker_lock(&q->locker);
    if ((!q->cnt) && (tmo < 0)) {
        nlocker_unlock(&q->locker);
        return (void *)0;
    }
    nlocker_unlock(&q->locker);

    if (sys_sem_wait(q->recv_sem, tmo) < 0) {
        return (void *)0;
    }

    // 第二组锁定，执行实际的取消息
    nlocker_lock(&q->locker);
    if (q->cnt <= 0) {  // 添加计数器检查
        nlocker_unlock(&q->locker);
        sys_sem_notify(q->recv_sem);  // 通知其他线程
        return (void *)0;
    }

    void *msg = q->buf[q->out++];
    if (q->out >= q->size) {
        q->out = 0;
    }
    q->cnt--;
    nlocker_unlock(&q->locker);

    // 通知有消息队列中有空闲空间可用
    sys_sem_notify(q->send_sem);

    return msg;
}

/**
 * @brief 消息队列销毁
*/
void fixq_destroy(fixq_t *q) {
    nlocker_destroy(&q->locker);
    sys_sem_free(q->send_sem);
    sys_sem_free(q->recv_sem);
}

/**
 * @brief 计数消息队列
*/
int fixq_count(fixq_t *q) {
    nlocker_lock(&q->locker);
    int count = q->cnt;
    nlocker_unlock(&q->locker);

    return count;
}
/**
 * @file fixq.h
 * @brief 定长消息队列
 *        在整个协议栈中，数据包需要排队，线程之间的通信消息也需要排队，因此需要
 *        借助于消息队列实现。该消息队列长度是定长的，以避免消息数量太多耗费太多资源
 */

#ifndef _FIXQ_H_
#define _FIXQ_H_

#include "nlocker.h"
#include "sys.h"

typedef struct _fixq_t {
    int size;               // 消息队列空闲单元长度
    int in, out;            // 写入/读取时的索引位置
    int cnt;                // 消息队列的当前消息个数

    void **buf;             // 消息结构数组

    nlocker_t locker;
    sys_sem_t recv_sem;     // 读信号量
    sys_sem_t send_sem;     // 写信号量
}fixq_t;

net_err_t fixq_init(fixq_t *q, void **buf, int size, nlocker_type_t type);
net_err_t fixq_send(fixq_t *q, void *msg, int tmo);
void *fixq_recv(fixq_t *q, int tmo);
void fixq_destroy(fixq_t *q);
int fixq_count(fixq_t *q);

#endif // _FIXQ_H_
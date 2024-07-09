#ifndef _MBLOCK_H_
#define _MBLOCK_H_

#include "nlist.h"
#include "nlocker.h"
#include "sys_plat.h"

typedef struct _mblock_t {
    nlist_t free_list;      // 空闲链表
    void *start;            // 空闲链表起始地址
    nlocker_t locker;       // 锁，用于多线程
    sys_sem_t alloc_sem;    // 用于分配时的信号量
}mblock_t;

net_err_t mblock_init(mblock_t *mblock, void *mem, int blk_size, int cnt, nlocker_type_t share_type);
void *mblock_alloc(mblock_t *mblock, int ms);
int mblock_free_cnt(mblock_t *mblock);

void mblock_free(mblock_t *mblock, void *block);
void mblock_destroy(mblock_t *mblock);

#endif // _MBLOCK_H_
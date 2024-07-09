#include "mblock.h"
#include "nlist.h"
#include "nlocker.h"
#include "sys_plat.h"
#include "dbg.h"

/**
 * @brief 初始化存储块管理器
 *        将mem开始的内存区域划分成多个相同大小的内存块，然后用链表链接起来
 * @param mblock        内存链表
 * @param mem           要建立内存链表的对应数组内存区域
 * @param blk_size      单个内存块的大小
 * @param cnt           表项大小
 * @param share_type    锁类型
*/
net_err_t mblock_init(mblock_t *mblock, void *mem, int blk_size, int cnt, nlocker_type_t lokcer_type) {
    // 链表使用了nlist_node结构，所以大小必须合适
    dbg_assert(blk_size >= sizeof(nlist_node_t), "size error");

    // 将缓存区分割成一块块固定大小内存，插入到队列中
    uint8_t *buf = (uint8_t *)mem;
    nlist_init(&mblock->free_list);
    for (int i=0; i<cnt; i++,buf+=blk_size) {
        nlist_node_t *block = (nlist_node_t *)buf;
        nlist_node_init(block);
        nlist_insert_last(&mblock->free_list, block);
    }

    nlocker_init(&mblock->locker, lokcer_type);
    
    // 涉及多线程时才分配信号量
    // 如果只是线程内部使用，则不需要分配信号量
    if (lokcer_type != NLOCKER_NONE) {
        mblock->alloc_sem = sys_sem_create(cnt);
        if (mblock->alloc_sem == SYS_SEM_INVALID) {
            dbg_error(DBG_MBLOCK, "create sem failed.");
            nlocker_destroy(&mblock->locker);
            return NET_ERR_SYS;
        }
    }

    // mblock->start = mem;
    return NET_ERR_OK;
}

/**
 * @brief 分配一个空闲的存储块
*/
void *mblock_alloc(mblock_t *mblock, int ms) {
    // 不需要等待信号量，查询后直接退出
    // 有两种情况：1）ms < 0；2）此时是用于线程内部的分配
    if ((ms < 0) || (mblock->locker.type == NLOCKER_NONE)) {
        nlocker_lock(&mblock->locker);
        int cnt = nlist_count(&mblock->free_list);
        nlocker_unlock(&mblock->locker);

        if (cnt == 0) {
            // 没有空闲块，直接退出
            return (void *)0;
        }
    }

    // 需要等待信号量
    if (mblock->locker.type != NLOCKER_NONE) {
        sys_sem_wait(mblock->alloc_sem, ms);
    }

    // 获取分配得到的项
    nlocker_lock(&mblock->locker);
    nlist_node_t *block = nlist_remove_first(&mblock->free_list);
    nlocker_unlock(&mblock->locker);
    return block;
}

/**
 * @brief 获取空闲块数量
 */
int mblock_free_cnt(mblock_t *mblock) {
    nlocker_lock(&mblock->locker);
    int cnt = nlist_count(&mblock->free_list);
    nlocker_unlock(&mblock->locker);

    return cnt;
}

/**
 * @brief 释放存储块
*/
void mblock_free(mblock_t *mblock, void *block) {
    nlocker_lock(&mblock->locker);
    // 将要释放的存储块加入空闲链表
    nlist_insert_last(&mblock->free_list, (nlist_node_t *)block);
    nlocker_unlock(&mblock->locker);

    if (mblock->locker.type != NLOCKER_NONE) {
        // 发信号，通知等待的线程有新的内存块
        // 线程则自己通过mblock_alloc中的sys_sem_wait()获取到这个通知
        sys_sem_notify(mblock->alloc_sem);
    }
}

/**
 * @brief 销毁存储管理块
 */
void mblock_destroy(mblock_t *mblock) {
    if (mblock->locker.type != NLOCKER_NONE) {
        sys_sem_free(mblock->alloc_sem);
        nlocker_destroy(&mblock->locker);
    }
}
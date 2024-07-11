#include "pktbuf.h"
#include "dbg.h"
#include "mblock.h"
#include "net_err.h"
#include "nlist.h"
#include "nlocker.h"

static nlocker_t locker;

static pktblk_t block_buffer[PKTBUF_BLK_CNT];
static mblock_t block_list;
static pktbuf_t pktbuf_buffer[PKTBUF_BUF_CNT];
static mblock_t pktbuf_list;

net_err_t pktbuf_init(void) {
    dbg_info(DBG_BUF, "init pktbuf");

    nlocker_init(&locker, NLOCKER_THREAD);
    mblock_init(&block_list, block_buffer, sizeof(pktblk_t), PKTBUF_BLK_CNT, NLOCKER_THREAD);
    mblock_init(&pktbuf_list, pktbuf_buffer, sizeof(pktbuf_t), PKTBUF_BUF_CNT, NLOCKER_THREAD);

    dbg_info(DBG_BUF, "init done");

    return NET_ERR_OK;
}

/**
 * @brief 分配一个空闲的block
 */
static pktblk_t *pktblk_alloc(void) {
    pktblk_t *block = mblock_alloc(&block_list, -1);
    if (block) {
        block->size = 0;
        block->data = (uint8_t *)0;
        nlist_node_init(&block->node);
    }
}

/**
 * @brief 创建分配一个缓冲链（数据块链），返回第一个数据块的地址
 * 
 *        由于分配是以数据块（blk）为单位，所以alloc_size的大小可能会小于实际分配的BUF块的总大小，
 *        那么此时就有一部分空间未用，这部分空间可能放在链表的最开始，也可能放在链表的结尾处存储，
 *        若add_front=1，将新分配的块插入到表头；否则，插入到表尾
*/
static pktblk_t *pktblk_alloc_list(int size, int add_front) {
    pktblk_t *first_blk = (pktblk_t *)0;
    pktblk_t *pre_blk = (pktblk_t *)0;  // 上一次分配的数据块
    int curr_size = 0;

    while (size) {
        // 新分配一个数据块
        pktblk_t *new_blk = pktblk_alloc();
        if (!new_blk) {
            dbg_error(DBG_BUF, "no buffer for alloc(%d)", size);
            // 释放建立当前block前已经创建的其他block
            // block_free();
            
            return (pktblk_t *)0;
        }

        if (add_front) {
            // 头插法
            // 判断要分配的size是否大于单个数据块的大小
            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;
            new_blk->size = curr_size;
            new_blk->data = new_blk->payload + PKTBUF_BLK_CNT - curr_size;
            if (first_blk) {
                nlist_node_set_next(&new_blk->node, &first_blk->node);
            }
        } else {
            // 尾插法
            if (!first_blk) {
                first_blk = new_blk;
            }

            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;
            new_blk->size = curr_size;
            new_blk->data = new_blk->payload;  // 尾插法从payload开始的位置开始放数据
            if (pre_blk) {
                // 设置pre_blk和new_blk的连接关系
                nlist_node_set_next(&pre_blk->node, &new_blk->node);
            }
        }

        size -= curr_size;
        pre_blk = new_blk;
    }

    return first_blk;
}

/**
 * @brief 分配数据包，一个数据包由一串数据块组成
 */
pktbuf_t *pktbuf_alloc(int size) {
    pktblk_alloc_list(size, 0);
    pktblk_alloc_list(size, 1);

    return (pktbuf_t *)0;
}

/**
 * @brief 释放数据包
 */
void pktbuf_free(pktbuf_t *buf);
#include "pktbuf.h"
#include "dbg.h"
#include "mblock.h"
#include "net_err.h"
#include "nlist.h"
#include "nlocker.h"
#include "sys_plat.h"

static nlocker_t locker;

static pktblk_t block_buffer[PKTBUF_BLK_CNT];
static mblock_t block_list;                     // 空闲块列表
static pktbuf_t pktbuf_buffer[PKTBUF_BUF_CNT];
static mblock_t pktbuf_list;                    // 空闲包列表

/**
 * @brief 获取blk的剩余空间大小
 */
static inline int curr_blk_tail_free(pktblk_t* blk) {
    // 总大小 - （头部空闲空间） - （已用区域大小） = blk剩余空间大小
    return PKTBUF_BLK_SIZE - (int)(blk->data - blk->payload) - blk->size;
}

/**
 * @brief 打印缓冲表链、同时检查表链的是否正确配置
 *
 *        在打印的过程中，同时对整个缓存链表进行检查，看看是否存在错误
 *        主要通过检查空间和size的设置是否正确来判断缓存是否正确设置
 *
 * @param buf 待查询的Buf
 */
#if DBG_DISP_ENABLED(DBG_BUF)
static void display_check_buf(pktbuf_t* buf) {
    if (!buf) {
        dbg_error(DBG_BUF, "invalid buf. buf == 0");
        return;
    }

    plat_printf("check buf %p: size %d\n", buf, buf->total_size);
    pktblk_t* curr;
    int total_size = 0, index = 0;
    for (curr = pktbuf_first_blk(buf); curr; curr = pktbuf_blk_next(curr)) {
        plat_printf("%d: ", index++);

        if ((curr->data < curr->payload) || (curr->data >= curr->payload + PKTBUF_BLK_SIZE)) {
            dbg_error(DBG_BUF, "bad block data. data=%p, payload=%p\n", curr->data, curr->payload);
        }


        // 开头可能存在的未用区域（从payload的起始地址到已用区域的起始地址）
        int head_size = (int)(curr->data - curr->payload);
        plat_printf("Head Free: %d b, ", head_size);

        // 中间存在的已用区域
        int used_size = curr->size;
        plat_printf("Used: %d b, ", used_size);

        // 末尾可能存在的未用区域（从已用区域的末端地址到payload的末端地址）
        int tail_size = curr_blk_tail_free(curr);
        plat_printf("Tail Free: %d b, ", tail_size);
        plat_printf("\n");

        // 检查当前计算所得的总和是否与PKTBUF_BLK_SIZE一致
        int blk_total = head_size + used_size + tail_size;
        if (blk_total != PKTBUF_BLK_SIZE) {
            dbg_error(DBG_BUF,"bad block size. %d != %d", blk_total, PKTBUF_BLK_SIZE);
        }

        // 累计总的大小
        total_size += used_size;
    }

    // 检查总的大小是否一致
    if (total_size != buf->total_size) {
        dbg_error(DBG_BUF,"bad buf size. %d != %d", total_size, buf->total_size);
    }
}
#else
#define display_check_buf(buf)
#endif

net_err_t pktbuf_init(void) {
    dbg_info(DBG_BUF, "init pktbuf");

    nlocker_init(&locker, NLOCKER_THREAD);
    mblock_init(&block_list, block_buffer, sizeof(pktblk_t), PKTBUF_BLK_CNT, NLOCKER_THREAD);
    mblock_init(&pktbuf_list, pktbuf_buffer, sizeof(pktbuf_t), PKTBUF_BUF_CNT, NLOCKER_THREAD);

    dbg_info(DBG_BUF, "init done");

    return NET_ERR_OK;
}

/**
 * @brief 在空闲块列表中分配一个空闲的数据块
 */
static pktblk_t *pktblk_alloc(void) {
    // 不等待分配，因为会在中断中调用
    nlocker_lock(&locker);
    pktblk_t* blk = mblock_alloc(&block_list, -1);
    nlocker_unlock(&locker);

    if (blk) {
        blk->size = 0;
        blk->data = (uint8_t *)0;
        nlist_node_init(&blk->node);
    }

    return blk;
}

/**
 * @brief 释放数据块链
 */
static void pktblk_free_list(pktblk_t *first_blk) {
    while (first_blk) {
        // 先取下一个
        pktblk_t* next_blk = pktbuf_blk_next(first_blk);

        // 然后释放
        mblock_free(&block_list, first_blk);

        // 然后调整当前的处理对像
        first_blk = next_blk;
    }
}

/**
 * @brief 创建分配一个缓冲链（数据块链），返回第一个数据块的地址
 * 
 *        由于分配是以数据块（blk）为单位，所以alloc_size的大小可能会小于实际分配的数据块的总大小，
 *        那么此时就有一部分空间未用，这部分空间可能放在链表的最开始，也可能放在链表的结尾处存储，
 *        若add_front=1，将新分配的块插入到表头；否则，插入到表尾
 * 
 * @return 返回开头的数据块
*/
static pktblk_t *pktblk_alloc_list(int size, int add_front) {
    pktblk_t *first_blk = (pktblk_t *)0;
    pktblk_t *pre_blk = (pktblk_t *)0;  // 上一次分配的数据块
    
    while (size) {
        // 新分配一个数据块
        pktblk_t *new_blk = pktblk_alloc();
        if (!new_blk) {
            dbg_error(DBG_BUF, "no buffer for alloc(%d)", size);

            // 释放建立当前block前已经创建的其他block
            nlocker_lock(&locker);
            // pktblock_free_list(first_blk);
            nlocker_unlock(&locker);
            
            return (pktblk_t *)0;
        }

        int curr_size = 0;
        if (add_front) {
            // 头插法
            // 判断要分配的size是否大于单个数据块的大小
            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;

            // 反向分配，从末端往前分配空间
            new_blk->size = curr_size;
            new_blk->data = new_blk->payload + PKTBUF_BLK_SIZE - curr_size;
            // 调试信息
            // plat_printf("Allocated block: payload=%p, data=%p\n", new_blk->payload, new_blk->data);
            if (first_blk) {
                nlist_node_set_next(&new_blk->node, &first_blk->node);
            }

            // 如果是反向分配，第一个包总是当前分配的包
            first_blk = new_blk;
        } else {
            // 尾插法
            // 如果是正向分配，第一个包是第一个分配的包
            if (!first_blk) {
                first_blk = new_blk;
            }

            curr_size = size > PKTBUF_BLK_SIZE ? PKTBUF_BLK_SIZE : size;

            // 正向分配，从前端向末端分配空间
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
 * @brief 将数据块链插入到数据包中
 */
static void pktbuf_insert_blk_list(pktbuf_t *buf, pktblk_t *first_blk, int add_front) {
    if (!add_front) {
        // 尾插法
        while (first_blk) {
            // 保存原first_blk
            pktblk_t *next_blk = pktbuf_blk_next(first_blk);

            // 将first_blk插入到尾部
            nlist_insert_last(&buf->blk_list, &first_blk->node);
            buf->total_size += first_blk->size;  // 加上first_blk的实际大小，即有效空间

            // 更新first_blk
            first_blk = next_blk;
        }
    } else {
        // 头插法
        pktblk_t *pre = (pktblk_t *)0;

        // 逐个取头部blk依次插入
        while (first_blk) {
            pktblk_t *next_blk = pktbuf_blk_next(first_blk);

            if (pre) {
                // 在要插入的数据块前有已经插入的其他数据块，即pre
                nlist_insert_after(&buf->blk_list, &pre->node, &first_blk->node);
            } else {
                // 在在要插入的数据块前没有已经插入的其他数据块，直接在链表头结点插入
                nlist_insert_first(&buf->blk_list, &first_blk->node);
            }
            buf->total_size += first_blk->size;

            pre = first_blk;
            first_blk = next_blk;
        };
    }
}

/**
 * @brief 分配数据包，一个数据包由一串数据块组成
 */
pktbuf_t *pktbuf_alloc(int size) {
    // 分配一个数据包
    nlocker_lock(&locker);
    pktbuf_t* buf = mblock_alloc(&pktbuf_list, -1);
    nlocker_unlock(&locker);
    if (!buf) {
        dbg_error(DBG_BUF, "no buffer");
        return (pktbuf_t *)0;
    }

    buf->total_size = 0;
    nlist_init(&buf->blk_list);
    nlist_node_init(&buf->node);

    if (size) {
        pktblk_t *blk = pktblk_alloc_list(size, 1);
        if (!blk) {
            mblock_free(&pktbuf_list, buf);
            return (pktbuf_t *)0;
        } 

        pktbuf_insert_blk_list(buf, blk, 1);
    }

    display_check_buf(buf);
    return buf;
}

/**
 * @brief 释放数据包
 */
void pktbuf_free(pktbuf_t *buf) {
    pktblk_free_list(pktbuf_first_blk(buf));
    mblock_free(&pktbuf_list, buf);
}

/**
 * @brief 为数据包增加一个包头
 * 
 * @param cont  cont为1代表添加连续包头；cont为0代表添加非连续包头
*/
net_err_t pkybuf_add_header(pktbuf_t *buf, int size, int cont) {
    pktblk_t *blk = pktbuf_first_blk(buf);

    int recv_size = (int)(blk->data - blk->payload);

    // 头部有足够的空间可以放包头
    if (size <= recv_size) {
        blk->size += size;
        blk->data -= size;
        buf->total_size += size;

        display_check_buf(buf);
        return NET_ERR_OK;
    }

    // 头部没有足够的空间可以放包头，此时根据cont判断是否需要连续空间
    if (cont) {
        // 分配连续包头

        // 包头数据长度超过数据块长度，无法分配
        if (size > PKTBUF_BLK_SIZE) {
            dbg_error(DBG_BUF, "set cont, size too big: %d > %d", size, PKTBUF_BLK_SIZE);
            return NET_ERR_SIZE;
        }

        blk = pktblk_alloc_list(size, 1);
        if (!blk) {
            dbg_error(DBG_BUF, "no buffer (size %d)", size);
            return NET_ERR_NONE;
        }
    } else {
        // 分配非连续包头
    }

    pktbuf_insert_blk_list(buf, blk, 1);
    display_check_buf(buf);
    
    return NET_ERR_OK;
}
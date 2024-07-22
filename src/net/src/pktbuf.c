/**
 * @brief 网络数据包分配管理实现
 *
 * 从网络上接收到的数据帧是变成的，用户要发送的数据也是变成的。所以需要一种
 * 灵活的方式来适应这种变长，以避免需要使用一个超大的内存块来保存数据，减少
 * 空间的浪费这里提供的是一种基于链式的存储方式，将原本需要一大块内存存储的
 * 数据，分块存储在多个数据块中数据量少时，需要的块就少；数据量大时，需要的
 * 块就多；提高了存储利用率。
 */

#include "pktbuf.h"
#include "dbg.h"
#include "mblock.h"
#include "net_err.h"
#include "nlist.h"
#include "nlocker.h"
#include "sys_plat.h"
#include <winnt.h>
#include <winuser.h>

static nlocker_t locker;

static pktblk_t block_buffer[PKTBUF_BLK_CNT];
static mblock_t block_list;                     // 空闲块列表
static pktbuf_t pktbuf_buffer[PKTBUF_BUF_CNT];
static mblock_t pktbuf_list;                    // 空闲包列表

/**
 * @brief 获取以当前位置而言，余下多少总共有效的数据空间
 */
static inline int total_blk_remain(pktbuf_t *buf) {
    return buf->total_size - buf->pos;
}

/**
 * @brief 获取在当前的blk中余下多少数据空间
 */
static int curr_blk_remain(pktbuf_t *buf) {
    pktblk_t* blk = buf->curr_blk;
    if (!blk) {
        return 0;
    }

    return (int)(buf->curr_blk->data + blk->size - buf->blk_offset);
}

/**
 * @brief 获取blk的剩余空间大小
 */
static inline int curr_blk_tail_free(pktblk_t *blk) {
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
static void display_check_buf(pktbuf_t *buf) {
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
 * @brief 释放数据块
 */
static void pktblk_free(pktblk_t *blk) {
    nlocker_lock(&locker);
    mblock_free(&block_list, blk);
    nlocker_unlock(&locker);
}

/**
 * @brief 释放数据块链，即数据包
 */
static void pktblk_free_list(pktblk_t *first_blk) {
    while (first_blk) {
        // 先取下一个
        pktblk_t* next_blk = pktbuf_blk_next(first_blk);

        // 然后释放
        pktblk_free(first_blk);

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
            pktblk_free_list(first_blk);
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

    buf->ref = 1;
    buf->total_size = 0;
    nlist_init(&buf->blk_list);
    nlist_node_init(&buf->node);

    if (size) {
        pktblk_t *blk = pktblk_alloc_list(size, 1);
        if (!blk) {
            nlocker_lock(&locker);
            mblock_free(&pktbuf_list, buf);
            nlocker_unlock(&locker);
            return (pktbuf_t *)0;
        } 

        pktbuf_insert_blk_list(buf, blk, 0);
    }

    pktbuf_reset_acc(buf);
    display_check_buf(buf);
    return buf;
}

/**
 * @brief 释放数据包
 */
void pktbuf_free(pktbuf_t *buf) {
    nlocker_lock(&locker);
    if (--buf->ref == 0) {
        pktblk_free_list(pktbuf_first_blk(buf));
        mblock_free(&pktbuf_list, buf);
    }
    nlocker_unlock(&locker);
}

/**
 * @brief 为数据包增加包头
 * 
 * @param cont  cont为1代表添加连续包头；cont为0代表添加非连续包头
 */
net_err_t pktbuf_add_header(pktbuf_t *buf, int size, int cont) {
    dbg_assert(buf->ref != 0, "buf freed");

    pktblk_t *blk = pktbuf_first_blk(buf);

    // 当前数据块链的第一个数据块可以存放空余数据的空间
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

        // 分配一个新的数据块用于放包头数据
        blk = pktblk_alloc_list(size, 1);
        if (!blk) {
            dbg_error(DBG_BUF, "no buffer (size %d)", size);
            return NET_ERR_NONE;
        }
    } else {
        // 分配非连续包头
        blk->data = blk->payload;
        blk->size += recv_size;
        buf->total_size += recv_size;
        size -= recv_size;

        blk = pktblk_alloc_list(size, 1);
        if (!blk) {
            dbg_error(DBG_BUF, "no buffer (size %d)", size);
            return NET_ERR_NONE;
        }
    }

    pktbuf_insert_blk_list(buf, blk, 1);
    display_check_buf(buf);
    
    return NET_ERR_OK;
}

/**
 * @brief 为数据包移除包头
 */
net_err_t pktbuf_remove_header(pktbuf_t *buf, int size) {
    dbg_assert(buf->ref != 0, "buf freed");

    pktblk_t *blk = pktbuf_first_blk(buf);

    while (size) {
        pktblk_t *next_blk = pktbuf_blk_next(blk);

        // 判断当前数据块的容量（size）是否大于要移除的包头的size
        if (size < blk->size) {
            // 数据块容量更大，直接移除
            blk->data += size;
            blk->size -= size;
            buf->total_size -= size;
            break;
        }

        // 数据块容量更小，则只能先移除当前容量的部分
        int curr_size = blk->size;
        
        // 随后删除该数据块，并更新到下一个数据块上
        nlist_remove_first(&buf->blk_list);
        pktblk_free(blk);

        // 更新size
        size -= curr_size;
        buf->total_size -= curr_size;

        blk = next_blk;
    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

/**
 * @brief 调整包大小
 */
net_err_t pktbuf_resize(pktbuf_t *buf, int to_size) {
    dbg_assert(buf->ref != 0, "buf freed");

    if (to_size == buf->total_size) {
        return NET_ERR_OK;
    }

    if (buf->total_size == 0) {
        pktblk_t *blk = pktblk_alloc_list(to_size, 0);
        if (!blk) {
            dbg_error(DBG_BUF, "no block");
            return NET_ERR_MEM;
        }

        pktbuf_insert_blk_list(buf, blk, 0);
    } else if (to_size == 0) {
        pktblk_free_list(pktbuf_first_blk(buf));
        buf->total_size = 0;
        nlist_init(&buf->blk_list);
    } else if (to_size > buf->total_size) {
        // 包扩大
        pktblk_t *tail_blk = pktbuf_last_blk(buf);
        // 判断数据块链的尾部数据块，其剩余空间大小是否可以放下需要扩充的那部分大小
        int inc_size = to_size - buf->total_size;
        int remain_size = curr_blk_tail_free(tail_blk);
        if (inc_size <= remain_size) {
            // 能放下
            tail_blk->size += inc_size;
            buf->total_size += inc_size;
        } else {
            // 放不下
            // 分配一个新的数据块链用来放置多出来的空间
            pktblk_t *new_blks = pktblk_alloc_list(inc_size - remain_size, 0);
            if (!new_blks) {
                dbg_error(DBG_BUF, "no block");
                return NET_ERR_MEM;                
            }

            tail_blk->size += remain_size;
            buf->total_size += remain_size;
            pktbuf_insert_blk_list(buf, new_blks, 0);
        }
    } else {
        // 包缩小
        int remove_size = 0;

        // 遍历数据块链，找到要保留的最后一个数据块
        pktblk_t *tail_blk;
        for (tail_blk = pktbuf_first_blk(buf); tail_blk; tail_blk = pktbuf_blk_next(tail_blk)) {
            remove_size += tail_blk->size;
            if (remove_size >= to_size) {
                break;
            }
        }

        if (tail_blk == (pktblk_t *)0) {
            return NET_ERR_SIZE;
        }

        // 删除tail_blk之后的剩余数据块，并统计剩余数据块的空间大小
        remove_size = 0;
        pktblk_t *curr_blk = pktbuf_blk_next(tail_blk);
        while (curr_blk) {
            pktblk_t *next_blk = pktbuf_blk_next(curr_blk);

            remove_size += curr_blk->size;
            nlist_remove(&buf->blk_list, &curr_blk->node);
            pktblk_free(curr_blk);

            curr_blk = next_blk;
        }

        // 更新尾部数据块的size
        // 原buf的total_size(buf->total_size) = 删除的总size + 要缩小到的size(to_size)
        // 删除的总size = 删除的完整数据块的总size(remove_size) + tail_blk要减去的size
        tail_blk->size -= buf->total_size - remove_size - to_size;
        buf->total_size = to_size;
    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

/**
 * @brief 将src拼到dst的尾部，组成一个buf链
 */
net_err_t pktbuf_join(pktbuf_t *dst, pktbuf_t *src) {
    dbg_assert(dst->ref != 0, "buf freed");
    dbg_assert(src->ref != 0, "buf freed");

    // 从src的块链中逐个取数据块，插入到dst的尾部
    pktblk_t *first;
    while ((first = pktbuf_first_blk(src))) {
        // 从src中移除首块
        nlist_remove_first(&src->blk_list);

        // 插入到块链表中
        pktbuf_insert_blk_list(dst, first, 0);
    }

    pktbuf_free(src);

    dbg_info(DBG_BUF,"join result:");
    display_check_buf(dst);
    return NET_ERR_OK;
}

/**
 * @brief 将包的最开始size个字节，配置成连续空间
 *        常用于对包头进行解析时，或者有其它选项字节时
 */
net_err_t pktbuf_set_cont(pktbuf_t *buf, int size) {
    dbg_assert(buf->ref != 0, "buf freed");

    // 必须要有足够的长度
    if (size > buf->total_size) {
        dbg_error(DBG_BUF,"size(%d) > total_size(%d)", size, buf->total_size);
        return NET_ERR_SIZE;
    }

    // 超过1个POOL的大小，返回错误
    if (size > PKTBUF_BLK_SIZE) {
        dbg_error(DBG_BUF,"size too big > %d", PKTBUF_BLK_SIZE);
        return NET_ERR_SIZE;
    }

    // 包头已经处于连续空间，不用处理
    pktblk_t * first_blk = pktbuf_first_blk(buf);
    if (size <= first_blk->size) {
        display_check_buf(buf);
        return NET_ERR_OK;
    }

    // 先将第一个blk中的数据挪动到起始处，以在尾部腾出size空间
#if 0
    uint8_t * dest = first_blk->payload + PKTBUF_BLK_SIZE - size;
    plat_memmove(dest, first_blk->data, first_blk->size);   // 注意处理内存重叠
    first_blk->data = dest;
    dest += first_blk->size;          // 指向下一块复制的目的地
#else
    uint8_t * dest = first_blk->payload;
    for (int i=0; i < first_blk->size; i++) {
        *dest++ = first_blk->data[i];
    }
    first_blk->data = first_blk->payload;
#endif

    // 再依次将后续的空间挪动到buf中，直到buf中的大小达到size
    int remain_size = size - first_blk->size;
    pktblk_t * curr_blk = pktbuf_blk_next(first_blk);
    while (remain_size && curr_blk) {
        // 计算本次移动的数据量
        int curr_size = (curr_blk->size > remain_size) ? remain_size : curr_blk->size;

        // 将curr中的数据，移动到buf中
        plat_memcpy(dest, curr_blk->data, curr_size);
        dest += curr_size;
        curr_blk->data += curr_size;
        curr_blk->size -= curr_size;
        first_blk->size += curr_size;
        remain_size -= curr_size;

        // 复制完后，若当前数据块没有数据了，则释放该块，并指向下一个数据块
        if (curr_blk->size == 0) {
            pktblk_t* next_blk = pktbuf_blk_next(curr_blk);

            nlist_remove(&buf->blk_list, &curr_blk->node);
            pktblk_free(curr_blk);

            curr_blk = next_blk;
        }
    }

    display_check_buf(buf);
    return NET_ERR_OK;
}

/**
 * @brief 准备数据包的读写
 */
void pktbuf_reset_acc(pktbuf_t *buf) {
    if (buf) {
        buf->pos = 0;
        buf->curr_blk = pktbuf_first_blk(buf);
        buf->blk_offset = buf->curr_blk ? buf->curr_blk->data : (uint8_t*)0;
    }
}

/**
 * @brief 前移位置. 如果跨越一个数据块，则移动到下一个数据块的开头
 */
static void move_forward(pktbuf_t *buf, int size) {
    pktblk_t* curr_blk = buf->curr_blk;

    // 调整读写位置
    buf->pos += size;
    buf->blk_offset += size;

    // 判断前移的位置是否超出当前数据块
    if (buf->blk_offset >= curr_blk->data + curr_blk->size) {
        // 超出当前数据块，则更新到下一个数据块的开头
        buf->curr_blk = pktbuf_blk_next(curr_blk);
        if (buf->curr_blk) {
            buf->blk_offset = buf->curr_blk->data;
        } else {
            // 若下一个数据块为空，则赋值为空
            buf->blk_offset = (uint8_t*)0;
        }
    }
}

/**
 * @brief 将src中的数据写入到buf中
 * 
 * @param size 要写入的数据大小
 */
net_err_t pktbuf_write(pktbuf_t *buf, uint8_t *src, int size) {
    dbg_assert(buf->ref != 0, "buf freed");

    if (!src || !size) {
        return NET_ERR_PARAM;
    }

    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        // 剩余空间不足以写入数据
        dbg_error(DBG_BUF, "size error: %d < %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    while (size) {
        // 获取当前blk可写入的数据大小
        int blk_size = curr_blk_remain(buf);
        // 和size比较，更新当前实际可写入的大小
        int copy_size = size > blk_size ? blk_size : size;
        plat_memcpy(buf->blk_offset, src, copy_size);

        // 写入数据后，在数据块中前移
        move_forward(buf, copy_size);
        src += copy_size;
        size -= copy_size;
    }

    return NET_ERR_OK;
}

/**
 * @brief 从当前位置读取指定的数据量
 *
 * @param buf       buf读写器
 * @param read_to   数据存储的起始位置
 * @param size      总共要读取的数据大小
 */
net_err_t pktbuf_read(pktbuf_t *buf, uint8_t *read_to, int size) {
    dbg_assert(buf->ref != 0, "buf freed");

    if (!read_to || !size) {
        return NET_ERR_OK;
    }

    // 判断剩余空间是否足以写入数据
    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        dbg_error(DBG_BUF, "size error: %d < %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    // 循环读取所有数据
    while (size > 0) {
        int blk_size = curr_blk_remain(buf);
        int curr_copy = size > blk_size ? blk_size : size;
        plat_memcpy(read_to, buf->blk_offset, curr_copy);

        // 若超出当前数据块，则移动到下一个数据块
        move_forward(buf, curr_copy);

        read_to += curr_copy;
        size -= curr_copy;
    }

    return NET_ERR_OK;
}

/**
 * @brief 移动当前buf的读写位置pos到offset偏移处
 */
net_err_t pktbuf_seek(pktbuf_t *buf, int offset) {
    dbg_assert(buf->ref != 0, "buf freed");

    // 若当前buf的读写位置和offset相同，直接跳过
    if (buf->pos == offset) {
        return NET_ERR_OK;
    }

    // 若offset不合法或offset超出总大小，无法移动
    if ((offset < 0) || (offset >= buf->total_size)) {
        return NET_ERR_SIZE;
    }

    int move_bytes;
    if (offset < buf->pos) {
        // 从当前pos开始往前移动
        // 比较麻烦，所以定位到buf的最开头再开始移动
        buf->curr_blk = pktbuf_first_blk(buf);
        buf->blk_offset = buf->curr_blk->data;
        buf->pos = 0;

        move_bytes = offset;
    } else {
        // 从当前pos开始往后移动，计算移动的字节量
        move_bytes = offset - buf->pos;
    }

    // 不断移动位置，在移动过程中主要调整total_offset、buf_offset和curr
    // offset的值可能大于余下的空间，此时只移动部分，但是仍然正确
    while (move_bytes) {
        int remain_size = curr_blk_remain(buf);
        int curr_move = move_bytes > remain_size ? remain_size : move_bytes;

        // 往前移动，可能会超出当前的buf
        move_forward(buf, curr_move);
        move_bytes -= curr_move;
    }

    return NET_ERR_OK;
}

/**
 * @brief 在buf之间复制数据，如果空间够，失败
 * @param dest_rw   目标buf读写器
 * @param src_rw    源buf读写器
 */
net_err_t pktbuf_copy(pktbuf_t *dest, pktbuf_t *src, int size) {
    dbg_assert(src->ref != 0, "buf freed");
    dbg_assert(dest->ref != 0, "buf freed");

    // 检查是否能进行拷贝
    if ((total_blk_remain(dest) < size) || (total_blk_remain(src) < size)) {
        return NET_ERR_SIZE;
    }

    // 进行实际的拷贝工作
    while (size) {
        // 在size、以及buf中当前块剩余大小三者中取最小的值
        int dest_remain = curr_blk_remain(dest);
        int src_remain = curr_blk_remain(src);
        int copy_size = dest_remain > src_remain ? src_remain : dest_remain;
        copy_size = copy_size > size ? size : copy_size;

        // 复制数据
        plat_memcpy(dest->blk_offset, src->blk_offset, copy_size);

        move_forward(dest, copy_size);
        move_forward(src, copy_size);
        size -= copy_size;
    }

    return NET_ERR_OK;
}

/**
 * @brief 向buf中当前位置连续填充size个字节值v
 *        上述操作，同pktbuf_write
 */
net_err_t pktbuf_fill(pktbuf_t *buf, uint8_t v, int size) {
    dbg_assert(buf->ref != 0, "buf freed");

    if (!size) {
        return NET_ERR_PARAM;
    }

    // 看看是否够填充
    int remain_size = total_blk_remain(buf);
    if (remain_size < size) {
        dbg_error(DBG_BUF, "size errorL %d < %d", remain_size, size);
        return NET_ERR_SIZE;
    }

    // 循环写入所有数据
    while (size > 0) {
        int blk_size = curr_blk_remain(buf);

        // 判断当前写入的量
        int curr_fill = size > blk_size ? blk_size : size;
        plat_memset(buf->blk_offset, v, curr_fill);

        // 移动指针
        move_forward(buf, curr_fill);
        size -= curr_fill;
    }

    return NET_ERR_OK;
}

/**
 * @brief 增加buf的引用次数
 * @param buf
 */
void pktbuf_inc_ref (pktbuf_t *buf) {
    nlocker_lock(&locker);
    buf->ref++;
    nlocker_unlock(&locker);
}
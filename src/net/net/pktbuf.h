/**
 * pktbuf - packet_buffer
*/

#ifndef _PKTBUF_H_
#define _PKTBUF_H_

#include "net_err.h"
#include "nlist.h"
#include "net_cfg.h"
#include <stdint.h>

// 数据块
typedef struct _pktblk_t {
    nlist_node_t node;                  // 指向下一个数据块
    int size;                           // 数据块大小
    uint8_t *data;                      // 当前读写位置
    uint8_t payload[PKTBUF_BLK_SIZE];   // 数据缓冲区
}pktblk_t;

// 数据包
typedef struct _pktbuf_t {
    int total_size;         // 所有数据块中的总数据大小
    nlist_t blk_list;       // 数据块链
    nlist_node_t node;      // 指向下一个数据包

    // 读写相关
    int ref;                // 引用计数
    int pos;                // 当前位置总的偏移量
    pktblk_t *curr_blk;     // 当前指向的数据块
    uint8_t *blk_offset;    // 在当前数据块中的偏移量
}pktbuf_t;

/**
 * @brief 获取当前数据缓存（buf)的下一个数据块（blk）
 */
static inline pktblk_t * pktbuf_blk_next(pktblk_t *blk) {
    nlist_node_t * next = nlist_node_next(&blk->node);

    return nlist_entry(next, pktblk_t, node);
}

/**
 * @brief 取buf的第一个数据块
 * 
 * @param buf 数据缓存buf
 * @return 第一个数据块
 */
static inline pktblk_t * pktbuf_first_blk(pktbuf_t *buf) {
    nlist_node_t * first = nlist_first(&buf->blk_list);
    return nlist_entry(first, pktblk_t, node);
}

/**
 * @brief 取buf的最后一个数据块
 * 
 * @param buf buf 数据缓存buf
 * @return 最后一个数据块
 */
static inline pktblk_t * pktbuf_last_blk(pktbuf_t *buf) {
    nlist_node_t * first = nlist_last(&buf->blk_list);
    return nlist_entry(first, pktblk_t, node);
}

/**
 * @brief 取数据包的总大小
 */
static int inline pktbuf_total(pktbuf_t *buf) {
    return buf->total_size;
}

/**
 * @brief 返回buf的数据区起始指针
 */
static inline uint8_t * pktbuf_data (pktbuf_t * buf) {
    pktblk_t * first = pktbuf_first_blk(buf);
    return first ? first->data : (uint8_t *)0;
}

net_err_t pktbuf_init(void);
pktbuf_t *pktbuf_alloc(int size);
void pktbuf_free(pktbuf_t *buf);

net_err_t pktbuf_add_header(pktbuf_t *buf, int size, int cont);
net_err_t pktbuf_remove_header(pktbuf_t *buf, int size);
net_err_t pktbuf_resize(pktbuf_t *buf, int to_size);
net_err_t pktbuf_join(pktbuf_t *dst, pktbuf_t *src);

void pktbuf_reset_acc(pktbuf_t* buf);
net_err_t pktbuf_write(pktbuf_t *buf, uint8_t *src, int size);
net_err_t pktbuf_read(pktbuf_t *buf, uint8_t *dest, int size);
net_err_t pktbuf_seek(pktbuf_t *buf, int offset);
net_err_t pktbuf_copy(pktbuf_t *dest, pktbuf_t *src, int size);
net_err_t pktbuf_fill(pktbuf_t *buf, uint8_t v, int size);
void pktbuf_inc_ref (pktbuf_t *buf);

#endif // _PKTBUF_H_
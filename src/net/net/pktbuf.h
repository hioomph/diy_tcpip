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
    int total_size;         // 所有数据包中的总数据大小
    nlist_t blk_list;       // 数据块链
    nlist_node_t node;      // 指向下一个数据包
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
 * @param buf 数据缓存buf
 * @return 第一个数据块
 */
static inline pktblk_t * pktbuf_first_blk(pktbuf_t * buf) {
    nlist_node_t * first = nlist_first(&buf->blk_list);
    return nlist_entry(first, pktblk_t, node);
}

net_err_t pktbuf_init(void);
pktbuf_t *pktbuf_alloc(int size);
void pktbuf_free(pktbuf_t *buf);

net_err_t pkybuf_add_header(pktbuf_t *buf, int size, int cont);

#endif // _PKTBUF_H_
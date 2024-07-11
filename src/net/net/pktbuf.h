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
    nlist_node_t node;      // 指向下一个数据块
    int size;               // 数据块大小
    uint8_t *data;          // 指向payload中有效字节的起始地址
    uint8_t payload[PKTBUF_BLK_SIZE];
}pktblk_t;

// 数据包
typedef struct _pktbuf_t {
    int total_size;         // 所有数据包中的总数据大小
    nlist_t blk_list;
    nlist_node_t node;
}pktbuf_t;

net_err_t pktbuf_init(void);
pktbuf_t *pktbuf_alloc(int size);
void pktbuf_free(pktbuf_t *buf);

#endif // _PKTBUF_H_
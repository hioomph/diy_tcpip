#ifndef _EXMSG_H_
#define _EXMSG_H_

#include "net_err.h"
#include "nlist.h"

typedef struct _exmsg_t {
    // 消息类型
    enum {
        NET_EXMSG_NETIF_IN,     // 网络接口数据消息
    }type;

    nlist_node_t temp;          // 临时使用
    int id;                     // 临时使用
}exmsg_t;

net_err_t exmsg_init(void);
net_err_t exmsg_start(void);
net_err_t exmsg_netif_in(void);


#endif // _EXMSG_H_
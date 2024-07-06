/**
 * @brief 协议栈初始化及启动
 */

#ifndef _NET_H_
#define _NET_H_

#include "net_err.h"

net_err_t net_init(void);
net_err_t net_start(void);

#endif // _NET_H_
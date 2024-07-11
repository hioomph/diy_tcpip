#ifndef _NET_ERR_H_
#define _NET_ERR_H_

typedef enum _net_err_t {
    NET_ERR_OK = 0,     // 无错误
    NET_ERR_SYS = -1,   // 系统错误
    NET_ERR_MEM = -2,   // 内存错误
    NET_ERR_FULL = -3,  // 缓存已满错误
    NET_ERR_TMO = -4,   // 超时错误
}net_err_t;

#endif // _NET_ERR_H_
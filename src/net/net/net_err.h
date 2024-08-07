#ifndef _NET_ERR_H_
#define _NET_ERR_H_

typedef enum _net_err_t {
    NET_ERR_OK = 0,         // 无错误
    NET_ERR_SYS = -1,       // 系统错误
    NET_ERR_MEM = -2,       // 内存错误
    NET_ERR_FULL = -3,      // 缓存已满错误
    NET_ERR_TMO = -4,       // 超时错误
    NET_ERR_SIZE = -5,      // 分配大小错误
    NET_ERR_NONE = -6,      // 分配块错误
    NET_ERR_PARAM = -7,     // 参数错误
    NET_ERR_STATE = -8,     // 状态错误
    NET_ERR_IO = -9,        // 输入输出错误
    NET_ERR_EXIST = 10,     // 已存在错误
}net_err_t;

#endif // _NET_ERR_H_
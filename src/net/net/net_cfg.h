/**
 * @file net_cfg.h
 * @brief 协议栈的配置文件
 * 
 * 所有的配置项，都使用了类似#ifndef的形式，用于实现先检查appcfg.h有没有预先定义。
 * 如果有的话，则优先使用。否则，就使用该文件中的缺省配置。
 */

#ifndef _NET_CFG_H_
#define _NET_CFG_H_

// 调试信息输出
#define DBG_MBLOCK		    DBG_LEVEL_INFO			// 内存块管理器
#define DBG_QUEUE           DBG_LEVEL_INFO          // 定长存储块
#define DBG_MSG             DBG_LEVEL_INFO          // 消息通信
#define DBG_BUF             DBG_LEVEL_INFO          // 数据包管理器
#define DBG_PLAT            DBG_LEVEL_INFO          // 系统平台
#define DBG_INIT            DBG_LEVEL_INFO          // 系统初始化

#define EXMSG_MSG_CNT       10                      // 消息缓冲区大小
#define EXMSG_LOCKER        NLOCKER_THREAD           // 核心线程的锁类型

#define PKTBUF_BLK_SIZE     128                     // 数据包中每一块的大小
#define PKTBUF_BLK_CNT      100                     // 数据包中块的总数量
#define PKTBUF_BUF_CNT      100                     // 数据包的总数量

#endif // _NET_CFG_H_
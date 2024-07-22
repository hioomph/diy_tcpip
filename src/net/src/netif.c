/**
 * @file netif.c
 * @brief 网络接口层代码
 * 
 * 该接口层代码负责将所有的网络接口统一抽象为netif结构，并提供统一的接口进行处理
 *
 * 
 */

#include "netif.h"
#include "dbg.h"
#include "fixq.h"
#include "mblock.h"
#include "net_err.h"
#include "nlist.h"
#include "pktbuf.h"
#include "sys_plat.h"
#include "exmsg.h"

static netif_t netif_buffer[NETIF_DEV_CNT];     // 整个系统所支持的、可供分配的网络接口
static mblock_t netif_mblock;                   // 网络接口分配结构
static nlist_t netif_list;                      // 放置整个系统中已经打开的网络接口
static netif_t *netif_default;                  // 缺省的网络接口

static const link_layer_t *link_layers[NETIF_TYPE_SIZE];  // 当前协议栈支持的链路层结构

/**
 * @brief 显示系统中的网卡列表信息
 */
#if DBG_DISP_ENABLED(DBG_NETIF)
void display_netif_list (void) {
    nlist_node_t * node;

    plat_printf("netif list:\n");
    nlist_for_each(node, &netif_list) {
        netif_t * netif = nlist_entry(node, netif_t, node);
        plat_printf("%s:", netif->name);
        switch (netif->state) {
            case NETIF_CLOSED:
                plat_printf(" %s ", "closed");
                break;
            case NETIF_OPENED:
                plat_printf(" %s ", "opened");
                break;
            case NETIF_ACTIVE:
                plat_printf(" %s ", "active");
                break;
            default:
                break;
        }
        switch (netif->type) {
            case NETIF_TYPE_ETHER:
                plat_printf(" %s ", "ether");
                break;
            case NETIF_TYPE_LOOP:
                plat_printf(" %s ", "loop");
                break;
            default:
                break;
        }
        plat_printf(" mtu=%d ", netif->mtu);
        plat_printf("\n");
        dump_mac("\tmac:", netif->hwaddr.addr);
        dump_ip_buf("  ip:", netif->ipaddr.a_addr);
        dump_ip_buf("  netmask:", netif->netmask.a_addr);
        dump_ip_buf("  gateway:", netif->gateway.a_addr);

        // 队列中包数量的显示
        plat_printf("\n");
    }
}
#else
#define display_netif_list()
#endif // DBG_NETIF

/**
 * @brief 网络接口层初始化
 */
net_err_t netif_init(void) {
    dbg_info(DBG_NETIF, "init netif");

    // 建立接口列表
    nlist_init(&netif_list);
    mblock_init(&netif_mblock, netif_buffer, sizeof(netif_t), NETIF_DEV_CNT, NLOCKER_NONE);

    // 设置缺省接口
    netif_default = (netif_t *)0;

    // 初始化链路层接口
    plat_memset((void *)link_layers, 0, sizeof(link_layers));

    dbg_info(DBG_NETIF, "init done.");
    return NET_ERR_OK;
}

/**
 * @brief 注册链表层处理类型
 */
net_err_t netif_register_layer(int type, const link_layer_t* layer) {
    // 类型错误
    if ((type < 0) || (type >= NETIF_TYPE_SIZE)) {
        dbg_error(DBG_NETIF, "type error: %d", type);
        return NET_ERR_PARAM;
    }

    // 当前类型已存在，返回错误
    if (link_layers[type]) {
        dbg_error(DBG_NETIF, "link layer: %d exist", type);
        return NET_ERR_EXIST;
    }

    link_layers[type] = layer;
    return NET_ERR_OK;
}

static const link_layer_t *netif_get_layer(int type) {
    // 类型错误
    if ((type < 0) || (type >= NETIF_TYPE_SIZE)) {
        return (const link_layer_t*)0;
    }

    return link_layers[type];
}

/**
 * @brief 打开网络接口
 */
netif_t *netif_open(const char *dev_name, const netif_ops_t *ops, void *ops_data) {
    netif_t *netif = (netif_t *)mblock_alloc(&netif_mblock, -1);
    if (!netif) {
        dbg_error(DBG_NETIF, "no netif");
        return (netif_t *)0;
    }

    // 初始化网络接口名称
    plat_strncpy(netif->name, dev_name, NETIF_NAME_SIZE);
    netif->name[NETIF_NAME_SIZE - 1] = '\0';

    // 初始化硬件地址和ip地址
    plat_memset(&netif->hwaddr, 0, sizeof(netif_hwaddr_t));
    ipaddr_set_any(&netif->ipaddr);
    ipaddr_set_any(&netif->netmask);
    ipaddr_set_any(&netif->gateway);

    // 初始化接口状态、接口类型和mtu值
    netif->state = NETIF_OPENED;
    netif->type = NETIF_TYPE_NONE;
    netif->mtu = 0;
    
    // 初始化链接节点，用于链接其他网络接口
    nlist_node_init(&netif->node);

    // 初始化输入/出队列以及对应的缓冲空间
    net_err_t err = fixq_init(&netif->in_q, netif->in_q_buf, NETIF_INQ_SIZE, NLOCKER_THREAD);
    if (err < 0) {
        dbg_error(DBG_NETIF, "netif in_q init failed.");
        fixq_destroy(&netif->in_q);
        return (netif_t *)0;
    }
    err = fixq_init(&netif->out_q, netif->out_q_buf, NETIF_INQ_SIZE, NLOCKER_THREAD);
    if (err < 0) {
        dbg_error(DBG_NETIF, "netif out_q init failed.");
        fixq_destroy(&netif->out_q);
        return (netif_t *)0;
    }

    // 驱动初始化
    netif->ops = ops;
    netif->ops_data = ops_data;
    err = ops->open(netif, ops_data);  // 驱动在内部可能会对ops相关进行自己的改写
    if (err < 0) {
        dbg_error(DBG_NETIF, "netif ops open failed.");
        goto free_return;
    }

    // 驱动初始化(ops->open中进行）完成后，对netif进行进一步检查
    // 做一些必要性的检查，以免驱动没写好
    if (netif->type == NETIF_TYPE_NONE) {
        dbg_error(DBG_NETIF, "netif type unknown");
        goto free_return;
    }

    // 获取驱动层接口
    netif->link_layer = netif_get_layer(netif->type);
    if (!netif->link_layer && (netif->type != NETIF_TYPE_LOOP)) {
        dbg_error(DBG_NETIF, "no link layer. netif name: %s", dev_name);
        goto free_return;
    }

    // 将打开的网络接口加入整个系统中已打开的网络接口列表中
    nlist_insert_last(&netif_list, &netif->node);
    display_netif_list();
    return netif;

free_return:
    if (netif->state == NETIF_OPENED) {
        netif->ops->close(netif);
    }

    fixq_destroy(&netif->in_q);
    fixq_destroy(&netif->out_q);
    mblock_free(&netif_mblock, netif);

    return (netif_t *)0;
}

/**
 * @brief 设置IP地址、掩码、网关等
 * 
 * 这里只是简单的对接口的各个地址进行写入
 */
net_err_t netif_set_addr(netif_t *netif, ipaddr_t *ip, ipaddr_t *netmask, ipaddr_t *gateway) {
    ipaddr_copy(&netif->ipaddr, ip ? ip : ipaddr_get_any());
    ipaddr_copy(&netif->netmask, netmask ? netmask : ipaddr_get_any());
    ipaddr_copy(&netif->gateway, gateway ? gateway : ipaddr_get_any());

    return NET_ERR_OK;
}

/**
 * @brief 设置硬件地址
 */
net_err_t netif_set_hwaddr(netif_t *netif, const uint8_t *hwaddr, int len) {
    plat_memcpy(netif->hwaddr.addr, hwaddr, len);

    return NET_ERR_OK;
}

/**
 * @brief 激活网络设备
 */
net_err_t netif_set_active(netif_t *netif) {
    // 当前网络设备必须为打开且未激活状态
    if (netif->state != NETIF_OPENED) {
        dbg_error(DBG_NETIF, "netif is not open");
        return NET_ERR_STATE;
    }

    // 如果有底层链路层则调用
    if (netif->link_layer) {
        // 打开失败，则退出
        net_err_t err = netif->link_layer->open(netif);
        if (err < 0) {
            dbg_info(DBG_NETIF, "active error.");
            return err;
        }
    }

    // 判断是否要添加缺省接口
    // 缺省网络接口用于外网数据收发时的包处理
    if (!netif_default && (netif->type != NETIF_TYPE_LOOP)) {
        netif_set_default(netif);
    }

    // 切换为就绪状态
    netif->state = NETIF_ACTIVE;

    display_netif_list();
    return NET_ERR_OK;
}

/**
 * @brief 取消网络设备的激活状态
 */
net_err_t netif_set_deactive(netif_t *netif) {
    // 当前网络设备必须为已激活状态
    if (netif->state != NETIF_ACTIVE) {
        dbg_error(DBG_NETIF, "netif is not active");
        return NET_ERR_STATE;
    }

    // 底层链路的处理
    if (netif->link_layer) {
        netif->link_layer->close(netif);
    }

    // 释放相关资源
    pktbuf_t *buf;
    while ((buf = fixq_recv(&netif->in_q, -1)) != (pktbuf_t *)0) {
        // 释放接收队列中的数据包
        pktbuf_free(buf);
    }
    while ((buf = fixq_recv(&netif->out_q, -1)) != (pktbuf_t *)0) {
        // 释放发送队列中的数据包
        pktbuf_free(buf);
    }

    // 恢复到打开但未激活的状态
    netif->state = NETIF_OPENED;
    display_netif_list();
    return NET_ERR_OK;
}

/**
 * @brief 关闭网络接口
 */
net_err_t netif_close(netif_t *netif) {
    if (netif->state ==  NETIF_ACTIVE) {
        dbg_error(DBG_NETIF, "netif is active, can not close");
        return NET_ERR_STATE;
    }

    // 先关闭内部设备
    netif->ops->close(netif);
    netif->state = NETIF_CLOSED;

    // 再释放netif结构
    nlist_remove(&netif_list, &netif->node);
    mblock_free(&netif_mblock, netif);

    display_netif_list();
    return NET_ERR_OK;
}

/**
 * @brief 设置缺省的网络接口
 * @param netif 缺省的网络接口
 */
void netif_set_default(netif_t *netif) {
    netif_default = netif;
}

/**
 * @brief 将buf加入到网络接口的输入队列中
 */
net_err_t netif_put_in(netif_t *netif, pktbuf_t *buf, int tmo) {
    // 写入接收队列
    net_err_t err = fixq_send(&netif->in_q, buf, tmo);
    if (err < 0) {
        dbg_warning(DBG_NETIF, "netif %s in_q full", netif->name);
        return NET_ERR_FULL;
    }

    // 通知消息处理线程，这里不处理消息是否发送成功等问题
    // 消息满了不要紧，说明网卡正在忙，后续还会处理的
    exmsg_netif_in(netif);
    return NET_ERR_OK;
}

/**
 * @brief 将buf添加到网络接口的输出队列中
 */
net_err_t netif_put_out(netif_t *netif, pktbuf_t *buf, int tmo) {
    // 写入发送队列
    net_err_t err = fixq_send(&netif->out_q, buf, tmo);
    if (err < 0) {
        dbg_info(DBG_NETIF, "netif %s out_q full", netif->name);
        return err;
    }

    // 只是写入队列，具体的发送会调用ops->xmit来启动发送
    return NET_ERR_OK;
}

/**
 * @brief 从输入队列中取出一个数据包
 */
pktbuf_t* netif_get_in(netif_t *netif, int tmo) {
    // 从接收队列中取数据包
    pktbuf_t *buf = fixq_recv(&netif->in_q, tmo);
    if (buf) {
        // 重新定位，方便进行读写
        pktbuf_reset_acc(buf);
        return buf;
    }

    dbg_info(DBG_NETIF, "netif %s in_q empty", netif->name);
    return (pktbuf_t *)0;
}

/**
 * @brief 从输出队列中取出一个数据包
 */
 pktbuf_t* netif_get_out(netif_t* netif, int tmo) {
    // 从发送队列中取数据包，不需要等待。可能会被中断处理程序中调用
    // 因此，不能因为没有包而挂起程序
    pktbuf_t *buf = fixq_recv(&netif->out_q, tmo);
    if (buf) {
        // 重新定位，方便进行读写
        pktbuf_reset_acc(buf);
        return buf;
    }

    dbg_info(DBG_NETIF, "netif %s out_q empty", netif->name);
    return (pktbuf_t*)0;
}

/**
 * @brief 发送一个网络包到网络接口上, 目标地址为ipaddr
 * 
 * 在发送前，先判断驱动是否正在发送，如果是则将其插入到发送队列，等驱动有空后，由驱动自行取出发送。
 * 否则，加入发送队列后，启动驱动发送
 */
net_err_t netif_out(netif_t* netif, ipaddr_t * ipaddr, pktbuf_t* buf) {
    // 缺省情况，将数据包插入就绪队列，然后通知驱动程序开始发送
    // 硬件当前发送如果未进行，则启动发送，否则不处理，等待硬件中断自动触发进行发送
    net_err_t err = netif_put_out(netif, buf, -1);
    if (err < 0) {
        dbg_info(DBG_NETIF, "send to netif queue failed. err: %d", err);
        return err;
    }

    // 启动发送
    return netif->ops->xmit(netif);
}

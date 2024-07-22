#include "ether.h"
#include "dbg.h"
#include "ipaddr.h"
#include "net_err.h"
#include "netif.h"


/**
 * @brief 对指定接口设备进行以太网协议相关初始化
 */
static net_err_t ether_open(netif_t *netif) {
    return NET_ERR_OK;
}

/**
 * @brief 以太网的关闭
 */
static void ether_close(netif_t *netif) {
    
}

/**
 * @brief 以太网输入包的处理
 */
static net_err_t ether_in(struct _netif_t *netif, pktbuf_t *buf) {
    return NET_ERR_OK;
}

static net_err_t ether_out(struct _netif_t *netif, ipaddr_t *dest, pktbuf_t *buf) {
    return NET_ERR_OK;
}

/**
 * @brief 初始化以太网接口层
 */
net_err_t ether_init(void) {
    static const link_layer_t link_layer = {
        .type = NETIF_TYPE_ETHER,
        .open = ether_open,
        .close = ether_close,
        .in = ether_in,
        .out = ether_out,
    };

    dbg_info(DBG_ETHER, "init ether");

    // 注册以太网驱动链接层接口
    net_err_t err = netif_register_layer(NETIF_TYPE_ETHER, &link_layer);
    if (err < 0) {
        dbg_error(DBG_ETHER, "register error");
        return err;
    }

    dbg_info(DBG_ETHER, "init done");
    return NET_ERR_OK;
}


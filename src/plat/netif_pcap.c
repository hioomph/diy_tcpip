/**
 * @brief 使用pcap_device创建的虚拟网络接口
 */

#include "netif_pcap.h"
#include "dbg.h"
#include "net_err.h"
#include "netif.h"
#include "pcap/pcap.h"
#include "pktbuf.h"
#include "sys_plat.h"
#include "ether.h"
#include "exmsg.h"

/**
 * @brief 接收线程
 */
void recv_thread(void *arg) {
    plat_printf("recv thread is running...\n");

    netif_t *netif = (netif_t *)arg;
    pcap_t *pcap = (pcap_t *)netif->ops_data;
    while (1) {
        struct pcap_pkthdr *pkthdr;
        const uint8_t *pkt_data;
        if (pcap_next_ex(pcap, &pkthdr, &pkt_data) != 1) {
            continue;
        }

        // 将pkt_data的数据拷贝到自己的协议栈中
        pktbuf_t *buf = pktbuf_alloc(pkthdr->len);
        if (buf == (pktbuf_t *)0) {
            dbg_warning(DBG_NETIF, "buf is none");
            continue;
        }
        pktbuf_write(buf, (uint8_t *)pkt_data, pkthdr->len);
        
        // 将buf加入输入队列中
        if (netif_put_in(netif, buf, 0) < 0) {
            dbg_warning(DBG_NETIF, "netif %s in_q full", netif->name);
            pktbuf_free(buf);
            continue;
        }
    }
}

/**
 * @brief 发送线程
 */
void xmit_thread(void *arg) {
    plat_printf("xmit thread is running...\n");

    netif_t *netif = (netif_t *)arg;
    pcap_t *pcap = (pcap_t *)netif->ops_data;
    static uint8_t rw_buffer[1500+6+6+2];  // 帧大小，4位校验不用加
    while (1) {
        // 从输出队列中取数据包
        pktbuf_t *buf = netif_get_out(netif, 0);
        if (buf == (pktbuf_t *)0) {
            continue;
        }

        int total_size = buf->total_size;
        plat_memset(rw_buffer, 0, sizeof(rw_buffer));
        pktbuf_read(buf, rw_buffer, total_size);
        pktbuf_free(buf);

        if (pcap_inject(pcap, rw_buffer, total_size) == -1) {
            fprintf(stderr, "pcap send failed: %s\n", pcap_geterr(pcap));
            fprintf(stderr, "pcap send: pcaket size %d\n", total_size);
            continue;
        }
    }
}

/**
 * @brief pcap设备打开
 * @param netif 打开的接口
 * @param driver_data 传入的驱动数据
 */
static net_err_t netif_pcap_open(struct _netif_t *netif, void *data) {
    // 打开pcap设备
    pcap_data_t *dev_data = (pcap_data_t *)data;
    pcap_t *pcap = pcap_device_open(dev_data->ip, dev_data->hwaddr);
    if (pcap == (pcap_t *)0) {
        dbg_error(DBG_NETIF, "pcap open failed! name: %s\n", netif->name);
        return NET_ERR_IO;
    }

    /**
     * 这里之所以不直接对网卡进行操作，是因为当前协议栈运行在Windows系统上，需要借助第三方库对网卡进行操作，
     * 而在本协议栈程序中采用了pcap库。pcap使用时需要先找到对应的网卡，因此此处传入的dev_data->ip, 
     * dev_data->hwaddr只是用于pcap对这个网卡进行“定位”，协议栈并没有用到。
     */


    netif->type = NETIF_TYPE_ETHER;  // 以太网类型
    netif->mtu = ETHER_MTU;
    netif->ops_data = pcap;
    netif_set_hwaddr(netif, dev_data->hwaddr, 6);  // 帧中mac地址大小为6字节

    sys_thread_create(recv_thread, netif);
    sys_thread_create(xmit_thread, netif);
    
    return NET_ERR_OK;
}

/**
 * @brief 关闭pcap网络接口
 * @param netif 待关闭的接口
 */
static void netif_pcap_close (struct _netif_t *netif) {
    pcap_t *pcap = (pcap_t *)netif->ops_data;
    pcap_close(pcap);
}

/**
 * @brief 向接口发送命令
 */
static net_err_t netif_pcap_xmit (struct _netif_t *netif) {
    // 从输出队列取收到的数据包，然后写入输入队列，并通知主线程处理
    pktbuf_t * pktbuf = netif_get_out(netif, -1);
    if (pktbuf) {
        // 写入接收队列
        net_err_t err = netif_put_in(netif, pktbuf, -1);
        if (err < 0) {
            dbg_warning(DBG_NETIF, "netif full");
            pktbuf_free(pktbuf);
            return err;
        }
    }

    return NET_ERR_OK;
}

// 初始化ops的相关接口函数
const netif_ops_t netdev_ops = {
    .open  = netif_pcap_open,
    .close = netif_pcap_close,
    .xmit  = netif_pcap_xmit,
};
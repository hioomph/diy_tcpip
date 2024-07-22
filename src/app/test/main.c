/**
 * @file main.c
 * @brief 测试主程序，完成一些简单的测试主程序
 * @version 0.1
 */

#include <stdio.h>
#include "dbg.h"
#include "ether.h"
#include "mblock.h"
#include "net.h"
#include "net_err.h"
#include "netif.h"
#include "netif_pcap.h"
#include "nlist.h"
#include "nlocker.h"
#include "pcap/pcap.h"
#include "pktbuf.h"
#include "sys_plat.h"


pcap_data_t netdev0_data = {.ip = netdev0_phy_ip, .hwaddr = netdev0_hwaddr};

/**
 * @brief 网络设备初始化
 */
net_err_t netdev_init(void) {    
    int size = sizeof(ether_pkt_t);
    netif_t *netif = netif_open("netif 0", &netdev_ops, &netdev0_data);
    if (!netif) {
        dbg_error(DBG_NETIF, "open netif error");
        return NET_ERR_NONE;
    }

    // 设置ip地址
    ipaddr_t ip, mask, gw;
    ipaddr_from_str(&ip, netdev0_ip);
    ipaddr_from_str(&mask, netdev0_mask);
    ipaddr_from_str(&gw, netdev0_gw);
    netif_set_addr(netif, &ip, &mask, &gw);

    // 激活pcap接口
    netif_set_active(netif);
    return NET_ERR_OK;
}

void mblock_test() {
	mblock_t blist;
	static uint8_t buffer[100][10];

	void *temp[10];
	mblock_init(&blist, buffer, 100, 10, NLOCKER_THREAD);
	for (int i=0; i<10; i++) {
		temp[i] = mblock_alloc(&blist, 0);
        plat_printf("block: %p, free_count: %d\n", temp[i], mblock_free_cnt(&blist));
	}

	for (int i=0; i<10; i++) {
		mblock_free(&blist, temp[i]);
		plat_printf("after free, free_count: %d\n", mblock_free_cnt(&blist));
	}
	mblock_destroy(&blist);
}

void pktbuf_test() {
    pktbuf_t *buf = pktbuf_alloc(2000);
    pktbuf_free(buf);

    // 连续包头的添加的移除
    buf = pktbuf_alloc(2000);
    for (int i=0; i<16; i++) {
        pktbuf_add_header(buf, 33, 1);
    }

    for (int i=0; i<16; i++) {
        pktbuf_remove_header(buf, 33);
    }

    // 非连续包头的添加的移除
    buf = pktbuf_alloc(2000);
    for (int i=0; i<16; i++) {
        pktbuf_add_header(buf, 33, 0);
    }

    for (int i=0; i<16; i++) {
        pktbuf_remove_header(buf, 33);
    }
    pktbuf_free(buf);

    // 调整数据包大小
    buf = pktbuf_alloc(8);
    pktbuf_resize(buf, 32);
    pktbuf_resize(buf, 288);
    pktbuf_resize(buf, 32);
    pktbuf_resize(buf, 8);
    pktbuf_resize(buf, 0);
    pktbuf_free(buf);

    // 合并数据包
    buf = pktbuf_alloc(689);
    pktbuf_t *sbuf = pktbuf_alloc(892);
    pktbuf_join(buf, sbuf);
    pktbuf_free(buf);

    // 调整包头连续性
    buf = pktbuf_alloc(32);
    pktbuf_join(buf, pktbuf_alloc(4));
    pktbuf_join(buf, pktbuf_alloc(16));
    pktbuf_join(buf, pktbuf_alloc(54));
    pktbuf_join(buf, pktbuf_alloc(32));
    pktbuf_join(buf, pktbuf_alloc(38));

    pktbuf_set_cont(buf, 44);
    pktbuf_set_cont(buf, 60);
    pktbuf_set_cont(buf, 128);
    pktbuf_set_cont(buf, 135);
    pktbuf_free(buf);

    // 读写相关测试
    buf = pktbuf_alloc(32);
    pktbuf_join(buf, pktbuf_alloc(4));
    pktbuf_join(buf, pktbuf_alloc(16));
    pktbuf_join(buf, pktbuf_alloc(54));
    pktbuf_join(buf, pktbuf_alloc(32));
    pktbuf_join(buf, pktbuf_alloc(38));
    pktbuf_join(buf, pktbuf_alloc(512));

    pktbuf_reset_acc(buf);
    static uint16_t temp[1000];
    for (int i = 0; i < 1000; i++) {
        temp[i] = i;
    }
    pktbuf_write(buf, (uint8_t *)temp, pktbuf_total(buf));
    // pktbuf_write(buf, (uint8_t *)temp, pktbuf_total(buf));

    static uint16_t read_temp[1000];
    plat_memset(read_temp, 0, sizeof(read_temp));

    pktbuf_reset_acc(buf);  // 调整pos指针
    pktbuf_read(buf, (uint8_t *)read_temp, pktbuf_total(buf));
    if (plat_memcmp(temp, read_temp, pktbuf_total(buf)) != 0) {
        plat_printf("not equal");
        return;
    }

    // 定位读写，不超过1个块
	plat_memset(read_temp, 0, sizeof(read_temp));
	pktbuf_seek(buf, 18 * 2);
	pktbuf_read(buf, (uint8_t*)read_temp, 56);
	if (plat_memcmp(temp + 18, read_temp, 56) != 0) {
		printf("not equal.");
		exit(-1);
	}

    // 定位跨一个块的读写测试, 从170开始读，读56
	plat_memset(read_temp, 0, sizeof(read_temp));
	pktbuf_seek(buf, 85 * 2);
	pktbuf_read(buf, (uint8_t*)read_temp, 256);
	if (plat_memcmp(temp + 85, read_temp, 256) != 0) {
		printf("not equal.");
		exit(-1);
	}

    // 数据的复制
	pktbuf_t* dest = pktbuf_alloc(1024);
	pktbuf_seek(buf, 200);  // 从200处开始读
	pktbuf_seek(dest, 600);  // 从600处开始写
	pktbuf_copy(dest, buf, 122);  // 复制122个字节

    // 重新定位到600处开始读
	plat_memset(read_temp, 0, sizeof(read_temp));
	pktbuf_seek(dest, 600);  // 重新回到600的位置
	pktbuf_read(dest, (uint8_t*)read_temp, 122);  // 读122个字节
	if (plat_memcmp(temp + 100, read_temp, 122) != 0) { 
        // temp+100，实际定位到200字节偏移处
		printf("not equal.");
		exit(-1);
	}

	// 填充测试
	pktbuf_seek(dest, 0);
	pktbuf_fill(dest, 53, pktbuf_total(dest));

	plat_memset(read_temp, 0, sizeof(read_temp));
	pktbuf_seek(dest, 0);
	pktbuf_read(dest, (uint8_t*)read_temp, pktbuf_total(dest));
	for (int i = 0; i < pktbuf_total(dest); i++) {
		if (((uint8_t *)read_temp)[i] != 53) {
			printf("not equal.");
			exit(-1);
		}
	}

	pktbuf_free(dest);
	pktbuf_free(buf);  // 可以进去调试，在退出函数前看下所有块是否全部释放完毕
}

/**
 * @brief 基本测试
 */
void basic_test(void) {
	mblock_test();
    pktbuf_test();
}

/**
 * @brief 测试入口
 */
int main(void) {
    // 初始化协议栈
    net_init();
    // 基础测试
    // basic_test();
    // 初始化网络接口
    netdev_init();
    // 启动协议栈
    net_start();

    while (1) {
        sys_sleep(10);
    }
}
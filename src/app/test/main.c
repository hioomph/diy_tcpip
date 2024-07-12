/**
 * @file main.c
 * @brief 测试主程序，完成一些简单的测试主程序
 * @version 0.1
 */

#include <stdio.h>
#include "net_err.h"
#include "nlocker.h"
#include "pcap/pcap.h"
#include "sys_plat.h"
#include "net.h"
#include "netif_pcap.h"
#include "dbg.h"
#include "nlist.h"
#include "mblock.h"
#include "pktbuf.h"

/**
 * @brief 网络设备初始化
 */
net_err_t netdev_init(void) {    
    netif_pcap_open();
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

    buf = pktbuf_alloc(2000);
    for (int i=0; i<16; i++) {
        pkybuf_add_header(buf, 33, 1);
    }
}

/**
 * @brief 基本测试
 */
void basic_test(void) {
	// mblock_test();
    pktbuf_test();
}

/**
 * @brief 测试入口
 */
int main(void) {
    // 初始化协议栈
    net_init();
    // 基础测试
    basic_test();
    // 初始化网络接口
    netdev_init();
    // 启动协议栈
    net_start();

    while (1) {
        sys_sleep(10);
    }
}
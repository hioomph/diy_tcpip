/**
 * @file main.c
 * @brief 测试主程序，完成一些简单的测试主程序
 * @version 0.1
 */

#include <stdio.h>
#include "net_err.h"
#include "pcap/pcap.h"
#include "sys_plat.h"
#include "net.h"
#include "netif_pcap.h"


static sys_sem_t sem;
static sys_mutex_t mutex;
static int count;  // 共享资源

static char buffer[100];  // 环形数据缓冲区
static int write_idx, read_idx;  // 写索引和读索引
static sys_sem_t read_sem;
static sys_sem_t write_sem;


void thread1_entry(void *arg) {
	// 消费者线程
	for (int i = 0; i < 2 * sizeof(buffer); i++) {
		sys_sem_wait(read_sem, 0);

		char data = buffer[read_idx++];
		if (read_idx >= sizeof(buffer)) {
			read_idx = 0;
		}
		plat_printf("thread1: read data = %d\n", data);
		
		sys_sem_notify(write_sem);
		sys_sleep(100);
	}

	while (1) {
		plat_printf("this is thread1: %s\n", (char *)arg);
		sys_sleep(1000);
		sys_sem_notify(sem);
		sys_sleep(1000);
	}
}

void thread2_entry(void *arg) {
	sys_sleep(100);

	// 生产者线程
	for (int i = 0; i < 2 * sizeof(buffer); i++) {
		sys_sem_wait(write_sem, 0);

		buffer[write_idx++] = i;
		if (write_idx >= sizeof(buffer)) {
			write_idx = 0;
		}
		plat_printf("thread2: write data = %d\n", i);

		sys_sem_notify(read_sem);
	}

	while (1) {
		sys_sem_wait(sem, 0);
		plat_printf("this is thread2: %s\n", (char *)arg);
	}
}

net_err_t netdev_init(void) {
	netif_pcap_open();

	return NET_ERR_OK;
}

int main(void) {
	net_init();
	net_start();
	netdev_init();

	while (1) {
		sys_sleep(10);
	}

	return 0;
}
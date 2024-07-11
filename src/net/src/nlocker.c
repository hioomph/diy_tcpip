#include "nlocker.h"
#include "net_err.h"
#include "sys_plat.h"

/**
 * @brief 锁初始化
 */
net_err_t nlocker_init(nlocker_t *locker, nlocker_type_t type) {
    // 类型为用于多线程间的锁
    if (type == NLOCKER_THREAD) {
        sys_mutex_t mutex = sys_mutex_create();
        if (mutex == SYS_MUTEX_INVALID) {
            return NET_ERR_SYS;
        }
        locker->mutex = mutex;
    }

    locker->type = type;
    return NET_ERR_OK;
}

/**
 * @brief 锁销毁
 */
void nlocker_destroy(nlocker_t *locker) {
    if (locker->type == NLOCKER_THREAD) {
        sys_mutex_free(locker->mutex);
    }
}

/**
 * @brief 加锁
 */
void nlocker_lock(nlocker_t *locker) {
    if (locker->type == NLOCKER_THREAD) {
        sys_mutex_lock(locker->mutex);
    }
}

/**
 * @brief 解锁
 */
void nlocker_unlock(nlocker_t *locker) {
    if (locker->type == NLOCKER_THREAD) {
        sys_mutex_unlock(locker->mutex);
    }
}
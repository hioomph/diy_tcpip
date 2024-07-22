/**
 * @brief 调试信息输出
 */

#include <string.h>
#include <stdarg.h>
#include "dbg.h"
#include "net_plat.h"

/**
 * @brief 调试信息输出，输出文件名、函数名、函数以及调试信息
 * 
 * @param m_level  主日志等级
 * @param s_level  子日志等级
 */
void dbg_print(int m_level, int s_level, const char* file, const char* func, int line, const char* fmt, ...) {
    static const char* title[] = {
        [DBG_LEVEL_ERROR] = DBG_STYLE_ERROR"error",
        [DBG_LEVEL_WARNING] = DBG_STYLE_WARNING"warning",
        [DBG_LEVEL_INFO] = "info",
        [DBG_LEVEL_NONE] = "none"
    };

    // 当仅当前等级数值比较大时才输出
    if (m_level >= s_level) {
        // 由于file参数可能是完整的文件路径，因此从路径的末尾开始，
        // 向前搜索第一个出现的路径分隔符（'\\' 或 '/'）
        // end最终指向文件名的起始位置
        const char *end = file + plat_strlen(file);
        while (end >= file) {
            if ((*end == '\\') || (*end == '/')) {
                break;
            }
            end--;
        }
        end++;

        // 每行信息提示的开头
        plat_printf("%s(%s-%s-%d):", title[s_level], end, func, line);

        char str_buf[128];  // 存储格式化后的日志字符串
        va_list args;       // 用于处理可变参数列表

        // 具体的信息
        va_start(args, fmt);  // 初始化va_list，指向fmt后面的参数
        plat_vsprintf(str_buf, fmt, args);  // 将格式化字符串和参数列表格式化到str_buf中
        plat_printf("%s\n"DBG_STYLE_RESET, str_buf);  // 输出格式化后的日志信息，并重置样式
        va_end(args);  // 结束可变参数的处理
    }
}

/**
 * @brief 打印mac地址
 */
void dump_mac(const char * msg, const uint8_t * mac) {
    if (msg) {
        plat_printf("%s", msg);
    }

    plat_printf("%02x-%02x-%02x-%02x-%02x-%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief 打印IP地址
 * 
 * @param ip ip地址
 */
void dump_ip_buf(const char* msg, const uint8_t* ip) {
    if (msg) {
        plat_printf("%s", msg);
    }

    if (ip) {
        plat_printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    } else {
        plat_printf("0.0.0.0");
    }
}
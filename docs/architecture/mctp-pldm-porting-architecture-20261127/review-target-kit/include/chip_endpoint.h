/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CHIP_ENDPOINT_H
#define CHIP_ENDPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 首版固定 BTU=64。wire 长度包括 4B MCTP header 和 1B PEC。 */
#define CHIP_WIRE_MAX 69u
#define CHIP_MESSAGE_MAX 512u
#define CHIP_TX_SLOTS 16u

/* 单实例、单线程 API：只能从裸机主循环调用，ISR 只记录事件和复制数据。
 * init 前可调用 mctp_set_alloc_ops() 替换 malloc/free；整个实例生命周期内
 * 不得切换全局 allocator。默认构建使用 C runtime malloc/free。
 * now_ms 必须单调递增；initial_eid 可为 0（待分配），或 8..254。
 * 尚未实现 I3C Discovery Notify 主动请求流程，需 HOST 主动初始化端点。
 */
int chip_endpoint_init(uint8_t initial_eid, uint8_t tid,
                       uint64_t (*now_ms)(void *), void *clock_arg);
void chip_endpoint_deinit(void);

/* RX 传入本次实际收到的完整 private-write 数据，含 PEC，不含地址头。
 * dynamic_addr 为这笔事务使用的 7-bit I3C 动态地址；错误 PEC 不交给核心。
 * 返回 0 表示包已交给核心，不等于上层命令成功。
 */
int chip_endpoint_rx(uint8_t dynamic_addr, const uint8_t *wire, size_t actual);

/* 在主循环取待发包并复制到硬件 TX buffer；成功返回 0，空队列 -EAGAIN。
 * 返回的数据含 PEC，随后才允许发 IBI MDB=0xAE。
 * 必须保持硬件缓冲区有效直到 completion；每次准备发送只调用一次。
 * 动态地址必须在 peek 到 completion 期间保持不变，否则先撤销硬件发送。
 */
int chip_endpoint_tx_peek(uint8_t dynamic_addr, uint8_t *wire,
                          size_t capacity, size_t *length);

/* 完整、正常传输才出队；短读/异常保留队头，下一次从包头重装再发 IBI。
 * 由调用者用 ISR/硬件结果判断正常结束，不可只凭 IRQ 发生就传 true。
 */
int chip_endpoint_tx_complete(size_t actual, bool normal_end);
size_t chip_endpoint_tx_pending(void);
uint8_t chip_endpoint_pec(uint8_t address_byte, const uint8_t *data, size_t len);
#endif

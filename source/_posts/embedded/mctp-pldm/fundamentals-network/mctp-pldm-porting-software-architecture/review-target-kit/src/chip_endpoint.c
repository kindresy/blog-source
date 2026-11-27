/* SPDX-License-Identifier: Apache-2.0 */
#include "chip_endpoint.h"
#include "libmctp.h"
#include <libpldm/base.h>
#include <errno.h>
#include <string.h>

/* 队列拥有 packet 的独立副本；libmctp tx 回调返回后，原 pkt 会被释放。
 * 不在 ISR 调 libmctp，避免 allocator、重组状态机和队列的并发访问。
 */
static struct {
    struct mctp *core;
    struct mctp_binding binding;
    struct { uint8_t data[68]; size_t len; } tx[CHIP_TX_SLOTS];
    size_t head, count, prepared;
    uint8_t tid;
    int dispatch_error;
} ep;
static uint8_t tx_storage[MCTP_PKTBUF_SIZE(68)] PKTBUF_STORAGE_ALIGN;

uint8_t chip_endpoint_pec(uint8_t address_byte, const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    /* SMBus CRC-8, polynomial 0x07, initial 0. 地址/RnW 纳入校验。 */
    for (size_t n = 0; n <= len; ++n) {
        crc ^= n ? data[n - 1] : address_byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0));
    }
    return crc;
}

static int binding_tx(struct mctp_binding *binding, struct mctp_pktbuf *pkt)
{
    (void)binding;
    const size_t len = mctp_pktbuf_size(pkt);
    if (len > 68) return -EMSGSIZE;
    if (ep.count == CHIP_TX_SLOTS) return -EBUSY;
    size_t tail = (ep.head + ep.count) % CHIP_TX_SLOTS;
    memcpy(ep.tx[tail].data, mctp_pktbuf_hdr(pkt), len);
    ep.tx[tail].len = len;
    ep.count++;
    return 0;
}

static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (unsigned b = 0; b < 8; b++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static void on_message(uint8_t src, bool tag_owner, uint8_t tag, void *arg,
                       void *message, size_t len)
{
    (void)arg;
    const uint8_t *bytes = message;
    /* MCTP message[0] 是 type/IC；本示例只接收无额外 MIC 的 PLDM 请求。
     * MCTP Control 由 upstream core 在调用本函数前处理。
     */
    if (!tag_owner || len < 4 || bytes[0] != 0x01) return;
    const struct pldm_msg *req = (const void *)(bytes + 1);
    struct pldm_header_info h;
    if (bytes[1] & 0x20 || bytes[2] & 0xc0) return;
    if (unpack_pldm_header(&req->hdr, &h) != PLDM_SUCCESS ||
        h.msg_type != PLDM_REQUEST) return;
    uint8_t out[64] = { 0x01 };
    struct pldm_msg *resp = (void *)(out + 1);
    size_t payload = len - 4, response_payload = 1;
    uint8_t cc = PLDM_SUCCESS;
    int rc = 0;
    /* Wire version 1.1.0: alpha=00, update=f0, minor=f1, major=f1.
     * GetVersion 最后一项为 version bytes 的 IEEE CRC32（小端）。
     */
    static const uint8_t version[4] = { 0x00, 0xf0, 0xf1, 0xf1 };
    if (h.pldm_type != PLDM_BASE) {
        cc = PLDM_ERROR_INVALID_PLDM_TYPE;
    } else switch (h.command) {
    case PLDM_SET_TID: {
        uint8_t tid;
        rc = decode_set_tid_req(req, payload, &tid);
        if (rc) cc = payload != 1 ? PLDM_ERROR_INVALID_LENGTH : PLDM_ERROR_INVALID_DATA;
        else ep.tid = tid;
        break;
    }
    case PLDM_GET_TID:
        if (payload) { cc = PLDM_ERROR_INVALID_LENGTH; break; }
        rc = encode_get_tid_resp(h.instance, 0, ep.tid, resp);
        response_payload = 2;
        break;
    case PLDM_GET_PLDM_TYPES: {
        if (payload) { cc = PLDM_ERROR_INVALID_LENGTH; break; }
        bitfield8_t types[8] = { { .byte = 1 } };
        rc = encode_get_types_resp(h.instance, 0, types, resp);
        response_payload = 9;
        break;
    }
    case PLDM_GET_PLDM_COMMANDS: {
        uint8_t type;
        ver32_t requested;
        rc = decode_get_commands_req(req, payload, &type, &requested);
        if (rc) { cc = (uint8_t)rc; break; }
        if (type != PLDM_BASE) { cc = 0x83; break; }
        if (memcmp(&requested, version, 4)) { cc = 0x84; break; }
        bitfield8_t commands[32] = { { .byte = 0x3e } };
        rc = encode_get_commands_resp(h.instance, 0, commands, resp);
        response_payload = 33;
        break;
    }
    case PLDM_GET_PLDM_VERSION: {
        uint32_t handle;
        uint8_t op, type;
        rc = decode_get_version_req(req, payload, &handle, &op, &type);
        if (rc) { cc = (uint8_t)rc; break; }
        if (handle) { cc = 0x80; break; }
        if (op != PLDM_GET_FIRSTPART) { cc = 0x81; break; }
        if (type != PLDM_BASE) { cc = 0x83; break; }
        uint8_t versions[8];
        memcpy(versions, version, 4);
        uint32_t crc = crc32_ieee(version, 4);
        for (unsigned i = 0; i < 4; i++) versions[4 + i] = (uint8_t)(crc >> (8 * i));
        rc = encode_get_version_resp(h.instance, 0, 0, PLDM_START_AND_END,
                                     (const ver32_t *)(const void *)versions, 8, resp);
        response_payload = 14;
        break;
    }
    default:
        cc = PLDM_ERROR_UNSUPPORTED_PLDM_CMD;
    }
    /* SetTID 与所有错误响应都使用通用 header + completion code。
     * 返回未知 type 时仍回显请求 type，不能错误地写成其他 PLDM 类型。
     */
    if (cc || h.command == PLDM_SET_TID) {
        struct pldm_header_info rh = h;
        rh.msg_type = PLDM_RESPONSE;
        rc = pack_pldm_header(&rh, &resp->hdr);
        resp->payload[0] = cc;
        response_payload = 1;
    }
    if (rc) { ep.dispatch_error = -EPROTO; return; }
    ep.dispatch_error = mctp_message_tx(ep.core, src, false, tag,
                                         out, 4 + response_payload);
}

int chip_endpoint_init(uint8_t eid, uint8_t tid,
                       uint64_t (*now_ms)(void *), void *clock_arg)
{
    if (ep.core) return -EBUSY;
    if ((!now_ms) || (eid && (eid < 8 || eid == 255)) || !tid || tid == 255)
        return -EINVAL;
    memset(&ep, 0, sizeof(ep));
    ep.tid = tid;
    ep.core = mctp_init();
    if (!ep.core) return -ENOMEM;
    mctp_set_now_op(ep.core, now_ms, clock_arg);
    mctp_set_max_message_size(ep.core, CHIP_MESSAGE_MAX);
    ep.binding.name = "chip-i3c-target";
    ep.binding.version = 1;
    ep.binding.pkt_size = 68;
    ep.binding.tx = binding_tx;
    ep.binding.tx_storage = tx_storage;
    int rc = mctp_register_bus(ep.core, &ep.binding, eid);
    if (!rc) rc = mctp_set_rx_all(ep.core, on_message, NULL);
    if (!rc) rc = mctp_control_add_type(ep.core, 1);
    if (rc) { chip_endpoint_deinit(); return rc; }
    mctp_binding_set_tx_enabled(&ep.binding, true);
    return 0;
}

void chip_endpoint_deinit(void)
{
    /* 必须先由平台停止 IRQ/DMA 和硬件读写，再销毁实例。 */
    if (ep.core) mctp_destroy(ep.core);
    memset(&ep, 0, sizeof(ep));
}

int chip_endpoint_rx(uint8_t da, const uint8_t *wire, size_t actual)
{
    if (!ep.core || !wire || !da || da >= 0x7e) return -EINVAL;
    if (actual < 6 || actual > CHIP_WIRE_MAX) return -EMSGSIZE;
    /* 首版串行处理管理请求：上一条响应尚未发完时由平台保留新 RX 包重试。 */
    if (ep.count || ep.prepared) return -EBUSY;
    if (chip_endpoint_pec((uint8_t)(da << 1), wire, actual - 1) != wire[actual - 1])
        return -EBADMSG;
    if (wire[0] != 1 || wire[1] == 255 || wire[2] == 255)
        return -EPROTO;
    struct mctp_pktbuf *pkt = mctp_pktbuf_alloc(&ep.binding, actual - 1);
    if (!pkt) return -ENOMEM;
    memcpy(mctp_pktbuf_hdr(pkt), wire, actual - 1);
    ep.dispatch_error = 0;
    /* 固定的上游版本实际借用 pkt，并不释放它（与该版本头文件注释不同）。
     * 同步调用结束后由此适配层释放；升级 libmctp 时必须重新审查所有权。
     */
    mctp_bus_rx(&ep.binding, pkt);
    mctp_pktbuf_free(pkt);
    return ep.dispatch_error;
}

int chip_endpoint_tx_peek(uint8_t da, uint8_t *wire, size_t cap, size_t *len)
{
    if (!ep.core || !wire || !len || !da || da >= 0x7e) return -EINVAL;
    if (ep.prepared) return -EBUSY;
    if (!ep.count) return -EAGAIN;
    size_t n = ep.tx[ep.head].len;
    if (cap < n + 1) return -EMSGSIZE;
    memcpy(wire, ep.tx[ep.head].data, n);
    wire[n] = chip_endpoint_pec((uint8_t)((da << 1) | 1), wire, n);
    *len = ep.prepared = n + 1;
    return 0;
}

int chip_endpoint_tx_complete(size_t actual, bool normal_end)
{
    if (!ep.core || !ep.prepared) return -EINVAL;
    bool good = normal_end && actual == ep.prepared;
    ep.prepared = 0;
    if (!good) return -EIO; /* 队头不释放，重新 peek 可从包头重发。 */
    ep.head = (ep.head + 1) % CHIP_TX_SLOTS;
    ep.count--;
    mctp_binding_set_tx_enabled(&ep.binding, true);
    return 0;
}

size_t chip_endpoint_tx_pending(void) { return ep.count; }

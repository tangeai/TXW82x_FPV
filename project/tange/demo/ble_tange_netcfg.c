/*
 * 探鸽云 BLE 蓝牙配网 — GATT 数据通道 + 8 字节包头协议 + TciProcessRegInfo 对接
 *
 * 见 ble_tange_netcfg.h 头部说明。与 SDK 自带 ble_demo.c 的旧简单协议
 * (":ssid,passwd,keymgmt") 并存但互不影响: 本文件用自己的 GATT 服务表调
 * uble_init, 走探鸽 SDK 标准注册流程。
 */
#include "sys_config.h"
#include "basic_include.h"

#if BLE_SUPPORT

#include "lib/bluetooth/uble/ble_demo.h"   /* ble_set_mode / ble_set_coexist_en, 间接含 uble.h / hci_host.h / ble_adv.h */
#include "ble_tange_netcfg.h"
#include "ipc_tool.h"                       /* struct IpcLic / IpcWorkParam / IPCP_F_BIND_TO_USER */
#include "TgCloudApi.h"                     /* TciProcessRegInfo */
#include "TgCloudCmd.h"                     /* Tcis_SetWifiResp */

/* demo.c 的全局: 设备 license(含 uuid) 与 工作参数(含 flags) */
extern struct IpcLic       g_Lic;
extern struct IpcWorkParam g_IpcParam;

/* ===================== 自定义 128 位 UUID =========================
 * TODO: 占位 UUID, 必须与 APP 约定后替换为正式值!
 * uble 的 128 位 UUID 按小端字节序存放 (低字节在前)。
 * 这里给一个明显的占位: 服务 ...AA, 特征 ...BB。 */
#define BLE_TG_SERVICE_UUID {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0xAA}
#define BLE_TG_CHAR_UUID    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0xBB}

/* 一个特征同时具备 写(含无应答写)+读+通知 */
#define BLE_TG_CHAR_PROP  (UBLE_GATT_CHARAC_WRITE | UBLE_GATT_CHARAC_WRITE_WITHOUT_RESPONSE \
                          | UBLE_GATT_CHARAC_READ | UBLE_GATT_CHARAC_NOTIFY)

/* notify 查句柄要用的 128 位 UUID 数组 (与 BLE_TG_CHAR_UUID 同值) */
static const uint8 g_tg_char_uuid[16] = BLE_TG_CHAR_UUID;

/* 探鸽 BLE 配网命令 (协议文档定义, TgCloudCmd.h 未含) */
#define TG_CMD_SETWIFI_REQ   0x8006
#define TG_CMD_SETWIFI_RESP  0x8007

/* ===================== 8 字节包头协议解析 + 分包重组 ===================== */
#define TG_PKT_HDR_LEN     8
#define TG_RXBUF_SZ        512    /* 覆盖 8 头 + 244 数据, 对齐 MTU, 留余量 */
#define TG_RX_TIMEOUT_MS   5000   /* 收包途中静默超过此值则丢弃复位 */

static struct {
    uint8   buf[TG_RXBUF_SZ];
    uint32  got;     /* 已累计字节数 */
    uint32  need;    /* 完整包总长 = 8 + Length; 头未解析时为 0 */
    uint16  cmd;
    uint16  sn;      /* 应答要回填的请求 SN */
    uint32  last_ms;
} g_tg_rx;

static uint8 g_tg_ble_started = 0;   /* 1=BLE 配网已启动 (用于配网成功后关 BLE) */

static void tg_rx_reset(void)
{
    g_tg_rx.got = 0;
    g_tg_rx.need = 0;
    g_tg_rx.cmd = 0;
    g_tg_rx.sn = 0;
}

/* 小端读取 (避免对齐/字节序问题) */
static uint16 rd_le16(const uint8 *p) { return (uint16)(p[0] | (p[1] << 8)); }
static uint32 rd_le32(const uint8 *p) { return (uint32)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static void   wr_le16(uint8 *p, uint16 v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static void   wr_le32(uint8 *p, uint32 v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff; }

/* 发送 0x8007 应答: 8 字节包头 + 8 字节 Tcis_SetWifiResp */
static void tg_send_setwifi_resp(uint16 sn, int result)
{
    uint8 resp[TG_PKT_HDR_LEN + 8];
    Tcis_SetWifiResp body;

    os_memset(&body, 0, sizeof(body));
    body.result = result;   /* 0: 已接受配网数据; 1: 失败 */

    wr_le16(resp + 0, TG_CMD_SETWIFI_RESP);
    wr_le16(resp + 2, sn);
    wr_le32(resp + 4, sizeof(body));   /* Length = 8 */
    os_memcpy(resp + TG_PKT_HDR_LEN, &body, sizeof(body));

    int hdl = uble_uuid_2hdl((uint32)(unsigned long)g_tg_char_uuid, 128);
    if (hdl > 0) {
        uble_gatt_notify((uint16)hdl, resp, sizeof(resp));
        os_printf(KERN_NOTICE "tg_ble: notify 0x8007 sn=%u result=%d (hdl=%d)\r\n",
                  (unsigned)sn, result, hdl);
    } else {
        os_printf(KERN_ERR "tg_ble: uble_uuid_2hdl fail, ret=%d\r\n", hdl);
    }
}

/* 收齐一个完整 0x8006 包后处理: 透传数据部分给 SDK + 立即应答 */
static void tg_handle_full_packet(void)
{
    /* 数据部分 = buf + 8, 长度 = need - 8 */
    int ret = TciProcessRegInfo(g_tg_rx.buf + TG_PKT_HDR_LEN);
    os_printf(KERN_NOTICE "tg_ble: 0x8006 full (datalen=%u), TciProcessRegInfo=%d\r\n",
              (unsigned)(g_tg_rx.need - TG_PKT_HDR_LEN), ret);

    /* 收到即应答: result=0 表示已接受配网数据 (TciProcessRegInfo 非 0 视为失败) */
    tg_send_setwifi_resp(g_tg_rx.sn, ret == 0 ? 0 : 1);
}

/*
 * GATT 读写回调 (签名同 uble 框架要求):
 *   read != 0: APP 读特征 (本协议读侧无实质内容, 返回 0 长度)
 *   read == 0: APP 写特征 (配网数据), 在此累积 + 拆包 + 收齐处理
 * 返回: 写成功返回 0; 读返回响应字节数; 错误返回 UBLE_ATT_ERR_*
 */
static int32 tg_ble_gatt_hdlval(const struct uble_value_entry *entry,
                                uint8 read, uint8 *buff, int32 size, uint32 offset)
{
    (void)entry;

    /* [BLEDBG] 无条件: 确认 HDL 回调是否被库调用 */
    os_printf(KERN_NOTICE "[BLEDBG] hdlval ENTER read=%u size=%d off=%u\r\n",
              read, size, (unsigned)offset);

    if (read) {
        /* 读侧无内容 (应答走 notify) */
        return 0;
    }

    /* ---- 写: 累积分包数据 ---- */
    uint32 now = (uint32)os_jiffies_to_msecs(os_jiffies());

    /* 收包途中静默超时 -> 视为上一包残留, 丢弃复位 */
    if (g_tg_rx.got > 0 && (uint32)(now - g_tg_rx.last_ms) > TG_RX_TIMEOUT_MS) {
        os_printf(KERN_WARNING "tg_ble: rx timeout, reset (got=%u)\r\n", (unsigned)g_tg_rx.got);
        tg_rx_reset();
    }
    g_tg_rx.last_ms = now;

    if (size <= 0) {
        return 0;
    }

    /* 注意: 本 uble 库分包写时 offset 恒为 0 (每包是"连续追加"语义, 不是 offset
     * 寻址), 因此必须以累计的 got 作为写入位置, 顺序拼接, 不能用 offset。
     * 防溢出: got+size 超缓冲 -> 丢弃复位 */
    if (g_tg_rx.got + (uint32)size > TG_RXBUF_SZ) {
        os_printf(KERN_ERR "tg_ble: rx overflow got=%u size=%d, reset\r\n", (unsigned)g_tg_rx.got, size);
        tg_rx_reset();
        return UBLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    os_memcpy(g_tg_rx.buf + g_tg_rx.got, buff, size);
    g_tg_rx.got += (uint32)size;

    /* 头收齐 -> 解析 Command/SN/Length */
    if (g_tg_rx.need == 0 && g_tg_rx.got >= TG_PKT_HDR_LEN) {
        g_tg_rx.cmd  = rd_le16(g_tg_rx.buf + 0);
        g_tg_rx.sn   = rd_le16(g_tg_rx.buf + 2);
        uint32 len   = rd_le32(g_tg_rx.buf + 4);
        g_tg_rx.need = TG_PKT_HDR_LEN + len;

        if (g_tg_rx.cmd != TG_CMD_SETWIFI_REQ) {
            os_printf(KERN_WARNING "tg_ble: unexpected cmd=0x%04X, reset\r\n", g_tg_rx.cmd);
            tg_rx_reset();
            return UBLE_ATT_ERR_REQ_NOT_SUPPORTED;
        }
        if (g_tg_rx.need > TG_RXBUF_SZ) {
            os_printf(KERN_ERR "tg_ble: pkt too long need=%u, reset\r\n", (unsigned)g_tg_rx.need);
            tg_rx_reset();
            return UBLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
    }

    /* 整包收齐 -> 处理 */
    if (g_tg_rx.need > 0 && g_tg_rx.got >= g_tg_rx.need) {
        tg_handle_full_packet();
        tg_rx_reset();
    }

    return 0;
}

/* ===================== GATT 服务表 ===================== */
static const struct uble_value_entry tg_ble_values[] = {
    { .type = UBLE_VALUE_TYPE_HDL, .value = (void *)tg_ble_gatt_hdlval, .size = 0, .bitoff = 0, .maskbit = 0 },
};

static const struct uble_gatt_data tg_ble_att_table[] = {
    /* Generic Access service */
    { .att_type = 0x2800, .properties = 0,                     .att_value = 0x1800 },
    /* Characteristic: Device Name */
    { .att_type = 0x2803, .properties = UBLE_GATT_CHARAC_READ, .att_value = 0x2A00 },
    { .att_type = 0x2A00, .properties = 0,                     .att_value = 0 },

    /* 探鸽 BLE 配网 service (128 位自定义 UUID) */
    { .att_type = 0x2800,                       .properties = 0,               .att_value_128 = BLE_TG_SERVICE_UUID },
    /* Characteristic 声明: 写+读+通知 三合一 */
    { .att_type = 0x2803,                       .properties = BLE_TG_CHAR_PROP, .att_value_128 = BLE_TG_CHAR_UUID },
    /* Characteristic 值: 绑 HDL 读写回调 */
    { .att_type_128 = BLE_TG_CHAR_UUID,         .properties = 0,               .att_value = (uint32)&tg_ble_values[0] },
    /* CCCD: notify 必需 */
    { .att_type = 0x2902,                       .properties = 0,               .att_value = 0 },
};

/* ===================== 初始化 ===================== */
int tg_ble_netcfg_init(const char* uuid)
{
    char name[80];
    int  nlen = os_snprintf(name, sizeof(name), "AICAM_%s", uuid);
    if (nlen <= 0) { nlen = 0; }

    /* 构建 scan_resp: [len][0x09 Complete Local Name][name...] */
    uint8 scan_resp[64];
    int   p = 0;
    scan_resp[p++] = (uint8)(nlen + 1);   /* AD length = 1(type) + name */
    scan_resp[p++] = 0x09;                /* AD type: Complete Local Name */
    os_memcpy(scan_resp + p, name, nlen);
    p += nlen;

    /* adv_data: Flags(LE General Discoverable + BR/EDR not supported) */
    uint8 adv_data[] = { 0x02, 0x01, 0x06 };

    tg_rx_reset();
    g_tg_rx.last_ms = (uint32)os_jiffies_to_msecs(os_jiffies());

    /* 用本协议的 GATT 表初始化 uble (MTU 512); adv_rx 复用 ble_adv_rx_data (广播配网解析, 本协议不依赖它) */
    uble_init(tg_ble_att_table, ARRAY_SIZE(tg_ble_att_table), 512, (void *)ble_adv_rx_data);
    bt_hci_set_advdata(adv_data, sizeof(adv_data));
    bt_hci_set_scan_rsp(scan_resp, p);
    bt_hci_set_adv_interval(50000);
    bt_hci_set_ll_length(251);
    bt_hci_set_adv_en(1);

    /* 与 WiFi 共存 + BLE 协议配网模式 (mode 3), 信道 38 */
    ble_set_coexist_en(1, 0);
    ble_set_mode(3, 38);

    g_tg_ble_started = 1;
    os_printf(KERN_NOTICE "tg_ble: BLE netcfg started, name=%s\r\n", name);
    /* [BLEDBG] 打印回调地址, 与日志 uble_value_set value=... 对比是否一致 */
    os_printf(KERN_NOTICE "[BLEDBG] hdlval=%p, values[0]=%p\r\n",
              (void *)tg_ble_gatt_hdlval, (void *)&tg_ble_values[0]);
    return 0;
}

/* 配网成功 (STA 连上) 后关闭 BLE 省电. 在 events.c sys_event_hdl 里调用 */
void tg_ble_netcfg_event(uint32 event_id, uint32 data, uint32 priv)
{
    (void)data; (void)priv;
    if (!g_tg_ble_started) {
        return;
    }
    if (event_id == SYS_EVENT(SYS_EVENT_WIFI, SYSEVT_WIFI_CONNECTTED)) {
        os_printf(KERN_NOTICE "tg_ble: WiFi connected, close BLE\r\n");
        ble_set_mode(0, 38);       /* 关 BLE */
        ble_set_coexist_en(0, 0);
        g_tg_ble_started = 0;
    }
}

#endif /* BLE_SUPPORT */

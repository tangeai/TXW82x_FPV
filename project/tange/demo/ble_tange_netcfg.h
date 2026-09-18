#ifndef _BLE_TANGE_NETCFG_H_
#define _BLE_TANGE_NETCFG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 探鸽云 BLE 蓝牙配网 (走 GATT 通道 + 探鸽 SDK 标准流程)
 *
 * 协议: 《蓝牙配网的应用层协议.md》
 *   数据包 = 8 字节包头 + Length 字节数据 (全部小端)
 *   包头   = Command(2B) + SN(2B) + Length(4B)
 *   配网请求 Command = 0x8006, 数据部分透传给 TciProcessRegInfo()
 *   应答     Command = 0x8007, 数据 = Tcis_SetWifiResp{int result; u8 rsv[4]}
 *
 * 与 AP 热点配网并存: GATT 只是数据通道, 配网主流程仍是 IpcStep2 的
 * TciConfigWifi(GWM_AP) — 收到 BLE 配网包调 TciProcessRegInfo 释放其信号量。
 *
 * 使用: 在 IpcStep1() 之后 (g_Lic.uuid 已加载)、未注册时调用一次:
 *   tg_ble_netcfg_init();
 */

/* 初始化探鸽 BLE 配网: 设蓝牙名 AICAM_<uuid> + 注册 GATT 服务 + 开广播.
 * 内部判断 g_IpcParam.flags 是否已注册, 已注册则不启动 BLE 配网。
 * @return 0 启动成功 / 负值未启动 (已注册或 BLE 不可用) */
int tg_ble_netcfg_init(const char* uuid);

/* 系统事件处理: 配网成功 (SYSEVT_WIFI_CONNECTTED) 后关闭 BLE 省电.
 * 在 events.c 的 sys_event_hdl() 里调用 (仿 sys_event_hdl_dhcp)。 */
void tg_ble_netcfg_event(uint32 event_id, uint32 data, uint32 priv);

#ifdef __cplusplus
}
#endif
#endif /* _BLE_TANGE_NETCFG_H_ */

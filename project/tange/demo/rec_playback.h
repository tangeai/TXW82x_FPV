#ifndef _REC_PLAYBACK_H_
#define _REC_PLAYBACK_H_

#include "basic_include.h"
#include "TgCloudCmd.h"   // STimeDay, SAvExEvent, Tcis_ExListEventResp

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * SD 卡录像 + 探鸽 P2P 回放模块
 *
 * 使用流程 (在 fpv_app_init() 中调用):
 *   sd_open();
 *   fatfs_register();
 *   rec_playback_init();
 *
 * 录像模式切换:
 *   rec_set_mode(REC_MODE_ALL_DAY);   // 连续录像(默认)
 *   rec_set_mode(REC_MODE_ALARM);     // 仅报警录像
 *   rec_set_mode(REC_MODE_OFF);       // 关闭录像
 *
 * 报警触发(由外部应用/移动侦测回调调用):
 *   rec_trigger_alarm(ECEVENT_MOTION_DETECTED);
 *
 * 所有 P2P 命令通过 project/icam365/demo.c 里的 Handle_P2p_Cmd 分发
 * 到本模块的 pb_* / rec_* / sd_* API.
 ******************************************************************************/

/* 录像模式 */
typedef enum {
    REC_MODE_OFF     = 0,  /* 不录像 */
    REC_MODE_ALARM   = 1,  /* 仅报警触发录像, 单文件 30s, 30s 内再次触发延长到 60s */
    REC_MODE_ALL_DAY = 2,  /* 连续录像, 单文件 60s */
} rec_mode_t;

/* 单文件时长 (秒) */
#define REC_FILE_SEC_ALL_DAY    60
#define REC_FILE_SEC_ALARM      30
#define REC_FILE_SEC_ALARM_EXT  60   /* 延长后时长 */

/* SD 卡目录 */
#define REC_ROOT_PATH           "0:/REC"

/* 录像扩展名 */
#define REC_EXT_NAME            ".MP4"

/* 循环录像 SD 卡剩余容量水位 (MB), 低于此值删最旧文件.
 *   主码流 (REC_STREAM_TYPE=0): 数据量大 (1080P 15MB/min),
 *     阈值放大到 1024MB ≈ 68 分钟缓冲, 避免清理过于频繁
 *   子码流 (REC_STREAM_TYPE=1): 数据量小 (250kbps 1.8MB/min),
 *     256MB ≈ 2.4 小时缓冲, 足够 */
#if defined(REC_STREAM_TYPE) && (REC_STREAM_TYPE == 0)
#define REC_LOOP_REMAIN_MB      1024
#else
#define REC_LOOP_REMAIN_MB      256
#endif

/* ========== 初始化 ========== */

/**
 * @brief 初始化录像 + 回放模块
 *        包括 SD 卡路径准备、启动文件扫描线程、根据默认模式启动录像
 * @return 0 成功, 负值失败
 */
int rec_playback_init(void);

/* ========== 录像控制 ========== */

/**
 * @brief 设置录像模式
 * @param mode REC_MODE_OFF / REC_MODE_ALARM / REC_MODE_ALL_DAY
 * @return 0 成功
 */
int rec_set_mode(rec_mode_t mode);

/**
 * @brief 获取当前录像模式
 */
rec_mode_t rec_get_mode(void);

/**
 * @brief 外部报警触发录像
 * 行为:
 *   REC_MODE_OFF   : 忽略
 *   REC_MODE_ALARM :
 *     - 未录像中    → 立即开始录像, 文件名带 event_type, 时长 30s
 *     - 录像中 <30s → 延长到 60s (30s 内再次触发)
 *     - 已延长到 60s → 忽略
 *   REC_MODE_ALL_DAY:
 *     - 标记当前正在录的文件 event_type (重命名)
 *
 * @param event_type  ECEVENT 类型, 参考 ec_const.h
 *                    常用: ECEVENT_MOTION_DETECTED (1), ECEVENT_HUMAN_BODY (2),
 *                          ECEVENT_SOUND (3), ECEVENT_PIR (4)
 * @return 0 成功, 负值失败
 */
int rec_trigger_alarm(uint8_t event_type);

/* ========== P2P 回放控制 (由 Handle_P2p_Cmd 调用) ========== */

typedef void *p2phandle_t_;   /* 避免重复包含 TgCloudApi.h */

/**
 * @brief 开始回放 (响应 TCIC_RECORD_PLAY_START)
 * @param handle    探鸽 p2p handle
 * @param t_seek    UTC 秒, 要回放的起始时间
 * @param mode_bit  bit1=0: 连续模式(文件播完自动跳下一个);
 *                  bit1=1: 事件模式(文件播完发 EndOfEvent 暂停)
 * @return 0 成功
 */
int pb_start(void *handle, uint32_t t_seek, uint8_t mode_bit);

/**
 * @brief 停止回放 (响应 TCIC_RECORD_PLAY_STOP)
 */
int pb_stop(void);

/**
 * @brief 暂停 (响应 TCIC_RECORD_PLAY_PAUSE)
 */
int pb_pause(void);

/**
 * @brief 继续 (响应 TCIC_RECORD_PLAY_CONTINUE)
 */
int pb_resume(void);

/**
 * @brief 设置快进速度 (响应 TCIC_RECORD_PLAY_FORWARD)
 * @param param  0: 1x  1: 2x  2: 4x  >2: 只发 I 帧
 */
int pb_set_forward(uint32_t param);

/* ========== 录像文件列表 ========== */

/**
 * @brief 获取指定时间范围内的录像列表, 相邻同类事件会合并
 * @param t_start    UTC 秒, 开始时间
 * @param t_end      UTC 秒, 结束时间
 * @param out_items  [out] 返回的 SAvExEvent 数组指针(由内部 _os_malloc_psram 分配)
 * @return 返回条目数, <0 失败; out_items 需要调用者 _os_free_psram 释放
 */
int rec_list_get(uint32_t t_start, uint32_t t_end, SAvExEvent **out_items);

/**
 * @brief 获取 SD 卡上有录像的日期列表 (扫描 REC_ROOT_PATH 下的 YYYYMMDD 子目录)
 *        响应 TCI_CMD_LIST_RECORDDAYS
 * @param out_days  [out] 返回的 SDay 数组指针(由内部 _os_malloc_psram 分配)
 * @return 返回日期数, <0 失败; out_days 需要调用者 _os_free_psram 释放
 */
int rec_list_days_get(SDay **out_days);

/* ========== SD 卡管理 ========== */

/**
 * @brief 获取 SD 卡容量信息 (MB)
 * @param total  [out] 总容量 MB
 * @param free   [out] 剩余 MB
 * @return 0 成功
 */
int sd_get_capacity(uint32_t *total, uint32_t *free);

/**
 * @brief 格式化 SD 卡 (FAT32 或 EXFAT, 根据 ffconf.h 配置)
 * 内部会先停止录像, 格式化完成后根据当前模式恢复录像
 */
void sd_format(void*arg);

/**
 * @brief NTP/平台时间首次同步成功时调用 (在 set_time 回调里调).
 *        ！！！只记录基准 + 置 pending 标志, 不做任何 IO/rename, 立即返回,
 *        避免阻塞 set_time 回调. 真正的 UNSYNC 文件迁移由 rec_bootstrap_thread
 *        异步执行.
 * @param utc 同步到的真实 UTC 秒
 */
void rec_on_time_synced(uint32_t utc);

/* ========== 兼容保留 (按需扫描模式下为空实现) ========== */
int rec_index_rebuild(void);

#ifdef __cplusplus
}
#endif
#endif /* _REC_PLAYBACK_H_ */

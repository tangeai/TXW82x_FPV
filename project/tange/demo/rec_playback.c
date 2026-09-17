/*******************************************************************************
 * SD 卡录像 + 探鸽 P2P 回放实现 (按需扫描版)
 *
 * 文件命名:  0:/REC/YYYYMMDD/HHMMSS_Eee_dd.MP4
 *                                   ↑ ↑
 *                              事件类型(ECEVENT) 时长(秒)
 * 例:
 *   0:/REC/20260423/153045_E00_60.MP4    连续录 60s
 *   0:/REC/20260423/153100_E01_30.MP4    motion 报警 30s
 *
 * 不维护常驻索引, 查询时按需扫描 SD 卡目录.
 * 回放跨文件只在当前日期目录内查找; 找不到下一个就 EndOfEvent.
 ******************************************************************************/

#include "basic_include.h"
#include "project_config.h"      /* 确保 __TXW826__ / __TXW828__ 宏可见 */
#include "osal/string.h"
#include "lib/multimedia/msi.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "lib/fs/fatfs/osal_file.h"
#include "lib/fs/fatfs/ff.h"
#include "lib/audio/audio_code/audio_code.h"
#include "stream_define.h"
#include "app/recorder/file_process.h"
#include "app/video_app/file_common_api.h"
#include "app/audio_msi/audio_adc.h"
#include "app/audio_media_ctrl/audio_code_ctrl.h"
#include "TgCloudApi.h"
#include "TgCloudCmd.h"  /* 42229 SDK Tcis_FormatExtStorageResp / TCI_CMD_FORMATEXTSTORAGE_RESP 在这里 */
#include "ec_const.h"
#include "rec_playback.h"

/* 卡录像 H264 framebuff stype: 按芯片 + 主/子码流 4 个组合.
 * 由 project_config.h 的 REC_STREAM_TYPE 选 (0=主, 1=子, 默认 1):
 *   826 主码流: VPP_DATA0 直出 YUV → H264, stype=FSTYPE_H264_VPP_DATA0  (1280x720)
 *   826 子码流: VPP → gen420 硬件重整 YUV → H264, stype=FSTYPE_H264_GEN420_DATA
 *   828 主码流: VPP_DATA0 直出 YUV → H264, stype=FSTYPE_H264_VPP_DATA0  (1920x1080)
 *   828 子码流: VPP_DATA1 直出 YUV → H264, stype=FSTYPE_H264_VPP_DATA1  (640x360)
 * 用于 mp4_encode_msi2_init 的 filter_type 和 action 回调里的 fb 过滤. */
#ifndef REC_STREAM_TYPE
#define REC_STREAM_TYPE  1     /* 兜底: project_config.h 没定义时按子码流走 */
#endif

#if defined(__TXW826__)
  #if REC_STREAM_TYPE == 0
    #define REC_STREAM_STYPE    FSTYPE_H264_VPP_DATA0
  #else
    #define REC_STREAM_STYPE    FSTYPE_H264_GEN420_DATA
  #endif
#else  /* TXW828 */
  #if REC_STREAM_TYPE == 0
    #define REC_STREAM_STYPE    FSTYPE_H264_VPP_DATA0
  #else
    #define REC_STREAM_STYPE    FSTYPE_H264_VPP_DATA1
  #endif
#endif
/* 旧别名: 之前代码用 REC_SUB_STREAM_STYPE, 保留作为向后兼容, 指向同一个值 */
#define REC_SUB_STREAM_STYPE    REC_STREAM_STYPE

/* 音频转码: AAC -> PCM -> G.711A.
 * APP 端仅支持 G.711A, MP4 里存 AAC 节省卡空间, 回放时实时转码 */
extern uint8_t linear2alaw(short pcm);

/* 系统不支持时区, 自定义全局时区偏移 (秒).
 * 语义: local_secs = utc_secs + _tg_timezone_, 北京时区 = 28800 */
extern long _tg_timezone_;

/* 录像列表/日期扫描用的内存池按芯片分化:
 *   826: PSRAM 仅 4MB 极度紧张, 扫描期间的临时数组挪到 SRAM (相对富余)
 *   828: PSRAM 宽裕, SRAM 紧张, 保持走 PSRAM
 * 注意: MALLOC 和 FREE 必须同池, 跨池 free 会破坏堆元数据 */
#if defined(__TXW826__)
#define RP_MALLOC(sz)       _os_malloc(sz)
#define RP_FREE(p)          do { if(p) { _os_free(p); (p)=NULL; } } while(0)
#else
#define RP_MALLOC(sz)       _os_malloc_psram(sz)
#define RP_FREE(p)          do { if(p) { _os_free_psram(p); (p)=NULL; } } while(0)
#endif

/* =========================================================================
 * 外部依赖声明
 * ========================================================================= */
extern struct msi *mp4_encode_msi2_init(const char *mp4_msi_name, uint8_t srcID, uint8_t filter_type,
                                        uint8_t rec_time, uint32_t audio_encode,
                                        struct file_process *file_process, uint8_t mode);
extern struct msi *mp4_demux_msi_init(const char *msi_name, const char *filename);

/* =========================================================================
 * 全局状态
 * ========================================================================= */

/* 录像模式 - 全局变量, 默认全天连续录像 */
static volatile rec_mode_t g_rec_mode = REC_MODE_ALL_DAY;

/* 是否已经初始化 */
static volatile uint8_t g_rp_inited = 0;

/* sd_format 进行中标志: 阻塞 rec_bootstrap_thread 的预删除循环, 防止跟 f_mkfs 抢 SD.
 * sd_format 入口设 1, 出口设 0; 预删除循环每轮开头检查. */
static volatile uint8_t g_sd_formatting = 0;

/* 正在删除的文件名/日期目录快照. rec_recycle_oldest 进入 osal_unlink 前设置,
 * 删完立即清空. scan_day_dir 在两遍扫描里都跳过该名字, 这样:
 *   - APP 调 rec_list_get 拿到的列表不含将被删的文件
 *   - pb_locate_file (回放定位) 也会跳过它
 * 避免 "APP 看到 X → 几百 ms 后请求播放 X → fopen 失败" 的竞态.
 * 注意: 名字+目录两个一起匹配, 防止误伤同名跨日文件. */
static volatile char g_recycling_fname[FILE_NAME_LEN + 1] = {0};
static volatile char g_recycling_date_dir[16] = {0};

/* 保护 rec_recycle_oldest 的并发调用. 两个潜在调用者:
 *   1) rec_bootstrap_thread (BELOW_NORMAL): 预删除循环
 *   2) mp4_encode_thread    (ABOVE_NORMAL): rec_create_file_cb 兜底删除
 * 不加锁两路并发会让 FatFS f_unlink 状态错乱, scan_day_dir 选到相同 oldest 文件,
 * g_recycling_fname 也会互相覆盖. mutex 把整个 rec_recycle_oldest 串行化. */
static struct os_mutex g_recycle_lock;
static volatile uint8_t g_recycle_lock_inited = 0;

/* 回放是否在跑的前向访问器: rec_bootstrap_thread 定义在 g_pb (line ~1317) 之前,
 * 不能直接读 g_pb.thread_alive, 通过这个函数转一手, 实现在文件底部. */
static int rec_pb_is_active(void);

/* SD 卡不可恢复故障标志: 连续 REC_UNRECOV_FAIL_LIMIT 次 rec_create_file_cb
 * 返回 NULL 后置 1. 一旦置 1:
 *   - rec_bootstrap_thread 主动停录像 (_rp_record_stop)
 *   - 每 3 秒打印告警, 同时 sd_get_capacity 探测 SD 卡是否自恢复
 *   - 自恢复后清 0, 重启录像
 * 用户也可调 sd_format 强制清状态 (sd_format 内会清这个标志).
 * 设计判定: 看 rec_create_file 失败累计 (最顶层信号), 不看 fwrite/fclose. */
#define REC_UNRECOV_FAIL_LIMIT  3U    /* 连续 3 次创建失败 = 不可恢复 (约 3 分钟内确认故障) */
#define REC_UNRECOV_ALARM_MS    3000U /* 告警打印间隔 */
static volatile uint8_t g_sd_unrecoverable = 0;
/* rec_create_file_cb 连续返 NULL 计数. 文件级 static, 让 rec_create_fail_inc
 * helper 也能访问 (helper 同文件 static 函数). */
static uint32_t g_create_null_streak = 0;

/* rec_create_file_cb 失败 (return NULL) 时统一调这个 helper:
 *   - 累计连续失败次数
 *   - 达到 REC_UNRECOV_FAIL_LIMIT 时设 g_sd_unrecoverable=1, 触发 bootstrap
 *     线程停录像 + 告警轮询
 * 成功 fopen mp4 时调 rec_create_fail_reset() 清零. */
static void rec_create_fail_inc(void)
{
    g_create_null_streak++;
    if (g_create_null_streak >= REC_UNRECOV_FAIL_LIMIT && !g_sd_unrecoverable) {
        g_sd_unrecoverable = 1;
        os_printf(KERN_ERR "rec_create_file: SD card UNRECOVERABLE (%u consecutive NULL returns), "
                           "bootstrap_thread will stop recording and watch for self-heal\n",
                  (unsigned)g_create_null_streak);
    }
}
static inline void rec_create_fail_reset(void)
{
    if (g_create_null_streak) {
        os_printf(KERN_INFO "rec_create_file: SD healthy again, clear null_streak=%u\n",
                  (unsigned)g_create_null_streak);
        g_create_null_streak = 0;
    }
}

/* =========================================================================
 * 断网启动录像 (UNSYNC 模式)
 *
 * 需求: 设备启动时若还没等到 NTP/平台时间同步 (time_sync_flag=0), 也要能录像,
 * 不能因为断网就永远不录. 但设备没有 RTC, 时间没同步时 time(NULL) 是开机相对
 * 时间 (从固定基准走), 直接拿来命名会生成错误日期的文件, NTP 来了又跳变, 乱套.
 *
 * 方案: 时间没同步时进入 UNSYNC 模式, 文件录到独立目录 0:/REC/UNSYNC/, 文件名
 * 用开机秒 boot_sec (单调递增可排序). NTP 同步那一刻记基准 (g_sync_utc,
 * g_sync_boot_sec), 由 bootstrap_thread 把 UNSYNC 目录文件按
 *   真实UTC = g_sync_utc - (g_sync_boot_sec - file_boot_sec)
 * 还原到真实 YYYYMMDD/HHMMSS 路径 (相对时间在同一开机会话内是准的).
 *
 * 重启残留 (上次没等到同步就断电): boot_sec 基准随重启丢失, 时间无法还原,
 * 直接整目录删除 (不保留, 时间不可知的录像留着也无意义).
 * ========================================================================= */
#define REC_UNSYNC_DIR          "0:/REC/UNSYNC"   /* 未同步录像临时目录 */
#define REC_TIME_SYNC_WAIT_MS   15000U            /* 启动等时间同步超时, 超时进 UNSYNC 模式 */

static volatile uint8_t  g_rec_unsync_mode  = 0;  /* 1=当前 UNSYNC 模式录像 (文件录到 UNSYNC 目录) */
static volatile uint8_t  g_sync_pending     = 0;  /* set_time 置 1, 通知 bootstrap_thread 做 migrate */
static volatile uint32_t g_sync_utc         = 0;  /* NTP 同步时刻的真实 UTC 秒 */
static volatile uint32_t g_sync_boot_sec    = 0;  /* NTP 同步时刻的开机秒 (os_jiffies) */

/* 1=有 UNSYNC 录像待迁移到真实时间目录. 由 mp4_encode_thread 在同步后第一次切
 * 文件 (rec_create_file_cb 走真实路径) 时置位, 或 bootstrap 在"同步时无活跃录像"
 * 时直接置位; bootstrap 线程检测后异步执行 rec_unsync_migrate_all.
 * 关键: 迁移绝不在录像活跃时 stop/destroy msi —— msi_destroy 是异步引用计数销毁,
 * 返回时 mp4_encode 线程未必退出, 紧接着 migrate/restart 会在 os_event_del/free 与
 * 线程 os_event_wait 之间踩到 use-after-free (assert evt->magic). 必须等编码线程
 * 自然切到真实目录、旧 UNSYNC 文件 fclose 后再 rename. */
static volatile uint8_t  g_unsync_need_migrate = 0;

/* NTP 时间首次同步回调 (set_time 里调). 只记基准 + 置标志, 零 IO 零阻塞,
 * 真正的 UNSYNC 文件迁移交给 rec_bootstrap_thread 异步做. */
void rec_on_time_synced(uint32_t utc)
{
    if (g_sync_pending) return;   /* 已记过, 防重复 (set_time 可能被多次调) */
    g_sync_utc      = utc;
    g_sync_boot_sec = (uint32_t)(os_jiffies_to_msecs(os_jiffies()) / 1000);
    g_sync_pending  = 1;
}

/* MSI */
static struct msi *g_rec_msi     = NULL;   /* MP4 编码 msi (仅视频轨) */
static struct msi *g_rec_h264    = NULL;   /* 上游 AUTO_H264 */
static struct msi *g_rec_alaw    = NULL;   /* PCM → G.711A 转码 + 写 .alaw 文件 msi */

/* 音频独立文件方案说明:
 *   录像时 MP4 只存视频轨, 音频独立存成 .alaw 文件, 和 mp4 同名不同后缀.
 *   8kHz mono × 1B = 8KB/s = 480KB/min, 和 mp4 (视频) 并列存储.
 *   回放时 pb_thread 同时打开 mp4 (视频) 和 .alaw (音频), 按视频 ts 节奏各自读取.
 *   好处: 避免 AAC 解码器连续调用的状态问题, 音频直接 G711A 透传给 APP 不再转码. */
#define REC_AUDIO_EXT_NAME   ".alaw"       /* 音频文件扩展名 */
static void *g_rec_alaw_fp = NULL;         /* 当前 .alaw 写入文件句柄 (os_mutex_lock 保护) */
static struct os_mutex g_rec_alaw_lock;    /* 保护 g_rec_alaw_fp 切换 */
static uint8_t g_rec_alaw_lock_inited = 0;

/* 当前正在录的文件信息 (用于报警延长/标记事件类型) */
static struct {
    char      fname[FILE_NAME_LEN + 1];    /* 当前文件名 (不含路径) */
    char      fpath[96];                   /* 当前完整路径 */
    char      alaw_fpath[96];              /* 对应 .alaw 文件完整路径 */
    uint32_t  t_start;                     /* 开始时刻 UTC */
    uint8_t   event;                       /* 事件类型 */
    uint8_t   duration_sec;                /* 计划时长 */
    uint8_t   extended;                    /* 是否已经延长过 */
} g_curfile;

/* =========================================================================
 * 工具函数
 * ========================================================================= */

/* 从文件名解析事件类型和时长: HHMMSS_Eee_dd.MP4 */
static int parse_filename(const char *name, uint8_t *event, uint16_t *duration,
                          uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    int h, m, s, e, d;
    if (os_strlen(name) < 17)
        return -1;
    if (sscanf(name, "%02d%02d%02d_E%02d_%d", &h, &m, &s, &e, &d) != 5)
        return -1;
    if (hh) *hh = h;
    if (mm) *mm = m;
    if (ss) *ss = s;
    if (event) *event = e;
    if (duration) *duration = d;
    return 0;
}

/* 从日期目录名解析年月日: YYYYMMDD */
static int parse_dirname(const char *name, uint16_t *year, uint8_t *mon, uint8_t *day)
{
    int y, mm, dd;
    if (os_strlen(name) != 8)
        return -1;
    if (sscanf(name, "%04d%02d%02d", &y, &mm, &dd) != 3)
        return -1;
    if (year) *year = y;
    if (mon) *mon = mm;
    if (day) *day = dd;
    return 0;
}

/* 时间语义统一: 所有文件名/目录名按"本地时间"命名 (和 APP 下发的时间语义一致).
 * 系统 gmtime/mktime 是纯 UTC 数学转换 (不考虑时区), 自己叠加 _tg_timezone_ 做时区偏移.
 *
 *   utc_to_dir/time(utc)   : 输入 UTC 秒  → 输出本地时间目录/HMS
 *   tmval_to_utc(y..s)     : 输入本地年月日时分秒  → 输出 UTC 秒 */

/* UTC 秒 → 本地日期目录 YYYYMMDD */
static void utc_to_dir(uint32_t utc, char *buf, int buf_sz)
{
    time_t t = (time_t)((long) utc + _tg_timezone_);   /* UTC → local 秒 */
    struct tm *tm = gmtime(&t);                         /* gmtime 做纯数学分解 */
    if (tm) {
        os_snprintf(buf, buf_sz, "%04d%02d%02d",
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    } else {
        os_strncpy(buf, "00000000", buf_sz);
    }
}

/* UTC 秒 → 本地 HHMMSS */
static void utc_to_time(uint32_t utc, char *buf, int buf_sz)
{
    time_t t = (time_t)((long) utc + _tg_timezone_);
    struct tm *tm = gmtime(&t);
    if (tm) {
        os_snprintf(buf, buf_sz, "%02d%02d%02d",
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        os_strncpy(buf, "000000", buf_sz);
    }
}

/* 本地 年月日时分秒 → UTC 秒 */
static uint32_t tmval_to_utc(uint16_t y, uint8_t mon, uint8_t day,
                             uint8_t h, uint8_t m, uint8_t s)
{
    struct tm t;
    os_memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    t.tm_hour = h;
    t.tm_min  = m;
    t.tm_sec  = s;
    time_t local = mktime(&t);          /* 纯数学合成, 得到本地秒 */
    return (uint32_t)((long) local - _tg_timezone_);  /* 本地 → UTC */
}

/* =========================================================================
 * 文件扫描辅助 (按需, 不维护常驻索引)
 * ========================================================================= */

/* 单次查询的临时文件条目.
 * !!! 不存 fname 字符串, 节省内存:
 *     原 fname[17] 使单条 scan_item_t ≈ 28B, 1440 条 = 40KB PSRAM,
 *     在碎片严重时申请失败. 改存 hh/mm/ss 配合 event/duration, 运行时
 *     用 build_fname_from_item() 按需重建 "HHMMSS_Eee_dd.MP4" 字符串.
 *     新结构 12B, 1440 条 ≈ 17KB, 省 23KB. */
typedef struct {
    uint32_t t_start;       /* UTC 秒 */
    uint16_t duration;      /* 秒 */
    uint8_t  event;
    uint8_t  hh, mm, ss;    /* 重建文件名用 */
} scan_item_t;

/* 从 scan_item_t 重建文件名字符串 "HHMMSS_Eee_dd.MP4".
 * buf 至少 FILE_NAME_LEN+1 字节 */
static void build_fname_from_item(const scan_item_t *it, char *buf, int buf_sz)
{
    os_snprintf(buf, buf_sz, "%02u%02u%02u_E%02u_%u%s",
                it->hh, it->mm, it->ss, it->event, it->duration, REC_EXT_NAME);
}

static int cmp_scan_item(const void *a, const void *b)
{
    const scan_item_t *ia = (const scan_item_t *) a;
    const scan_item_t *ib = (const scan_item_t *) b;
    if (ia->t_start < ib->t_start) return -1;
    if (ia->t_start > ib->t_start) return 1;
    return 0;
}

/* 扫描指定日期目录, 返回按时间排序的文件条目数组.
 * @param date_dir  YYYYMMDD, 如 "20260423"
 * @param out_arr   [out] 分配的条目数组, 需要调用者 RP_FREE 释放
 * @param out_cnt   [out] 条目数
 * @return 0 成功
 */
static int scan_day_dir(const char *date_dir, scan_item_t **out_arr, uint32_t *out_cnt)
{
    *out_arr = NULL;
    *out_cnt = 0;

    char sub_path[64];
    os_snprintf(sub_path, sizeof(sub_path), "%s/%s", REC_ROOT_PATH, date_dir);

    void *d = osal_opendir(sub_path);
    if (!d) return -1;

    uint16_t y;
    uint8_t  mon, day;
    if (parse_dirname(date_dir, &y, &mon, &day) != 0) {
        osal_closedir(d);
        return -1;
    }

    /* 过滤"正在写入的当前录像文件" — 未完成关闭的 MP4 box 结构不完整,
     * 如果回放打开它, mp4_demux 解析会失败, 或者读到尚未 fsync 的脏数据.
     * 录像启动后 g_curfile.fname/fpath 一直指向"当前正在写"的那个文件,
     * 切分新文件时会更新. rec_create_file_cb 先 fclose 旧文件再 fopen 新的,
     * 所以只有"与 g_curfile 完全同名同路径"的才算未完成.
     * !!! g_curfile 仅在录像模式激活时有效; 未激活时 fname 为空字符串 */
    char cur_fname_snapshot[FILE_NAME_LEN + 1] = {0};
    char cur_fpath_snapshot[96] = {0};
    if (g_rec_msi) {
        os_strncpy(cur_fname_snapshot, g_curfile.fname, sizeof(cur_fname_snapshot) - 1);
        os_strncpy(cur_fpath_snapshot, g_curfile.fpath, sizeof(cur_fpath_snapshot) - 1);
    }

    /* 过滤"正在被预删除线程 unlink 的文件" - 见 g_recycling_fname 定义注释.
     * 同样快照后只读, 避免和 rec_recycle_oldest 写入时的竞态. */
    char recy_fname_snapshot[FILE_NAME_LEN + 1] = {0};
    char recy_dir_snapshot[16] = {0};
    os_strncpy(recy_fname_snapshot, (const char *)g_recycling_fname,
               sizeof(recy_fname_snapshot) - 1);
    os_strncpy(recy_dir_snapshot,   (const char *)g_recycling_date_dir,
               sizeof(recy_dir_snapshot) - 1);

    /* 先数一遍 */
    uint32_t cnt = 0;
    void *fno;
    while ((fno = osal_readdir(d)) != NULL) {
        char *fname = osal_dirent_name(fno);
        if (!fname || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fname);
        if (nlen < 5 || os_strcasecmp(fname + nlen - 4, REC_EXT_NAME) != 0) continue;
        uint8_t ev; uint16_t dur; uint8_t hh, mm, ss;
        if (parse_filename(fname, &ev, &dur, &hh, &mm, &ss) != 0) continue;
        /* 过滤正在录的文件: fname 匹配 + date_dir 隶属 fpath */
        if (cur_fname_snapshot[0] &&
            os_strcmp(fname, cur_fname_snapshot) == 0 &&
            os_strstr(cur_fpath_snapshot, date_dir) != NULL) {
            continue;
        }
        /* 过滤正在被预删除的文件 (date_dir + fname 双匹配, 防误伤跨日同名) */
        if (recy_fname_snapshot[0] &&
            os_strcmp(fname, recy_fname_snapshot) == 0 &&
            os_strcmp(date_dir, recy_dir_snapshot) == 0) {
            continue;
        }
        cnt++;
    }
    osal_closedir(d);

    if (cnt == 0) return 0;

    scan_item_t *arr = (scan_item_t *) RP_MALLOC(sizeof(scan_item_t) * cnt);
    if (!arr) return -1;

    /* 第二遍收集 */
    d = osal_opendir(sub_path);
    if (!d) { RP_FREE(arr); return -1; }

    uint32_t idx = 0;
    while ((fno = osal_readdir(d)) != NULL && idx < cnt) {
        char *fname = osal_dirent_name(fno);
        if (!fname || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fname);
        if (nlen < 5 || os_strcasecmp(fname + nlen - 4, REC_EXT_NAME) != 0) continue;
        uint8_t ev; uint16_t dur; uint8_t hh, mm, ss;
        if (parse_filename(fname, &ev, &dur, &hh, &mm, &ss) != 0) continue;
        /* 同 pass1 过滤规则, 保持两遍一致 */
        if (cur_fname_snapshot[0] &&
            os_strcmp(fname, cur_fname_snapshot) == 0 &&
            os_strstr(cur_fpath_snapshot, date_dir) != NULL) {
            continue;
        }
        if (recy_fname_snapshot[0] &&
            os_strcmp(fname, recy_fname_snapshot) == 0 &&
            os_strcmp(date_dir, recy_dir_snapshot) == 0) {
            continue;
        }
        arr[idx].t_start  = tmval_to_utc(y, mon, day, hh, mm, ss);
        arr[idx].duration = dur;
        arr[idx].event    = ev;
        arr[idx].hh       = hh;
        arr[idx].mm       = mm;
        arr[idx].ss       = ss;
        idx++;
    }
    osal_closedir(d);

    if (idx > 1)
        qsort(arr, idx, sizeof(scan_item_t), cmp_scan_item);

    *out_arr = arr;
    *out_cnt = idx;
    return 0;
}

/* 在指定日期目录内查找 "下一个文件".
 * @param date_dir    当前日期 "YYYYMMDD"
 * @param cur_fname   当前文件名
 * @param next_fname  [out] 下一个文件名 (需要 ≥ FILE_NAME_LEN+1)
 * @param next_t0     [out] 下一个文件的起始时间
 * @return 0 成功, -1 没有下一个
 */
static int find_next_in_day(const char *date_dir, const char *cur_fname,
                            char *next_fname, uint32_t *next_t0)
{
    scan_item_t *arr = NULL;
    uint32_t cnt = 0;
    if (scan_day_dir(date_dir, &arr, &cnt) != 0 || cnt == 0) {
        if (arr) RP_FREE(arr);
        return -1;
    }
    int  ret = -1;
    char tmp[FILE_NAME_LEN + 1];
    for (uint32_t i = 0; i < cnt - 1; i++) {
        build_fname_from_item(&arr[i], tmp, sizeof(tmp));
        if (os_strcmp(tmp, cur_fname) == 0) {
            build_fname_from_item(&arr[i + 1], next_fname, FILE_NAME_LEN + 1);
            *next_t0 = arr[i + 1].t_start;
            ret = 0;
            break;
        }
    }
    RP_FREE(arr);
    return ret;
}

/* =========================================================================
 * SD 卡管理
 * ========================================================================= */

int sd_get_capacity(uint32_t *total, uint32_t *free)
{
    uint32_t tot_mb = 0, free_mb = 0;
    /* osal_fatfsfree 内部已做 扇区 / 2 / 1024 换算, 返回单位就是 MB */
    FRESULT res = osal_fatfsfree("0:", &tot_mb, &free_mb);
    if (res != FR_OK) {
        os_printf(KERN_ERR "sd_get_capacity: err=%d\n", res);
        if (total) *total = 0;
        if (free) *free = 0;
        return -1;
    }
    if (total) *total = tot_mb;
    if (free) *free = free_mb;
    return 0;
}

/* 循环录像: 找 REC 目录下最早的日期, 再删其最早文件 */
/* 真正的回收逻辑, 必须在 g_recycle_lock 持锁状态下调用. */
static int rec_recycle_oldest_locked(void)
{
    void *root = osal_opendir(REC_ROOT_PATH);
    if (!root) {
        os_printf(KERN_ERR "rec_recycle: opendir(%s) fail, REC root not exist\n", REC_ROOT_PATH);
        return -1;
    }

    char oldest_dir[16] = {0};
    void *fno;
    uint32_t total_dirs = 0, valid_date_dirs = 0;
    while ((fno = osal_readdir(root)) != NULL) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        total_dirs++;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) {
            os_printf(KERN_INFO "rec_recycle: skip dir '%s' (not YYYYMMDD)\n", name);
            continue;
        }
        valid_date_dirs++;
        if (oldest_dir[0] == '\0' || os_strcmp(name, oldest_dir) < 0) {
            os_strncpy(oldest_dir, name, sizeof(oldest_dir) - 1);
            oldest_dir[sizeof(oldest_dir) - 1] = '\0';
        }
    }
    osal_closedir(root);

    if (oldest_dir[0] == '\0') {
        os_printf(KERN_ERR "rec_recycle: no YYYYMMDD dir under %s "
                  "(total dirs=%u, valid=%u). SD may be filled by other files.\n",
                  REC_ROOT_PATH, total_dirs, valid_date_dirs);
        return -1;
    }

    /* 按"单文件粒度"清理 (按需删 - 删一个看一下 free 够不够):
     *   1) 目录里有 MP4 → 删最旧的 1 个 MP4 + 同名 .alaw, return 0
     *      (本日还有 MP4 时, 下次调用继续删本日下一个; 全删完了下次调用 oldest 切到次旧日)
     *   2) 目录里没 MP4 但有孤儿 .alaw → 一次性全删 + 删空目录, return 0
     *      (孤儿 .alaw 单独留着没意义)
     *   3) 目录本来就空 → 删空目录, return -1
     */
    scan_item_t *arr = NULL; uint32_t cnt = 0;
    if (scan_day_dir(oldest_dir, &arr, &cnt) == 0 && cnt > 0) {
        /* === 路径 1: 删最旧的 1 个 MP4 + 同名 .alaw === */
        char full_path[96];
        char fn0[FILE_NAME_LEN + 1];
        build_fname_from_item(&arr[0], fn0, sizeof(fn0));
        os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                    REC_ROOT_PATH, oldest_dir, fn0);

        /* 公告"我要开始删 fn0 了": scan_day_dir 看到此名+目录会跳过, 这样
         * 后续 rec_list_get / pb_locate_file 不会返回这个即将消失的文件 */
        os_strncpy((char *)g_recycling_date_dir, oldest_dir,
                   sizeof(g_recycling_date_dir) - 1);
        os_strncpy((char *)g_recycling_fname,    fn0,
                   sizeof(g_recycling_fname)    - 1);

        FRESULT res = osal_unlink(full_path);
        if (res == FR_OK) {
            os_printf(KERN_INFO "rec_recycle: deleted %s\n", full_path);
        } else {
            os_printf(KERN_ERR "rec_recycle: unlink %s fail res=%d\n", full_path, res);
        }
        /* 同步删除对应 .alaw 文件 (如果存在) */
        int plen = os_strlen(full_path);
        int elen = os_strlen(REC_EXT_NAME);
        if (plen > elen) {
            char alaw_path[96];
            os_snprintf(alaw_path, sizeof(alaw_path), "%.*s%s",
                        plen - elen, full_path, REC_AUDIO_EXT_NAME);
            osal_unlink(alaw_path);    /* 忽略错误: 可能本来就没 .alaw */
        }

        /* 清空"正在删除"快照, 让 scan_day_dir 立即恢复正常 */
        g_recycling_fname[0]    = '\0';
        g_recycling_date_dir[0] = '\0';

        /* 这是最后一个 MP4? 删完目录可能空了, 但目录里可能还有非 IPC 文件,
         * 这里不做目录清理, 留给"路径 2"在下次调用中处理 */
        if (cnt == 1) {
            char full_dir[64];
            os_snprintf(full_dir, sizeof(full_dir), "%s/%s", REC_ROOT_PATH, oldest_dir);
            osal_unlink_dir(full_dir, 0);   /* 空了才会删成功, 不空就保留 */
        }
        RP_FREE(arr);
        return (res == FR_OK) ? 0 : -1;
    }
    if (arr) RP_FREE(arr);

    /* === 路径 2/3: 目录里没 MP4, 扫整个目录处理孤儿 / 判空 === */
    char full_dir[64];
    os_snprintf(full_dir, sizeof(full_dir), "%s/%s", REC_ROOT_PATH, oldest_dir);
    void *d2 = osal_opendir(full_dir);
    uint32_t purged = 0;
    if (d2) {
        void *fno2;
        while ((fno2 = osal_readdir(d2)) != NULL) {
            char *fn2 = osal_dirent_name(fno2);
            if (!fn2 || osal_dirent_isdir(fno2)) continue;
            if (fn2[0] == '.' && (fn2[1] == 0 || (fn2[1] == '.' && fn2[2] == 0))) continue;
            char fp[96];
            os_snprintf(fp, sizeof(fp), "%s/%s", full_dir, fn2);
            if (osal_unlink(fp) == FR_OK) {
                purged++;
                os_printf(KERN_INFO "rec_recycle: purged orphan %s\n", fp);
            } else {
                os_printf(KERN_ERR "rec_recycle: unlink %s fail\n", fp);
            }
        }
        osal_closedir(d2);
    }
    osal_unlink_dir(full_dir, 0);   /* 空了删目录 */
    if (purged > 0) {
        os_printf(KERN_INFO "rec_recycle: oldest dir %s purged %u orphan(s) + removed dir\n",
                  oldest_dir, purged);
        return 0;   /* 删了东西, 让 bootstrap 继续轮询容量 */
    }
    os_printf(KERN_WARNING "rec_recycle: oldest dir %s empty, removed\n", oldest_dir);
    return -1;      /* 目录空, 没腾出空间 */
}

/* 对外入口: 加 mutex 串行化, 避免预删除线程和 mp4_encode_thread 兜底删除并发.
 * 在 g_recycle_lock_inited 之前调用 (启动阶段第一次 cleanup) 直接放行,
 * 因为那时只有 bootstrap 一个线程在跑, 不会有并发. */
static int rec_recycle_oldest(void)
{
    if (!g_recycle_lock_inited) {
        return rec_recycle_oldest_locked();
    }
    os_mutex_lock(&g_recycle_lock, osWaitForever);
    int ret = rec_recycle_oldest_locked();
    os_mutex_unlock(&g_recycle_lock);
    return ret;
}

/* 前向声明 */
int _rp_record_start(uint8_t event_type);
int _rp_record_stop(void);

/* 把 UNSYNC 目录里一个文件 (mp4 或 alaw) 改名到真实时间目录.
 * 仅用于"本会话时间同步"场景, 按 (g_sync_utc, g_sync_boot_sec) 还原真实时间.
 * 跨重启残留 (boot_sec 基准已丢, 时间无法还原) 的文件不走这里, 由调用方直接删除.
 * @param unsync_fname  UNSYNC 目录下文件名, 形如 "<boot_sec>_Eee_dd.MP4/.alaw"
 * @return 0 成功 / -1 跳过
 *
 * 文件名映射: <boot_sec>_Eee_dd.EXT  →  YYYYMMDD/HHMMSS_Eee_dd.EXT
 *   真实UTC = g_sync_utc - (g_sync_boot_sec - boot_sec) */
static int rec_unsync_migrate_one(const char *unsync_fname)
{
    /* 解析 <boot_sec>_Eee_dd.EXT */
    uint32_t boot_sec = 0; int ev = 0, dur = 0;
    char ext[8] = {0};
    /* 先取扩展名 */
    int nlen = os_strlen(unsync_fname);
    if (nlen < 7) return -1;
    const char *dot = NULL;
    for (int i = nlen - 1; i >= 0; i--) { if (unsync_fname[i] == '.') { dot = unsync_fname + i; break; } }
    if (!dot) return -1;
    os_strncpy(ext, dot, sizeof(ext) - 1);   /* ".MP4" 或 ".alaw" */

    if (sscanf(unsync_fname, "%u_E%02d_%d", &boot_sec, &ev, &dur) != 3)
        return -1;

    /* 算目标日期目录 + HHMMSS.
     * 真实 UTC = sync_utc - (sync_boot_sec - boot_sec).
     * boot_sec 早于 sync_boot_sec, 差值为正 */
    char date_dir[16];
    char hms[8];
    int32_t delta_s = (int32_t)(g_sync_boot_sec - boot_sec);
    if (delta_s < 0) delta_s = 0;       /* 防御: 不应为负 */
    uint32_t real_utc = g_sync_utc - (uint32_t)delta_s;
    utc_to_dir(real_utc, date_dir, sizeof(date_dir));
    utc_to_time(real_utc, hms, sizeof(hms));

    /* 确保目标日期目录存在 */
    char dst_dir[48];
    os_snprintf(dst_dir, sizeof(dst_dir), "%s/%s", REC_ROOT_PATH, date_dir);
    void *d = osal_opendir(dst_dir);
    if (d) osal_closedir(d);
    else   osal_fmkdir(dst_dir);

    /* 源 / 目标完整路径 */
    char src_path[96], dst_path[96];
    os_snprintf(src_path, sizeof(src_path), "%s/%s", REC_UNSYNC_DIR, unsync_fname);
    os_snprintf(dst_path, sizeof(dst_path), "%s/%s_E%02d_%02d%s",
                dst_dir, hms, ev, dur, ext);

    FRESULT r = osal_rename(src_path, dst_path);
    if (r != FR_OK) {
        os_printf(KERN_ERR "rec_unsync_migrate: rename %s -> %s fail res=%d\n",
                  src_path, dst_path, r);
        return -1;
    }
    os_printf(KERN_INFO "rec_unsync_migrate: %s -> %s\n", src_path, dst_path);
    return 0;
}

/* 遍历 UNSYNC 目录, 把里面所有文件按真实时间搬到日期目录.
 * 仅用于"本会话时间同步"场景 (g_sync_utc/g_sync_boot_sec 有效).
 * 调用方保证此时没有任何 UNSYNC 文件处于打开状态 (录像已停/已切到真实目录).
 * 跨重启残留场景不调本函数, 由调用方直接删整个 UNSYNC 目录. */
static void rec_unsync_migrate_all(void)
{
    uint32_t moved = 0, total = 0;
    void *fno;
    void *dir;
    /* 注意: 一边 readdir 一边 rename 同目录, FATFS 行为不保证. 这里只 rename
     * 出本目录 (移到别的目录), 当前目录条目变少, 用"反复扫到空"策略更稳:
     * 每轮重新 opendir 扫一遍只搬第一个文件, 直到扫不到文件. UNSYNC 文件数量
     * 有限 (断网时段的录像, 一分钟一个), 多扫几遍开销可接受. */
    while (1) {
        dir = osal_opendir(REC_UNSYNC_DIR);
        if (!dir) break;   /* 目录不存在或已删空 */
        char one_fname[FILE_NAME_LEN + 8] = {0};
        int found = 0;
        while ((fno = osal_readdir(dir)) != NULL) {
            char *fn = osal_dirent_name(fno);
            if (!fn || osal_dirent_isdir(fno)) continue;
            if (fn[0] == '.') continue;
            os_strncpy(one_fname, fn, sizeof(one_fname) - 1);
            found = 1;
            break;
        }
        osal_closedir(dir);
        if (!found) break;

        total++;
        if (rec_unsync_migrate_one(one_fname) == 0) {
            moved++;
        } else {
            /* 迁移失败 (解析不了/rename 失败): 直接 unlink 避免死循环卡在这个文件 */
            char bad[96];
            os_snprintf(bad, sizeof(bad), "%s/%s", REC_UNSYNC_DIR, one_fname);
            osal_unlink(bad);
            os_printf(KERN_WARNING "rec_unsync_migrate: drop unmigratable %s\n", bad);
        }
    }
    /* 迁移完删空的 UNSYNC 目录 (空了才会删成功) */
    osal_unlink_dir(REC_UNSYNC_DIR, 0);
    os_printf(KERN_INFO "rec_unsync_migrate_all: total=%u moved=%u\n",
              (unsigned)total, (unsigned)moved);
}

void sd_format_handle(void*arg)
{
    os_printf(KERN_INFO "sd_format: start\n");

    /* 通知预删除循环让位. 必须在调 _rp_record_stop 前设置, 让 bootstrap
     * 线程在下一轮检查时立刻跳过 SD 操作 */
    g_sd_formatting = 1;

    /* 停止录像 */
    rec_mode_t saved_mode = g_rec_mode;
    g_rec_mode = REC_MODE_OFF;
    _rp_record_stop();

    /* sleep 2s: 之前 500ms 仅给 mp4_encode_thread 收尾; 现在还要给可能正在
     * 跑 osal_unlink 的预删除线程一个完成窗口 (单次 unlink ~500ms),
     * 避免和 f_mkfs 并发操作 SD 卡导致 FatFS 状态错乱 */
    os_sleep_ms(2000);

    /* 进一步严谨: 拿 g_recycle_lock 才进 f_mkfs.
     * 即使上面的 2s sleep 没等到 unlink 完成, 这里也会真正阻塞直到预删除释放
     * 锁. 注意: 持锁期间预删除线程会被阻塞, 必须保证 f_mkfs 后立即 unlock. */
    uint8_t hold_recycle = 0;
    if (g_recycle_lock_inited) {
        os_mutex_lock(&g_recycle_lock, osWaitForever);
        hold_recycle = 1;
    }

    int ret = 0;
    /* 执行格式化 */
    BYTE *work = (BYTE*)_os_malloc_psram(FF_MAX_SS);
    if(!work){
        os_printf(KERN_ERR "sd_format: malloc psram failed\n");
        if (hold_recycle) os_mutex_unlock(&g_recycle_lock);
        g_rec_mode = saved_mode;
        g_sd_formatting = 0;
        ret = -1;
        goto FORMAT_EXIT ;
    }
    FRESULT res = f_mkfs("0:", FM_ANY, 0, work, FF_MAX_SS);
    if (res != FR_OK) {
        os_printf(KERN_ERR "sd_format: f_mkfs failed, res=%d\n", res);
        if (hold_recycle) os_mutex_unlock(&g_recycle_lock);
        g_rec_mode = saved_mode;
        g_sd_formatting = 0;
        ret = -1;
        goto FORMAT_EXIT ;
    }
    os_printf(KERN_INFO "sd_format: done\n");

    /* 重建 REC 根目录 */
    osal_fmkdir(REC_ROOT_PATH);

    /* 释放回收锁: 预删除线程现在可以恢复工作 (但 g_sd_formatting 还为 1,
     * 它在循环开头会再次检查并跳过, 直到下面把 g_sd_formatting 清掉) */
    if (hold_recycle) os_mutex_unlock(&g_recycle_lock);

    /* 恢复录像; 清标志后预删除循环也会重新生效.
     * 顺便清 SD 不可恢复故障标志 — 格式化后 SD 应该可用, 用户期望立刻恢复录像 */
    g_rec_mode = saved_mode;
    g_sd_formatting = 0;
    g_sd_unrecoverable = 0;
    g_create_null_streak = 0;
    if (saved_mode == REC_MODE_ALL_DAY) {
        _rp_record_start(ECEVENT_NONE);
    }

    Tcis_FormatExtStorageResp formatRes;
FORMAT_EXIT:
    if(ret == 0){
        formatRes.storage = 0;
        formatRes.result  = 0;
    }else{
        formatRes.storage = 0;
        formatRes.result  = 1;
    }
    _os_printf("TCI_CMD_FORMATEXTSTORAGE_REQ ret=%d\n", ret);

    void *handle = arg;
    TciSendCmdResp(handle, TCI_CMD_FORMATEXTSTORAGE_RESP, (char *)&formatRes, sizeof(Tcis_FormatExtStorageResp));
    if(work){
        _os_free_psram(work);
    }
    return ;
}
void sd_format(void *arg)
{
    struct os_task format_task;
    OS_TASK_INIT("sd_format", &format_task, sd_format_handle, arg, OS_TASK_PRIORITY_NORMAL, NULL, 2048);
}

/* =========================================================================
 * 模式控制
 * ========================================================================= */

int rec_set_mode(rec_mode_t mode)
{
    if (mode > REC_MODE_ALL_DAY)
        return -1;

    rec_mode_t old = g_rec_mode;
    g_rec_mode = mode;

    os_printf(KERN_INFO "rec_set_mode: %d -> %d\n", old, mode);

    if (old == mode)
        return 0;

    if (mode == REC_MODE_OFF || mode == REC_MODE_ALARM) {
        _rp_record_stop();
    } else if (mode == REC_MODE_ALL_DAY) {
        _rp_record_start(ECEVENT_NONE);
    }
    return 0;
}

rec_mode_t rec_get_mode(void)
{
    return g_rec_mode;
}

/* =========================================================================
 * 模块初始化
 * ========================================================================= */
extern volatile uint8_t time_sync_flag;
/* SDK FatFS mount 标志 (set_fat_ready). 真实挂载入口是 fatfs_test.c 里的 fatfs_register */
extern uint8_t get_fat_isready(void);

/* 启动检测任务: 周期检查 SD 卡是否就绪, 就绪后根据当前模式启动录像 */
static void rec_bootstrap_thread(void *arg)
{
    (void) arg;
    os_printf(KERN_INFO "rec_bootstrap: wait SD card ready...\n");
    int n = 0;
    /* 等待条件分两层:
     *   1. SD 必须就绪 (fat_ready + 有容量): 没卡没法写, 必须死等
     *   2. 时间同步: 等最多 REC_TIME_SYNC_WAIT_MS, 超时进 UNSYNC 模式
     *      (断网启动也要录, 不能因为没同步就永远不录) */
    uint32_t wait_ms = 0;
    while (1) {
        os_sleep_ms(1000);

        if(GetNetworkState() == 1){
            continue;
        }

        wait_ms += 1000;
        uint32_t total_mb = 0, free_mb = 0;
        uint8_t fat_ready = get_fat_isready();
        uint8_t sd_ok = (fat_ready && sd_get_capacity(&total_mb, &free_mb) == 0 && total_mb > 0);

        if (sd_ok && time_sync_flag) {
            /* SD 就绪 + 时间已同步: 正常模式启动 */
            os_printf(KERN_INFO "rec_bootstrap: SD ready + time synced, total=%dMB free=%dMB\n",
                      total_mb, free_mb);
            g_rec_unsync_mode = 0;
            break;
        }
        if (sd_ok && !time_sync_flag && wait_ms >= REC_TIME_SYNC_WAIT_MS) {
            /* SD 就绪但时间同步超时: 进 UNSYNC 模式, 先录起来, 文件录到
             * UNSYNC 目录用开机毫秒命名, 等 NTP 同步后再 migrate 到真实时间 */
            os_printf(KERN_WARNING "rec_bootstrap: SD ready but time NOT synced after %ums, "
                                   "enter UNSYNC mode (record to %s, migrate on time-sync)\n",
                      (unsigned)wait_ms, REC_UNSYNC_DIR);
            g_rec_unsync_mode = 1;
            break;
        }
        if(++n % 5 == 0){
            os_printf(KERN_INFO "waitting SDcard ready, time_sync=%d, fat_ready=%d, total_mb=%d, free_mb=%d\n",
                                                    time_sync_flag, fat_ready, total_mb, free_mb);
        }
    }

    /* SD 就绪, 建立 REC 根目录 */
    void *dir = osal_opendir(REC_ROOT_PATH);
    if (!dir) {
        osal_fmkdir(REC_ROOT_PATH);
    } else {
        osal_closedir(dir);
    }

    /* 处理上次会话残留的 UNSYNC 文件 (上次没等到时间同步就断电, boot_sec 基准
     * 已随重启丢失, 时间无法还原). 直接整目录删除 (不再搬占位日期):
     * 这些文件时间不可知, 留着也无法在 APP 时间轴上正确呈现, 删掉更干净.
     * 注意: 必须在本次录像启动 (可能又往 UNSYNC 写) 之前做完. */
    {
        void *ud = osal_opendir(REC_UNSYNC_DIR);
        if (ud) {
            osal_closedir(ud);
            os_printf(KERN_WARNING "rec_bootstrap: found leftover UNSYNC dir from previous session, "
                                   "time unrecoverable, deleting it\n");
            osal_unlink_dir(REC_UNSYNC_DIR, 1 /*递归删目录内所有文件*/);
        }
    }

    /* 启动前主动清理: 若剩余容量不足, 删旧文件腾空间;
     * 删完还不足(或没有旧文件可删) 则禁用录像并打印警告 */
    uint32_t tot_mb = 0, free_mb = 0;
    sd_get_capacity(&tot_mb, &free_mb);
    os_printf(KERN_INFO "rec_bootstrap: sd total=%dMB free=%dMB, threshold=%dMB\n",
              tot_mb, free_mb, REC_LOOP_REMAIN_MB);

    int cleanup_try = 0;
    /* 单文件粒度: 每次循环删 1 个 MP4 + 同名 .alaw, 容量够了立即停.
     * 上限 5000 防死循环, 一般场景几十次就能达标 */
    while (free_mb < REC_LOOP_REMAIN_MB && cleanup_try++ < 5000) {
        if (rec_recycle_oldest() != 0) {
            os_printf(KERN_ERR "rec_bootstrap: no old files to delete, free=%dMB\n", free_mb);
            break;
        }
        sd_get_capacity(&tot_mb, &free_mb);
    }

    if (free_mb < REC_LOOP_REMAIN_MB) {
        os_printf(KERN_ERR "rec_bootstrap: SD free(%dMB) < threshold(%dMB), recording DISABLED\n",
                  free_mb, REC_LOOP_REMAIN_MB);
        if (free_mb == 0 && cleanup_try > 0) {
            os_printf(KERN_ERR "rec_bootstrap: SD totally full but no recyclable record under %s.\n",
                      REC_ROOT_PATH);
            os_printf(KERN_ERR "rec_bootstrap: SD card likely contains other files. Please format SD or clean up manually.\n");
        } else {
            os_printf(KERN_ERR "rec_bootstrap: please insert a larger SD card\n");
        }
        return;   /* 不启动录像 */
    }
    if (cleanup_try > 0) {
        os_printf(KERN_INFO "rec_bootstrap: cleanup done, free=%dMB\n", free_mb);
    }

    /* 按模式启动录像 */
    if (g_rec_mode == REC_MODE_ALL_DAY) {
        if (_rp_record_start(ECEVENT_NONE) == 0) {
            os_printf(KERN_INFO "rec_bootstrap: recording started (all-day)\n");
        } else {
            os_printf(KERN_ERR "rec_bootstrap: record start failed, retry in 2s\n");
            os_sleep_ms(2000);
            _rp_record_start(ECEVENT_NONE);
        }
    } else if (g_rec_mode == REC_MODE_ALARM) {
        os_printf(KERN_INFO "rec_bootstrap: alarm mode, wait trigger\n");
    }

    /* === 预删除常驻循环 ===
     * 周期性检查 SD 容量, 在 free_mb 触及"预删除阈值"时主动删 1 个最旧 MP4,
     * 把"运行时切文件同步删多文件"的阻塞从 mp4_encode_thread 卸载到这里.
     *
     * 协调点:
     *   - g_sd_formatting: sd_format 进行中, 让位避免和 f_mkfs 抢 SD
     *   - g_pb.thread_alive: 回放进行中, 不删 (可能正读最旧文件)
     *   - g_rec_mode == REC_MODE_OFF: 录像关闭, 没必要预删
     *
     * 触发线 REC_CLEANUP_HIGH_MB = REC_LOOP_REMAIN_MB + 256MB:
     *   主码流 (1024) → 1280MB 开始预删, 给 rec_create_file_cb 留 256MB 余量
     *   子码流  (256) → 512MB  开始预删
     *
     * 单轮只删 1 个文件, unlink ~500ms 内完成, 不会和录像争 SD 太久.
     * 检查间隔 30s: 1080P ~15MB/min 录, 30s 增量 7.5MB, 远低于 256MB 余量,
     * 不会在检查间隙撑爆阈值. */
    #define REC_CLEANUP_CHECK_MS  30000U
    #define REC_CLEANUP_HIGH_MB   (REC_LOOP_REMAIN_MB + 256U)

    os_printf(KERN_INFO "rec_bootstrap: enter cleanup loop, check=%us trigger=%uMB\n",
              (unsigned)(REC_CLEANUP_CHECK_MS / 1000U),
              (unsigned)REC_CLEANUP_HIGH_MB);

    /* 主循环: 3s 间隔, 兼顾 SD 不可恢复故障的 3s 告警节奏.
     * 预删除按 REC_CLEANUP_CHECK_MS 间隔 (用累计 tick 控制), 不每轮都跑 */
    #define REC_TICK_MS         3000
    int32_t cleanup_acc_ms = 0;
    rec_mode_t saved_mode_for_recovery = REC_MODE_OFF;  /* 故障停录像时记下原模式 */

    while (1) {
        os_sleep_ms(REC_TICK_MS);

        /* === NTP 时间同步 → 迁移 UNSYNC 录像到真实时间 (方案 A: 不打断活跃录像) ===
         * 关键教训: 旧实现在这里 _rp_record_stop() + migrate + _rp_record_start(),
         * 但 msi_destroy 是异步引用计数销毁 —— stop 返回时 mp4_encode 线程未必退出,
         * evt 未必还活着. 紧接着的 migrate/restart 在 os_event_del/free 与线程
         * os_event_wait 之间踩到 use-after-free, 触发 assert "evt->magic" 崩溃.
         * 新策略绝不主动 stop/destroy 正在录的 msi (见下方幂等检查 + 迁移). */
        if (g_sync_pending) {
            g_sync_pending = 0;
            os_printf(KERN_INFO "rec_bootstrap: time synced (utc=%u boot_sec=%u)\n",
                      (unsigned)g_sync_utc, (unsigned)g_sync_boot_sec);
        }

        /* 幂等检查 (不依赖 g_sync_pending 边沿, 每轮都查): 时间已同步但仍处于
         * UNSYNC 模式时, 推动退出. 覆盖两种情况:
         *   - ALL_DAY 录像中: 不打断, 等 mp4_encode 线程下次切文件时 rec_create_file_cb
         *     自动走真实目录并自己退出 UNSYNC 模式 (这里什么都不用做, 只是兜底:
         *     万一切文件那刻没退成, 下面"无活跃录像"条件迟早会命中).
         *   - 无活跃录像 (模式 OFF / 报警录完已 _rp_record_stop, g_rec_msi=NULL):
         *     不会再触发 rec_create_file_cb, 必须在这里主动退出模式 + 排迁移.
         *     此时没有任何线程在写 UNSYNC, rename 安全. */
        if (g_rec_unsync_mode && time_sync_flag) {
            if (g_rec_mode == REC_MODE_OFF || !g_rec_msi) {
                g_rec_unsync_mode     = 0;
                g_unsync_need_migrate = 1;
                os_printf(KERN_INFO "rec_bootstrap: no active UNSYNC rec, exit UNSYNC mode, "
                                   "migrate pending\n");
            }
            /* 否则: ALL_DAY 录像活跃中, 交给 rec_create_file_cb 在切文件时退出,
             * 不在此打断 (避免 msi_destroy 异步竞态) */
        }

        /* === 执行 UNSYNC 迁移 (标志由 rec_create_file_cb 或上面置位) ===
         * 到这里保证: 已退出 UNSYNC 模式, 编码线程 (若在录) 已在真实目录写新文件,
         * UNSYNC 目录里的旧文件全部 fclose, 可安全 rename. 不碰任何 msi. */
        if (g_unsync_need_migrate) {
            g_unsync_need_migrate = 0;
            os_printf(KERN_INFO "rec_bootstrap: migrating UNSYNC files to real time dir\n");
            rec_unsync_migrate_all();
            os_printf(KERN_INFO "rec_bootstrap: UNSYNC migrate done (no record interruption)\n");
            continue;
        }

        /* === 优先处理: SD 不可恢复故障监控 === */
        if (g_sd_unrecoverable) {
            /* 持续打告警 (3s 间隔, 由 sleep 节奏保证) */
            os_printf(KERN_ERR "[SD_FAULT] unrecoverable, recording disabled. "
                              "null_streak=%u, rec_mode=%d. Will auto-recover when SD readable\n",
                      (unsigned)g_create_null_streak, g_rec_mode);

            /* 第一次进来: 主动停录像, 记下原模式 */
            if (g_rec_mode != REC_MODE_OFF && g_rec_msi) {
                saved_mode_for_recovery = g_rec_mode;
                g_rec_mode = REC_MODE_OFF;
                _rp_record_stop();
                os_printf(KERN_ERR "[SD_FAULT] stopped recording, saved mode=%d for recovery\n",
                          saved_mode_for_recovery);
            }

            /* 探测自恢复: SD 卡能读容量且非 0 → 视为恢复 */
            uint32_t rtot = 0, rfre = 0;
            if (sd_get_capacity(&rtot, &rfre) == 0 && rtot > 0) {
                os_printf(KERN_WARNING "[SD_FAULT] SD readable again (tot=%uMB free=%uMB), "
                                       "clear unrecoverable flag and resume recording\n",
                          (unsigned)rtot, (unsigned)rfre);
                g_sd_unrecoverable = 0;
                g_create_null_streak = 0;
                if (saved_mode_for_recovery != REC_MODE_OFF) {
                    g_rec_mode = saved_mode_for_recovery;
                    saved_mode_for_recovery = REC_MODE_OFF;
                    if (g_rec_mode == REC_MODE_ALL_DAY) {
                        _rp_record_start(ECEVENT_NONE);
                    }
                }
            }
            continue;
        }

        /* === 30s 间隔的预删除 (累加 tick 实现) === */
        cleanup_acc_ms += REC_TICK_MS;
        if (cleanup_acc_ms < REC_CLEANUP_CHECK_MS) {
            continue;
        }
        cleanup_acc_ms = 0;

        if (g_sd_formatting) {
            os_printf(KERN_INFO "rec_cleanup: skip (sd_format in progress)\n");
            continue;
        }
        if (g_rec_mode == REC_MODE_OFF) {
            continue;
        }
        if (rec_pb_is_active()) {
            /* 回放期间不删, 避免删到正在播的旧文件. 改为 INFO 级别避免刷屏 */
            os_printf(KERN_INFO "rec_cleanup: skip (playback active)\n");
            continue;
        }

        uint32_t tot = 0, fre = 0;
        if (sd_get_capacity(&tot, &fre) != 0 || tot == 0) {
            os_printf(KERN_WARNING "rec_cleanup: sd_get_capacity failed, skip\n");
            continue;
        }
        if (fre >= REC_CLEANUP_HIGH_MB) {
            /* 容量充足, 啥也不做. 不打印避免日志噪音 */
            continue;
        }

        /* 触发预删除. 再次检查 g_sd_formatting 缩小竞态窗口 */
        if (g_sd_formatting) continue;

        uint32_t t0_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies());
        int rc = rec_recycle_oldest();
        uint32_t dt_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies()) - t0_ms;

        uint32_t fre2 = 0;
        sd_get_capacity(&tot, &fre2);
        os_printf(KERN_INFO "rec_cleanup: free %u→%u MB, rc=%d, cost=%u ms\n",
                  (unsigned)fre, (unsigned)fre2, rc, (unsigned)dt_ms);

        if (rc != 0) {
            /* rec_recycle_oldest 返回 -1: 没文件可删. acc 拉到 -90000, 实际等
             * 90+30=120s 再触发下次预删除, 避免空转刷日志 */
            cleanup_acc_ms = -90000;
        }
    }
}

int rec_playback_init(void)
{
    if (g_rp_inited)
        return 0;

    g_rp_inited = 1;

    /* 初始化回收 mutex. 必须在启动 rec_bootstrap_thread (含预删除循环) 之前完成,
     * 也必须在 rec_create_file_cb 第一次可能调 rec_recycle_oldest 之前 */
    if (!g_recycle_lock_inited) {
        os_mutex_init(&g_recycle_lock);
        g_recycle_lock_inited = 1;
    }

    /* sd_pb_recv msi 在此创建一次, 永不销毁, 避免 pb_start/stop 反复创建
     * 同名 msi 时触发 SDK 的 "不要重复打开" 路径导致 double destroy */
    struct msi *recv = msi_new("sd_pb_recv", 16, NULL);
    if (recv) {
        recv->action = NULL;
        recv->enable = 1;
    }

    /* 启动 bootstrap 任务: 等 SD 卡就绪后再真正开始录像;
     * 启动完后进入常驻循环, 兼做: 预删除 / SD 不可恢复故障监控 / UNSYNC 录像迁移.
     * 栈用量按最深路径估 (这几条路径互斥, 取最大):
     *   - 预删除 rec_recycle_oldest + FATFS ~1.1KB
     *   - UNSYNC 迁移 rec_unsync_migrate (sscanf/snprintf/f_rename) ~0.95KB
     * 原 3072 算下来够, 但 newlib sscanf / FATFS f_rename 嵌套真实栈深不确定,
     * 提到 4096 留 ~2KB 余量, 栈溢出是致命的, 多花 1KB SRAM 值得.
     * 优先级 BELOW_NORMAL 故意低于 mp4_encode (ABOVE_NORMAL), unlink/migrate 时
     * 不抢写卡 CPU, mp4_encode 可继续消费 fbq */
    static struct os_task bootstrap_task;
    OS_TASK_INIT("rec_bootstrap", &bootstrap_task, rec_bootstrap_thread, NULL,
                 OS_TASK_PRIORITY_BELOW_NORMAL, NULL, 4096);

    os_printf(KERN_INFO "rec_playback_init: ok, mode=%d, sd_pb_recv=%p\n",
              g_rec_mode, recv);
    return 0;
}

/* 向后兼容 (外部若调用不会出错) */
int rec_index_rebuild(void)
{
    return 0;
}

/* =========================================================================
 * 录像启停
 * ========================================================================= */

/* 取文件实际大小 (KB), 用于切片日志. 不存在返回 0 */
static uint32_t rec_file_size_kb(const char *path)
{
    if (!path || !path[0]) return 0;
    void *fp = osal_fopen(path, "rb");
    if (!fp) return 0;
    uint32_t sz = osal_fsize(fp);
    osal_fclose(fp);
    return (sz + 1023) / 1024;
}

/* mp4 encode 模块的 create_file 回调: 生成新文件并返回 FILE * */
static void *rec_create_file_cb(struct file_process *fp, char *file_name, char *file_path, uint32_t file_size)
{
    /* 快照上一个 (即将被切走的) 文件路径, 后面统一打印实际大小, 便于在日志里
     * 直接看到每个 MP4/.alaw 切片最终多大. 第一次创建时 fpath 为空, 不打印 */
    char prev_mp4_path[96]  = {0};
    char prev_alaw_path[96] = {0};
    if (g_curfile.fpath[0]) {
        os_strncpy(prev_mp4_path,  g_curfile.fpath,      sizeof(prev_mp4_path) - 1);
        os_strncpy(prev_alaw_path, g_curfile.alaw_fpath, sizeof(prev_alaw_path) - 1);
    }

    /* === 清理上一次写卡失败遗留的空 mp4 文件 ===
     * mp4_encode_thread 写卡失败异常退出后会立刻进入下一轮循环, 通过 SDK 调到
     * 这里. 上一个 mp4 (g_curfile.fpath) 实际什么都没写或只写了 ftyp/moov 壳,
     * 是个 < 16KB 的垃圾文件 (正常 60s mp4 至少几百 KB). 主动 unlink 不留卡上.
     *
     * 同时计数连续失败次数, 超过 REC_CREATE_FAIL_LIMIT 就 return NULL.
     * SDK mp4_encode_thread (mp4_encode_msi2.c) 看到 fp=NULL 会走
     * MP4_ENCODE_ERR_NO_SD 路径, 自己 os_event_wait 1s 再 retry.
     *
     * 不在本函数自己 sleep: 实测 5s sleep 期间上游 auto_h264 持续编帧
     * (25fps × 5s × ~10KB = 1.2MB), mp4 fbq + h264 static_buf 满后走
     * fallback STREAM_MALLOC av_psram, 把仅有 ~72KB 余量的 av_psram 吃光.
     * SDK 自己的 1s 等待已经够防 CPU 空转.
     *
     * 16KB 阈值: 正常 60s mp4 >> 16KB, 不会误伤; ftyp+moov 空壳 <= 几 KB. */
    #define REC_PREV_GARBAGE_KB     16U
    #define REC_CREATE_FAIL_LIMIT   3U

    static uint32_t s_create_fail_streak = 0;

    if (prev_mp4_path[0]) {
        uint32_t prev_kb = rec_file_size_kb(prev_mp4_path);
        if (prev_kb < REC_PREV_GARBAGE_KB) {
            s_create_fail_streak++;
            os_printf(KERN_WARNING "rec_create_file: prev mp4 only %uKB (<%uKB), treat as failed write, "
                                   "unlink it (streak=%u): %s\n",
                      (unsigned)prev_kb, (unsigned)REC_PREV_GARBAGE_KB,
                      (unsigned)s_create_fail_streak, prev_mp4_path);
            osal_unlink(prev_mp4_path);
            if (prev_alaw_path[0]) {
                osal_unlink(prev_alaw_path);   /* 同名 .alaw 一起删, 忽略 err */
            }
            /* 已经清理, 不再打 "prev=0KB" 那行 */
            prev_mp4_path[0]  = '\0';
            prev_alaw_path[0] = '\0';

            if (s_create_fail_streak >= REC_CREATE_FAIL_LIMIT) {
                /* 连续 REC_CREATE_FAIL_LIMIT 次写卡都失败, 大概率是 SD 卡硬件/
                 * FATFS 持续异常. return NULL 让 SDK mp4_encode_thread 走
                 * MP4_ENCODE_ERR_NO_SD 路径, os_event_wait 1s 后自动 retry.
                 * 计数器在这里清零, 下次再触发时从头计数 */
                os_printf(KERN_ERR "rec_create_file: %u consecutive failed writes, return NULL "
                                   "(SDK will retry in 1s). Check SD card health "
                                   "(see osal_fwrite FRESULT logs)\n",
                          (unsigned)s_create_fail_streak);
                s_create_fail_streak = 0;
                /* 清空 g_curfile, 让下一次 fpath 为空, 进来时 prev_*_path 也为空 */
                os_memset(&g_curfile, 0, sizeof(g_curfile));
                rec_create_fail_inc();
                return NULL;
            }
        } else {
            /* prev 大小正常, 重置连续失败计数 */
            s_create_fail_streak = 0;
        }
    }

    /* 检查容量, 循环删旧 */
    uint32_t total_mb = 0, free_mb = 0;
    sd_get_capacity(&total_mb, &free_mb);
    if (total_mb == 0) {
        os_printf(KERN_ERR "rec_create_file: sd not ready\n");
        rec_create_fail_inc();
        return NULL;
    }
    int retry = 0;
    while (free_mb < REC_LOOP_REMAIN_MB && retry++ < 10) {
        if (rec_recycle_oldest() != 0)
            break;
        sd_get_capacity(&total_mb, &free_mb);
    }
    if (free_mb < 32) {
        os_printf(KERN_ERR "rec_create_file: sd full\n");
        rec_create_fail_inc();
        return NULL;
    }

    /* 根据当前模式确定 event 和时长 */
    uint8_t ev = ECEVENT_NONE;
    uint8_t dur = REC_FILE_SEC_ALL_DAY;
    if (g_rec_mode == REC_MODE_ALARM) {
        ev = g_curfile.event;
        dur = REC_FILE_SEC_ALARM;
    } else {
        ev = g_curfile.event;
    }

    char full_dir[48];

    if (g_rec_unsync_mode && !time_sync_flag) {
        /* === UNSYNC 模式: 时间没同步, 录到 0:/REC/UNSYNC/, 文件名用开机毫秒 ===
         * 文件名 <boot_ms>_Eee_dd.MP4, boot_ms 单调递增可排序; NTP 同步后由
         * bootstrap_thread migrate 到真实日期目录 */
        os_strncpy(full_dir, REC_UNSYNC_DIR, sizeof(full_dir) - 1);
        full_dir[sizeof(full_dir) - 1] = '\0';
        void *d = osal_opendir(full_dir);
        if (!d) {
            if (osal_fmkdir(full_dir) != FR_OK) {
                os_printf(KERN_ERR "rec_create_file: mkdir %s fail\n", full_dir);
                rec_create_fail_inc();
                return NULL;
            }
        } else {
            osal_closedir(d);
        }
        /* 用开机秒 boot_sec (不是毫秒): 文件名长度受 FILE_NAME_LEN=17 限制,
         * "<boot_sec>_Eee_dd.MP4" 中 boot_sec 7 位 (49 天内) → 16 字节, 刚好 <17.
         * 秒级精度对录像文件时刻足够 (正常文件名本就是 HHMMSS 秒级). */
        uint32_t boot_sec = (uint32_t)(os_jiffies_to_msecs(os_jiffies()) / 1000);
        os_snprintf(file_name, FILE_NAME_LEN + 1, "%u_E%02d_%02d%s", boot_sec, ev, dur, REC_EXT_NAME);
        os_snprintf(file_path, 96, "%s/%s", full_dir, file_name);
        /* g_curfile.t_start 在 UNSYNC 模式无真实意义, 存 boot_sec 占位 */
        g_curfile.t_start = boot_sec;
    } else {
        /* === 正常模式: 时间已同步, 真实日期目录 + HHMMSS 文件名 === */
        /* UNSYNC→真实时间的切换点: 此刻是 mp4_encode_thread 自然切文件, 上一个
         * UNSYNC 文件 (mp4 + .alaw) 已经 fclose, 是可安全 rename 的普通文件.
         * 在这里退出 UNSYNC 模式 + 通知 bootstrap 异步迁移. 绝不在此 stop/destroy
         * msi (会触发 mp4_encode 线程 use-after-free), 让本函数正常返回新文件句柄,
         * 编码线程无缝继续录到真实目录. */
        if (g_rec_unsync_mode) {
            g_rec_unsync_mode     = 0;
            g_unsync_need_migrate = 1;
            os_printf(KERN_INFO "rec_create_file: time synced, exit UNSYNC mode, "
                                "new files go to real dir, migrate pending\n");
        }
        time_t now = time(NULL);
        char   date_dir[16];
        char   hms[8];
        utc_to_dir((uint32_t) now, date_dir, sizeof(date_dir));
        utc_to_time((uint32_t) now, hms, sizeof(hms));

        os_snprintf(full_dir, sizeof(full_dir), "%s/%s", REC_ROOT_PATH, date_dir);
        void *d = osal_opendir(full_dir);
        if (!d) {
            if (osal_fmkdir(full_dir) != FR_OK) {
                os_printf(KERN_ERR "rec_create_file: mkdir %s fail\n", full_dir);
                rec_create_fail_inc();
                return NULL;
            }
        } else {
            osal_closedir(d);
        }
        os_snprintf(file_name, FILE_NAME_LEN + 1, "%s_E%02d_%02d%s", hms, ev, dur, REC_EXT_NAME);
        os_snprintf(file_path, 96, "%s/%s", full_dir, file_name);
        g_curfile.t_start = (uint32_t) now;
    }

    /* 记录当前文件信息 */
    os_strncpy(g_curfile.fname, file_name, sizeof(g_curfile.fname) - 1);
    g_curfile.fname[sizeof(g_curfile.fname) - 1] = '\0';
    os_strncpy(g_curfile.fpath, file_path, sizeof(g_curfile.fpath) - 1);
    g_curfile.fpath[sizeof(g_curfile.fpath) - 1] = '\0';
    g_curfile.duration_sec = dur;
    g_curfile.extended = 0;

    /* 关键: 先尝试 fopen MP4 (视频), 失败直接 return NULL 不再建 .alaw.
     * 之前的 bug: 先建 .alaw 再 fopen MP4, MP4 失败时 .alaw 已存在且会被
     * rec_alaw_action 持续写入, 留下"只有 .alaw 没 MP4"的孤儿文件. */
    void *mp4_fp = osal_fopen(file_path, "a+");
    if (!mp4_fp) {
        os_printf(KERN_ERR "rec_create_file: mp4 fopen %s fail (SD likely full or fs error)\n",
                  file_path);
        /* 同步把上一个文件遗留的 .alaw 句柄关掉, 避免数据继续写到旧 .alaw 上.
         * 不创建新 .alaw, 让 rec_alaw_action 看到 g_rec_alaw_fp=NULL 自动跳过音频写入. */
        if (g_rec_alaw_lock_inited) {
            os_mutex_lock(&g_rec_alaw_lock, osWaitForever);
            if (g_rec_alaw_fp) {
                osal_fclose(g_rec_alaw_fp);
                g_rec_alaw_fp = NULL;
            }
            os_mutex_unlock(&g_rec_alaw_lock);
        }
        /* 清空 g_curfile, 表示当前没有有效的录像文件 */
        os_memset(&g_curfile, 0, sizeof(g_curfile));
        rec_create_fail_inc();
        return NULL;
    }
    /* 成功 fopen mp4: 卡当前可用, 清不可恢复故障计数 */
    rec_create_fail_reset();

    /* MP4 创建成功, 再同步创建 .alaw 文件: HHMMSS_Eee_dd.alaw 和 .MP4 同目录同前缀.
     * mp4 切片时 SDK 会先 fclose 旧 mp4 再 fopen 新 mp4, 再次调用 rec_create_file_cb,
     * 这里同步切 .alaw 文件: 关闭旧的 → 打开新的, mutex 保护 alaw_action 写入不踩车. */
    os_snprintf(g_curfile.alaw_fpath, sizeof(g_curfile.alaw_fpath),
                "%s/%.*s%s", full_dir,
                (int)(os_strlen(file_name) - os_strlen(REC_EXT_NAME)), file_name,
                REC_AUDIO_EXT_NAME);

    if (g_rec_alaw_lock_inited) {
        os_mutex_lock(&g_rec_alaw_lock, osWaitForever);
        if (g_rec_alaw_fp) {
            osal_fclose(g_rec_alaw_fp);
            g_rec_alaw_fp = NULL;
        }
        g_rec_alaw_fp = osal_fopen(g_curfile.alaw_fpath, "a+");
        if (!g_rec_alaw_fp) {
            /* .alaw 打开失败只警告, 不影响 MP4 录像继续 (这条录像就只有视频没音频).
             * rec_alaw_action 内部会检查 g_rec_alaw_fp 为 NULL 时跳过写入 */
            os_printf(KERN_WARNING "rec_create_file: alaw fopen %s fail, audio disabled for this slice\n",
                      g_curfile.alaw_fpath);
        }
        os_mutex_unlock(&g_rec_alaw_lock);
    }

    /* 打印新文件 + 上一个文件实际大小 (KB). 切片日志里能直接看到每段录了多大 */
    if (prev_mp4_path[0]) {
        os_printf(KERN_INFO "rec_create_file: %s (+%s)  prev: mp4=%uKB alaw=%uKB\n",
                  file_path, g_curfile.alaw_fpath,
                  rec_file_size_kb(prev_mp4_path),
                  rec_file_size_kb(prev_alaw_path));
    } else {
        os_printf(KERN_INFO "rec_create_file: %s (+%s)\n", file_path, g_curfile.alaw_fpath);
    }
    return mp4_fp;
}

static void rec_loop_free_cb(void **loop)
{
    (void) loop;
}

/* ==== 音频录制: PCM fb → G.711A → 写 .alaw 文件 ====
 * 上游 S_AUADC (audio_adc.c) 每 40ms 吐一帧 PCM fb: 8kHz mono int16, 320 samples × 2B = 640B.
 * 转 G.711A 后每包 320B / 40ms, 直接追加到 g_rec_alaw_fp.
 * 用 msi_action 而非独立线程, 最省资源 */
static int32_t rec_alaw_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    switch (cmd_id) {
    case MSI_CMD_TRANS_FB: {
        struct framebuff *fb = (struct framebuff *) param1;
        if (fb->mtype != F_AUDIO || fb->stype != FSTYPE_AUDIO_ADC) {
            return RET_ERR;
        }
        if (!g_rec_alaw_fp) return RET_ERR;

        /* PCM int16 → G.711A (linear2alaw 每次 1 样本 -> 1 字节) */
        uint32_t samples = fb->len / 2;
        if (samples == 0 || samples > 1024) return RET_ERR;
        /* 栈上 buffer: 40ms @ 8kHz = 320B, 留点余量 */
        static uint8_t alaw_buf[512];
        if (samples > sizeof(alaw_buf)) samples = sizeof(alaw_buf);
        int16_t *pcm = (int16_t *) fb->data;
        for (uint32_t i = 0; i < samples; i++) {
            alaw_buf[i] = linear2alaw(pcm[i]);
        }

        /* 写文件 (mutex 防止 rec_create_file_cb 切 fp 踩车) */
        os_mutex_lock(&g_rec_alaw_lock, osWaitForever);
        if (g_rec_alaw_fp) {
            osal_fwrite(alaw_buf, 1, samples, g_rec_alaw_fp);
            /* 每秒 fsync 一次, 跟 mp4_syn 节奏对齐. 避免突然断电时 .alaw 文件
             * 元数据 (FAT 表 size/cluster chain) 没刷盘 → 文件大小变 0 → 回放
             * 无声音. fsync 单次 ~10-30ms, 25 帧/秒里每 25 帧才同步一次, 平均
             * 开销 1ms/帧, 可接受 */
            static uint32_t s_alaw_sync_last_ms = 0;
            uint32_t now_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies());
            if ((uint32_t)(now_ms - s_alaw_sync_last_ms) >= 1000) {
                s_alaw_sync_last_ms = now_ms;
                osal_fsync(g_rec_alaw_fp);
//                os_sleep_ms(1);   /* fsync 后让出 CPU 给 WiFi 收 TCP 包 */
            }
        }
        os_mutex_unlock(&g_rec_alaw_lock);
        return RET_ERR;   /* 不占 fbq, 写完立即告诉上游失败丢弃 */
    }
    default:
        break;
    }
    return RET_OK;
}

/* ==== 调试模式: DRY_RUN 模式下不写卡, 接收 fb 立刻返回 RET_ERR 丢弃 ====
 * 1 = 开启 DRY RUN (不写卡, 用于排查非写卡路径的问题)
 * 0 = 正常写卡 (默认) */
#define REC_DRY_RUN_NO_WRITE   0

#if REC_DRY_RUN_NO_WRITE

/* 计数统计 */
static volatile uint32_t g_dry_run_video_cnt = 0;
static volatile uint32_t g_dry_run_audio_cnt = 0;

/* sd_rec_mp4 msi 的 action: 仅接收 H264 + AAC 帧, 不拷贝不写卡, 直接返回 RET_ERR
 * 上游 AUTO_H264 看到 RET_ERR 不会 fb_get, 也就不占 h264_static_buf */
static int32_t rec_dryrun_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    switch (cmd_id) {
        case MSI_CMD_TRANS_FB: {
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb->mtype == F_H264) {
                if (fb->stype == REC_STREAM_STYPE) {
                    /* 只统计我们要的码流 */
                    g_dry_run_video_cnt++;
                    if ((g_dry_run_video_cnt % 200) == 0)
                        os_printf(KERN_INFO "rec_dryrun: v=%d a=%d\n",
                                  g_dry_run_video_cnt, g_dry_run_audio_cnt);
                }
            } else if (fb->mtype == F_AUDIO) {
                g_dry_run_audio_cnt++;
            }
            return RET_ERR;   /* 关键: 返回 ERR 让上游不 fb_get, 立即丢弃 */
        }
        default:
            break;
    }
    return RET_OK;
}
#endif

int _rp_record_start(uint8_t event_type)
{
    if (g_rec_msi) {
        os_printf(KERN_INFO "record already started\n");
        return 0;
    }

    g_rec_h264 = msi_find(AUTO_H264, 1);
    if (!g_rec_h264) {
        os_printf(KERN_ERR "record_start: AUTO_H264 not found\n");
        return -1;
    }

#if REC_DRY_RUN_NO_WRITE
    /* ==== DRY RUN 模式: 不初始化 mp4_encode, 仅用一个丢弃 msi ==== */
    g_rec_msi = msi_new("sd_rec_mp4", 2, NULL);
    if (!g_rec_msi) {
        os_printf(KERN_ERR "record_start: dry-run msi create failed\n");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }
    g_rec_msi->action = rec_dryrun_action;
    g_rec_msi->enable = 1;

    msi_add_output(g_rec_h264, NULL, "sd_rec_mp4");

    g_dry_run_video_cnt = 0;
    g_dry_run_audio_cnt = 0;

    os_printf(KERN_WARNING "rec: DRY RUN mode (no SD write), event=%d\n", event_type);
    return 0;

#else
    /* 初始化 alaw 文件互斥锁 (一次性) */
    if (!g_rec_alaw_lock_inited) {
        os_mutex_init(&g_rec_alaw_lock);
        g_rec_alaw_lock_inited = 1;
    }

    /* 初始化 g_curfile (先设置 event, create_file 时会用) */
    g_curfile.event = event_type;
    g_curfile.extended = 0;

    /* MP4 encode: 无音频轨 (audio_encode=0), 仅存 H264 视频 */
    static struct file_process fp_cfg;
    os_memset(&fp_cfg, 0, sizeof(fp_cfg));
    fp_cfg.loop        = NULL;
    fp_cfg.rec_path    = (char *) REC_ROOT_PATH;
    fp_cfg.ext_name    = (char *) REC_EXT_NAME;
    fp_cfg.create_file = rec_create_file_cb;
    fp_cfg.loop_free   = rec_loop_free_cb;
    fp_cfg.lock_file   = NULL;

    /* 录像 filter_type = REC_STREAM_STYPE, 由 project_config.h 的 REC_STREAM_TYPE 选:
     *   REC_STREAM_TYPE=0 → 主码流 (826: VPP_DATA0/720P;  828: VPP_DATA0/1080P)
     *   REC_STREAM_TYPE=1 → 子码流 (826: GEN420_DATA;     828: VPP_DATA1/640x360)
     * rec_time 单位 分钟 */
    uint8_t rec_min = 1;
    g_rec_msi = mp4_encode_msi2_init("sd_rec_mp4",
                                     FRAMEBUFF_SOURCE_CAMERA0,
                                     REC_STREAM_STYPE,
                                     rec_min,
                                     0,              /* audio_encode=0, 不录音频到 MP4 */
                                     &fp_cfg,
                                     0);
    if (!g_rec_msi) {
        os_printf(KERN_ERR "record_start: mp4_encode init failed\n");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }

    msi_add_output(g_rec_h264, NULL, "sd_rec_mp4");

    /* 触发 mp4_encode_thread 启动 (先挂 msi 再发命令, rec_create_file_cb 里会创建 .alaw 文件) */
    msi_do_cmd(g_rec_msi, MSI_CMD_MEDIA_CTRL, MSI_MEDIA_CTRL_RECORD_START, 0);

    /* 独立音频录制 msi: 订阅 S_AUADC 的 PCM fb, 在 action 里转 G.711A 写 .alaw */
    g_rec_alaw = msi_new("sd_rec_alaw", 2, NULL);
    if (g_rec_alaw) {
        g_rec_alaw->action = rec_alaw_action;
        g_rec_alaw->enable = 1;
        auadc_msi_add_output(AUSYS_AUAD, "sd_rec_alaw");
    } else {
        os_printf(KERN_ERR "record_start: sd_rec_alaw create fail\n");
    }

    os_printf(KERN_INFO "record started (video-only MP4 + alaw), event=%d, min=%d\n",
              event_type, rec_min);
    return 0;
#endif
}

int _rp_record_stop(void)
{
    if (!g_rec_msi)
        return 0;

    if (g_rec_h264)
        msi_del_output(g_rec_h264, NULL, "sd_rec_mp4");

    /* 停止 alaw msi, 断开 S_AUADC → sd_rec_alaw 的投递 */
    if (g_rec_alaw) {
        /* 反向断开 S_AUADC 到 sd_rec_alaw 的绑定 */
        struct msi *adc = get_auadc_msi(AUSYS_AUAD);
        if (adc) {
            msi_del_output(adc, NULL, "sd_rec_alaw");
        }
        msi_destroy(g_rec_alaw);
        g_rec_alaw = NULL;
    }

    /* 关闭 alaw 文件 */
    if (g_rec_alaw_lock_inited) {
        os_mutex_lock(&g_rec_alaw_lock, osWaitForever);
        if (g_rec_alaw_fp) {
            osal_fclose(g_rec_alaw_fp);
            g_rec_alaw_fp = NULL;
        }
        os_mutex_unlock(&g_rec_alaw_lock);
    }

    msi_destroy(g_rec_msi);
    g_rec_msi = NULL;
    if (g_rec_h264) {
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
    }

    os_memset(&g_curfile, 0, sizeof(g_curfile));
    os_printf(KERN_INFO "record stopped\n");
    return 0;
}

int rec_trigger_alarm(uint8_t event_type)
{
    if (!g_rp_inited)
        return -1;

    if (g_rec_mode == REC_MODE_OFF) {
        os_printf(KERN_INFO "rec_trigger_alarm: mode=OFF, ignored\n");
        return 0;
    }

    if (g_rec_mode == REC_MODE_ALARM) {
        if (!g_rec_msi) {
            return _rp_record_start(event_type);
        }
        uint32_t now = (uint32_t) time(NULL);
        uint32_t elapsed = now - g_curfile.t_start;
        if (!g_curfile.extended && elapsed < REC_FILE_SEC_ALARM) {
            g_curfile.extended = 1;
            g_curfile.duration_sec = REC_FILE_SEC_ALARM_EXT;
            if (g_curfile.event == ECEVENT_NONE)
                g_curfile.event = event_type;
            os_printf(KERN_INFO "rec_trigger_alarm: extend to %ds, event=%d\n",
                      REC_FILE_SEC_ALARM_EXT, g_curfile.event);
        }
        return 0;
    }

    /* REC_MODE_ALL_DAY: 标记当前文件 event 类型, 下次切片生效 */
    if (g_curfile.event == ECEVENT_NONE) {
        g_curfile.event = event_type;
        os_printf(KERN_INFO "rec_trigger_alarm: all_day mode, mark event=%d\n", event_type);
    }
    return 0;
}

/* =========================================================================
 * 录像文件列表 (按需扫描 + 合并相邻同类事件)
 * ========================================================================= */

/* 内部: 遍历当日 day_arr, 按时间范围 + 相邻合并策略生成输出条目.
 * @param out_arr NULL 时仅统计数量 (pass1), 非 NULL 时填充 (pass2)
 * @return 产生的条目数 (pass1 模式下也有效), out_arr 满时停止 */
static int rec_list_fold_day(const scan_item_t *day_arr, uint32_t day_cnt,
                             uint32_t t_start, uint32_t t_end,
                             SAvExEvent *out_arr, int out_cap)
{
    int out_idx = -1;
    for (uint32_t i = 0; i < day_cnt; i++) {
        uint32_t ft0 = day_arr[i].t_start;
        uint32_t ft1 = ft0 + day_arr[i].duration;
        if (ft1 < t_start) continue;
        if (ft0 >= t_end) break;

        /* 尝试与上一条合并 (事件相同 && 时间连续允许 2s 误差) */
        if (out_idx >= 0) {
            uint8_t  prev_event;
            uint32_t prev_t0, prev_end;
            if (out_arr) {
                prev_t0    = (uint32_t) TcuTimeDay2T(&out_arr[out_idx].start_time);
                prev_end   = prev_t0 + out_arr[out_idx].file_len;
                prev_event = out_arr[out_idx].event;
            } else {
                /* pass1 仅统计, 从 day_arr 往回找最近一个 "被留下" 的条目.
                 * 这里简化: 假设合并只会发生在相邻 day_arr 元素 (实测录像命名连续成立) */
                prev_t0    = day_arr[i - 1].t_start;       /* i>=1 因为 out_idx>=0 */
                prev_end   = prev_t0 + day_arr[i - 1].duration;
                prev_event = day_arr[i - 1].event;
            }
            if (prev_event == day_arr[i].event &&
                ft0 >= prev_end - 2 && ft0 <= prev_end + 2) {
                if (out_arr) out_arr[out_idx].file_len = ft1 - prev_t0;
                continue;
            }
        }
        if (out_idx + 1 >= out_cap) break;
        out_idx++;
        if (out_arr) {
            TcuT2TimeDay((time_t) ft0, &out_arr[out_idx].start_time);
            out_arr[out_idx].file_len = day_arr[i].duration;
            out_arr[out_idx].event    = day_arr[i].event;
            out_arr[out_idx].flags    = 0;
        }
    }
    return out_idx + 1;
}

/* 单次查询只返回 t_start 所在那一天的条目 (本地日).
 * 两阶段: 先扫描+统计实际条目数, 再按精确数量 malloc, 再填充.
 *
 * !!! 目录名是"本地日期"(rec_create_file_cb 里 utc_to_dir(now) 得到, 加了 _tg_timezone_),
 *     所以这里必须用 utc_to_dir(t_start) 推目录, 不能按 UTC 86400 对齐.
 *     之前按 UTC 对齐会扫到前一天 (北京 5-6 00:00 对应真实 UTC 5-5 16:00,
 *     86400 对齐后 day0 是 5-5 00:00 UTC, 目录是 "20260505"), 导致 0 records. */
int rec_list_get(uint32_t t_start, uint32_t t_end, SAvExEvent **out_items)
{
    if (!out_items) return -1;
    *out_items = NULL;

    /* 目录按 t_start 对应的本地日期算; 过滤窗口直接用 APP 传的 t_start/t_end.
     * 只扫这一天的目录, 跨天查询请上层分天调用 */
    char date_dir[16];
    utc_to_dir(t_start, date_dir, sizeof(date_dir));
    uint32_t q_start = t_start;
    uint32_t q_end   = t_end;

    scan_item_t *day_arr = NULL;
    uint32_t     day_cnt = 0;
    if (scan_day_dir(date_dir, &day_arr, &day_cnt) != 0 || day_cnt == 0) {
        os_printf(KERN_INFO "rec_list_get: scan(%s) empty (t_start=%u)\n",
                  date_dir, t_start);
        if (day_arr) RP_FREE(day_arr);
        return 0;
    }

    /* Pass 1: 只统计条目数, 不分配输出数组 */
    const int scan_cap = 1440;  /* 单日最多文件数 (每分钟一个), 硬上限 */
    int need = rec_list_fold_day(day_arr, day_cnt, q_start, q_end, NULL, scan_cap);
    if (need <= 0) {
        RP_FREE(day_arr);
        return 0;
    }

    /* 按实际需求精确分配 */
    SAvExEvent *arr = (SAvExEvent *) RP_MALLOC(sizeof(SAvExEvent) * need);
    if (!arr) {
        os_printf(KERN_ERR "rec_list_get: alloc %d items (%d bytes) fail\n",
                  need, (int)(sizeof(SAvExEvent) * need));
        RP_FREE(day_arr);
        return -1;
    }
    os_memset(arr, 0, sizeof(SAvExEvent) * need);

    /* Pass 2: 实际填充 */
    int final_cnt = rec_list_fold_day(day_arr, day_cnt, q_start, q_end, arr, need);
    RP_FREE(day_arr);

    if (final_cnt <= 0) {
        RP_FREE(arr);
        return 0;
    }
    *out_items = arr;
    os_printf(KERN_INFO "rec_list_get: %d items (day %s, range %u-%u)\n",
              final_cnt, date_dir, q_start, q_end);
    return final_cnt;
}

/* 检查某个日期子目录下是否至少有一个 "已完成" 的 MP4 文件.
 * 找到第一个就立刻返回, 不做完整扫描, 节省 IO.
 * 跳过 g_curfile 标记的正在录文件 (未完成, 不可回放) */
static int day_dir_has_mp4(const char *date_dir)
{
    char sub_path[64];
    os_snprintf(sub_path, sizeof(sub_path), "%s/%s", REC_ROOT_PATH, date_dir);
    void *d = osal_opendir(sub_path);
    if (!d) return 0;

    /* 快照正在录的文件名/路径, 和 scan_day_dir 逻辑一致 */
    char cur_fname_snap[FILE_NAME_LEN + 1] = {0};
    char cur_fpath_snap[96] = {0};
    if (g_rec_msi) {
        os_strncpy(cur_fname_snap, g_curfile.fname, sizeof(cur_fname_snap) - 1);
        os_strncpy(cur_fpath_snap, g_curfile.fpath, sizeof(cur_fpath_snap) - 1);
    }

    int found = 0;
    void *fno;
    while ((fno = osal_readdir(d)) != NULL) {
        char *fn = osal_dirent_name(fno);
        if (!fn || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fn);
        if (nlen < 5) continue;
        if (os_strcasecmp(fn + nlen - 4, REC_EXT_NAME) != 0) continue;
        /* 跳过正在录的文件 */
        if (cur_fname_snap[0] &&
            os_strcmp(fn, cur_fname_snap) == 0 &&
            os_strstr(cur_fpath_snap, date_dir) != NULL) {
            continue;
        }
        found = 1;
        break;
    }
    osal_closedir(d);
    return found;
}

/* 扫描 REC_ROOT_PATH 下的 YYYYMMDD 子目录, 返回有录像的日期列表.
 * 只统计"目录内至少有 1 个 MP4 文件"的合法日期目录 */
int rec_list_days_get(SDay **out_days)
{
    if (!out_days) return -1;
    *out_days = NULL;

    /* 第一遍: 数数有多少个合法的 YYYYMMDD 目录且内含 MP4 */
    void *dir = osal_opendir(REC_ROOT_PATH);
    if (!dir) return 0;   /* REC 目录还没建, 没录像 */

    uint32_t cnt = 0;
    void *fno;
    while ((fno = osal_readdir(dir)) != NULL) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) continue;
        if (!day_dir_has_mp4(name)) continue;   /* 空目录忽略 */
        cnt++;
    }
    osal_closedir(dir);
    if (cnt == 0) return 0;

    /* 第二遍: 分配数组并填充 */
    SDay *arr = (SDay *) RP_MALLOC(sizeof(SDay) * cnt);
    if (!arr) return -1;
    os_memset(arr, 0, sizeof(SDay) * cnt);

    dir = osal_opendir(REC_ROOT_PATH);
    if (!dir) { RP_FREE(arr); return -1; }

    uint32_t idx = 0;
    while ((fno = osal_readdir(dir)) != NULL && idx < cnt) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) continue;
        if (!day_dir_has_mp4(name)) continue;   /* 和第一遍保持一致 */
        arr[idx].year  = y;
        arr[idx].month = mon;
        arr[idx].day   = day;
        idx++;
    }
    osal_closedir(dir);

    /* 按日期升序排序 */
    if (idx > 1) {
        for (uint32_t i = 0; i + 1 < idx; i++) {
            for (uint32_t j = 0; j + 1 < idx - i; j++) {
                uint32_t a = (uint32_t) arr[j].year * 10000
                           + (uint32_t) arr[j].month * 100
                           + (uint32_t) arr[j].day;
                uint32_t b = (uint32_t) arr[j+1].year * 10000
                           + (uint32_t) arr[j+1].month * 100
                           + (uint32_t) arr[j+1].day;
                if (a > b) {
                    SDay t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t;
                }
            }
        }
    }

    *out_days = arr;
    os_printf(KERN_INFO "rec_list_days_get: %u days\n", idx);
    return (int) idx;
}

/* =========================================================================
 * P2P 回放
 * ========================================================================= */

typedef enum {
    PB_ST_IDLE = 0,
    PB_ST_PLAYING,
    PB_ST_PAUSED,
    PB_ST_STOP_REQ,
} pb_state_t;

typedef enum {
    PB_SPEED_1X = 0,
    PB_SPEED_2X,
    PB_SPEED_4X,
    PB_SPEED_IKEY,
} pb_speed_t;

static struct {
    p2phandle_t           handle;
    volatile pb_state_t   state;
    volatile pb_speed_t   speed;
    volatile uint8_t      mode_bit;     /* 0: 连续; 1: 事件 */
    volatile uint8_t      thread_alive;
    volatile uint8_t      seek_req;
    volatile uint32_t     seek_t;
    struct msi           *demux_msi;
    struct os_mutex       lock;
    /* 当前播放的文件信息, 用于"当前日期下找下一个" */
    char                  cur_date_dir[16];
    char                  cur_fname[FILE_NAME_LEN + 1];
} g_pb;

/* 给 rec_bootstrap_thread 的预删除循环用 (它在 g_pb 之前定义, 无法直接读). */
static int rec_pb_is_active(void)
{
    return g_pb.thread_alive ? 1 : 0;
}

/* 按目标 UTC 时间, 查找要播放的文件.
 * 优先级:
 *   1) 覆盖 t_seek 的文件 (t_start <= t_seek < t_start+duration)
 *   2) 当天后续最近的文件 (t_start > t_seek 里最小的那个)
 * 都没有(t_seek 超过当天所有文件的结束时间) 才返回 -1, 不跨天查找.
 * @param out_t0   返回值是实际起播文件的 t_start, 不一定等于 t_seek */
static int pb_locate_file(uint32_t t_seek, char *out_date_dir, char *out_fname,
                          uint32_t *out_t0)
{
    char date_dir[16];
    utc_to_dir(t_seek, date_dir, sizeof(date_dir));
    scan_item_t *arr = NULL; uint32_t cnt = 0;
    int sret = scan_day_dir(date_dir, &arr, &cnt);
    if (sret != 0 || cnt == 0) {
        os_printf(KERN_WARNING "pb_locate: scan(%s) ret=%d cnt=%u t_seek=%u\n",
                  date_dir, sret, cnt, t_seek);
        if (arr) RP_FREE(arr);
        return -1;
    }

    int found = -1;
    /* 1) 先找覆盖区间 */
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t t0 = arr[i].t_start;
        uint32_t t1 = t0 + arr[i].duration;
        if (t_seek >= t0 && t_seek < t1) {
            found = (int) i;
            break;
        }
    }
    /* 2) 未覆盖, 找第一个 t_start >= t_seek 的文件 (fallback: 当天后续最近) */
    if (found < 0) {
        for (uint32_t i = 0; i < cnt; i++) {
            if (arr[i].t_start >= t_seek) {
                found = (int) i;
                os_printf(KERN_INFO "pb_locate: fallback to next file "
                          "in %s, t_seek=%u -> file_t0=%u (+%us)\n",
                          date_dir, t_seek, arr[i].t_start,
                          arr[i].t_start - t_seek);
                break;
            }
        }
    }
    if (found < 0) {
        os_printf(KERN_WARNING "pb_locate: no file after t_seek=%u in %s "
                  "(cnt=%u, last_t0=%u, last_end=%u)\n",
                  t_seek, date_dir, cnt,
                  arr[cnt-1].t_start,
                  arr[cnt-1].t_start + arr[cnt-1].duration);
        RP_FREE(arr);
        return -1;
    }
    os_strncpy(out_date_dir, date_dir, 16);
    build_fname_from_item(&arr[found], out_fname, FILE_NAME_LEN + 1);
    *out_t0 = arr[found].t_start;
    RP_FREE(arr);
    return 0;
}

/* 新方案: .alaw 独立文件读取回放
 *
 * 录像时 mp4 只存视频, 音频独立存成 .alaw 文件 (linear2alaw 转码). 回放时:
 *   1) pb_thread 拿 mp4 视频 fb, 同时打开同前缀的 .alaw 文件
 *   2) 按视频 ts 节奏读 320B/40ms alaw 发给 APP, 音频 ts 固定 40ms 递增
 *   3) 跨文件时关旧 .alaw 开新 .alaw, 和 mp4_demux 切换同步
 *
 * 好处: 完全绕开 AAC 解码器, G.711A 直接透传给 APP (APP 原生支持). */
#define PB_PACK_BYTES        320     /* 单包 G.711A 字节数 (40ms @ 8kHz, 对齐直播) */
#define PB_PACK_INTERVAL_MS  40      /* 每包音频间隔 40ms */
static void    *g_pb_alaw_fp   = NULL;   /* 当前 .alaw 读取文件句柄 */
static uint8_t  g_pb_alaw_buf[PB_PACK_BYTES];    /* 一包 alaw 读缓冲 */
static uint32_t g_pb_audio_ts  = 0;      /* 下次要发的音频包 ts (从 0 开始 40ms 递增) */

/* 反压机制: TciSendPbFrame 返回 TCE_NETWORK_BUSY (-10004001) 时, 说明探鸽 SDK
 * 内部帧队列已满, 继续塞会导致 PSRAM 被累积的帧 chunk 蚕食, 最终 malloc fail 崩溃.
 *
 * 处理策略: 不丢帧, 原地 sleep PB_BUSY_SLEEP_MS 后重发同一帧, 直到发送成功或到达
 * PB_BUSY_MAX_RETRY 次上限 (避免连接已断时死循环). 重发期间 sd_pb_recv 里的 fb
 * 会短暂堆积, 但 msi 背压会让 mp4_demux 自然停下, 不会无限增长. */
#define TCE_NETWORK_BUSY_VAL  (-10004001)
#define PB_BUSY_SLEEP_MS      200       /* 每次 BUSY 后的休眠间隔 (探鸽文档建议 300ms) */
#define PB_BUSY_MAX_RETRY     20        /* 最多重试次数, 20*200ms=4s 兜底退出 */

/* 预检 MP4 文件是否合法可播放.
 *
 * !!! 为什么要自己预检, 不直接交给 mp4_demux_msi_init 判定:
 *   SDK 的 mp4_demux_msi.c 在 SPS/PPS 解析失败时走 mp4_demux_msi_init_err,
 *   里面 msi_destroy() 会触发 action(POST_DESTROY), 而 POST_DESTROY 用
 *   `os_event_wait(MP4_DEMUX_EXIT, -1)` 等一个永远不会被 set 的事件
 *   (mp4_demux_thread 根本没启动就进 err 分支了) → 调用线程永久挂起.
 *   所以我们必须在调用 mp4_demux_msi_init 之前自己先判断.
 *
 * SDK 的 MP4_open_init 用 fast-start 布局: 文件头是 ftyp + moov + mdat,
 * moov 在文件开头. 所以扫开头 8KB 找 "moov" 4 字节签名即可.
 *
 * 额外: 正在录制中的文件, moov box 虽已写 header 但内容是预留空位, SPS/PPS
 * 还没填入. 这种文件"可以扫到 moov 签名"但 mp4_demux_msi_init 解析时仍会
 * SPS=0/PPS=0 导致 SDK 死锁. 所以当前正在录的文件必须在上层额外跳过
 * (见下方 pb_is_current_recording_file).
 *
 * 返回: 1=合法可播, 0=坏文件 / 不可读 */
#define PB_MP4_SCAN_HEAD_SZ  (8 * 1024)   /* 扫头 8KB, ftyp+moov 合计通常 < 6KB */
static int pb_mp4_is_playable(const char *mp4_full_path)
{
    void *fp = osal_fopen(mp4_full_path, "rb");
    if (!fp) return 0;
    uint32_t fsize = osal_fsize(fp);
    if (fsize < 64) { osal_fclose(fp); return 0; }   /* 太小, 不可能是合法 mp4 */

    uint32_t scan_sz = (fsize < PB_MP4_SCAN_HEAD_SZ) ? fsize : PB_MP4_SCAN_HEAD_SZ;
    /* 扫描 buffer 按芯片分池 (同 RP_MALLOC 规则):
     *   826: SRAM (PSRAM 仅 4MB 极度紧张)
     *   828: PSRAM (SRAM 紧张) */
    uint8_t *buf = (uint8_t *) RP_MALLOC(scan_sz);
    if (!buf) { osal_fclose(fp); return 0; }
    int got = osal_fread(buf, 1, scan_sz, fp);
    osal_fclose(fp);

    int ok = 0;
    if (got >= 4) {
        for (int i = 0; i <= got - 4; i++) {
            if (buf[i] == 'm' && buf[i+1] == 'o' && buf[i+2] == 'o' && buf[i+3] == 'v') {
                ok = 1;
                break;
            }
        }
    }
    RP_FREE(buf);
    return ok;
}

/* 判断路径是否是当前正在录制中的文件. 正在录的文件 moov 内容尚未写入,
 * 既不能播 (SDK 解析失败挂起), 也不能删 (录像仍在写). 遇到直接跳过,
 * 等下次 APP 回放时它已经录完关闭就能正常播放. */
static int pb_is_current_recording_file(const char *mp4_full_path)
{
    if (!mp4_full_path || !g_curfile.fpath[0]) return 0;
    return os_strcmp(mp4_full_path, g_curfile.fpath) == 0 ? 1 : 0;
}

/* 删除坏录像文件: mp4_demux_msi_init 失败最常见原因是断电残留, 文件内
 * 没有 moov box / SPS PPS 解析不出. 直接把该 .MP4 和同名 .alaw 一起删掉,
 * 下次扫描录像列表就不会再看到它, 也不会再触发 malloc sps or pps err.
 *
 * 二次保险: 正在录制的文件绝不删 (pb_is_current_recording_file). */
static void pb_delete_bad_file(const char *mp4_full_path)
{
    if (!mp4_full_path || mp4_full_path[0] == 0) return;
    if (pb_is_current_recording_file(mp4_full_path)) {
        os_printf(KERN_WARNING "pb: %s is current recording, skip delete\n",
                  mp4_full_path);
        return;
    }
    FRESULT res = osal_unlink(mp4_full_path);
    os_printf(KERN_WARNING "pb: delete bad mp4 %s res=%d\n", mp4_full_path, res);
    int plen = os_strlen(mp4_full_path);
    int elen = os_strlen(REC_EXT_NAME);
    if (plen > elen) {
        char alaw_path[96];
        os_snprintf(alaw_path, sizeof(alaw_path), "%.*s%s",
                    plen - elen, mp4_full_path, REC_AUDIO_EXT_NAME);
        osal_unlink(alaw_path);   /* 忽略错误: 可能没有 .alaw */
    }
}

/* 回放发送任务 */
static void pb_thread(void *arg)
{
    g_pb.thread_alive = 1;
    char full_path[96];
    char demux_name[24];     /* demux msi 名字, 每次切文件加序号避免同名冲突 */
    static uint32_t demux_seq = 0;

    /* 播放结束判定:
     * mp4_demux_msi 不支持 MSI_CMD_GET_RUNNING 命令, 所以不能用它判断文件播完.
     * 改用: 已消费到至少一帧 + 连续 N 次 (~1s) 取不到 fb, 认为当前文件播完 */
    uint8_t  frames_consumed = 0;
    uint32_t empty_fb_count  = 0;
    const uint32_t EMPTY_FB_THRESHOLD = 200;  /* 200 * 5ms = 1s */

    /* 音频状态: 无累积 buffer, 每次 40ms 直接读 320B alaw → 发送 */
    g_pb_audio_ts = 0;
    /* g_pb_alaw_fp 在 seek/切文件分支里打开 */

    /* 直到收到第一帧 I 帧才真正开始发送: 探鸽 SDK 对回放首帧是 I 帧才放行.
     * sync_sent 在每次 seek/切文件后清零, 碰到第一帧 I 帧时:
     *   1) 先发同步帧 (utc_time = 该 I 帧实际对应的 UTC 秒)
     *   2) 再发 [SC SPS][SC PPS][SC IDR] 的完整 I 帧
     * pending_sync_t0 是当前文件的 UTC 起点(文件名时间), 用于算 I 帧的 utc */
    uint8_t  sync_sent       = 0;
    uint32_t pending_sync_t0 = 0;    /* 当前文件 t_start (文件名时间, UTC 秒) */
    uint32_t ts_base_ms      = 0;    /* 时间戳基准: 视频首 I 帧的 fb->time, 后续所有 ts 减去它归零
                                      * 目的: 保证音视频 ts 在同一基准, 且都从 0 开始 (同步帧 utc 对齐) */
    /* PTS pacing: 让回放速率严格按文件原始帧率发, 避免比实时快 N 倍把探鸽 P2P
     * 缓冲撑爆 (p2pSendPbStream congestion / SKB pool exhausted).
     * 同步基准: 首 I 帧那一刻的 wall clock. 每帧应当发送时刻 =
     *   wall_start_ms + (fb->time - ts_base_ms)
     * 还没到 → sleep 等; 到了/迟了 → 立刻发. seek/切文件时清零重新对齐. */
    uint32_t wall_start_ms   = 0;
    /* 软件丢帧开关: 正常情况用 SDK 原生 MSI_VIDEO_DEMUX_JMP_TIME seek, 这里保持 0.
     * 之前 SDK JMP 崩溃原因是 video-only mp4 (audio_samplerate=0) 时除零, 已修. */
    uint32_t pending_jmp_ms  = 0;

    os_printf(KERN_INFO "pb_thread: start\n");

    /* sd_pb_recv 是 rec_playback_init 建的常驻 msi, 循环外 find 一次保持引用,
     * 避免每轮循环进 msi_find (全局 mutex + 遍历链表) 白烧 CPU.
     * 退出时在循环后统一 msi_put. */
    struct msi *recv = msi_find("sd_pb_recv", 1);
    if (!recv) {
        os_printf(KERN_ERR "pb_thread: sd_pb_recv not found, exit\n");
        g_pb.thread_alive = 0;
        return;
    }

    /* 诊断计数: 每秒打印一次, 看视频/音频 fb 流量是否正常 */
    uint32_t pb_stat_t0 = os_jiffies();
    uint32_t pb_stat_v  = 0;   /* 视频 fb 数 */
    uint32_t pb_stat_a  = 0;   /* 音频 fb 数 */
    uint32_t pb_stat_pkt = 0;  /* 实际发出的 G711A 包数 */
    uint32_t pb_stat_busy = 0; /* NET BUSY 重试次数 (不丢帧, 每次 BUSY 计 1) */

    /* 保险 C: 每轮循环无条件让出 CPU 至少 PB_LOOP_YIELD_MS 毫秒.
     *
     * !!! 这里不能用 "测耗时补 sleep" 的方式: TciSendPbFrame 阻塞等 WiFi 发送时
     *     单次循环耗时可能达 20-40ms, 超过 PB_LOOP_MIN_MS 就不会 sleep, 保险失效.
     *     无条件 sleep 强制每轮给其他线程 2ms CPU 时间窗口.
     *
     * 代价分析: 视频 25fps = 40ms/帧. 处理一帧 TciSendPbFrame 典型 5-20ms + 2ms sleep
     *          总周期 < 40ms, 能跟上 25fps; 最大吞吐 ~500Hz 循环, 远超实际需求. */
    #define PB_LOOP_YIELD_MS  20

    while (g_pb.state != PB_ST_STOP_REQ && g_pb.state != PB_ST_IDLE) {

        /* 处理 seek 请求 */
        if (g_pb.seek_req) {
            g_pb.seek_req = 0;
            frames_consumed = 0;
            empty_fb_count  = 0;
            sync_sent       = 0;
            ts_base_ms      = 0;
            g_pb_audio_ts   = 0;
            wall_start_ms   = 0;   /* pacing 基准重新对齐到下个首 I 帧 */
            if (g_pb.demux_msi) {
                msi_destroy(g_pb.demux_msi);
                g_pb.demux_msi = NULL;
            }
            /* 关闭旧 .alaw 文件 */
            if (g_pb_alaw_fp) {
                osal_fclose(g_pb_alaw_fp);
                g_pb_alaw_fp = NULL;
            }
            /* 清空 sd_pb_recv 里残留的 fb (切文件时旧 fb 的 ts 属于上一个文件,
             * 留在队列里下轮发出会让 APP 收到乱序/重复 ts, 且旧 fb 占着 fbpool) */
            {
                struct framebuff *rfb;
                while ((rfb = msi_get_fb(recv, 0)) != NULL) {
                    msi_delete_fb(NULL, rfb);
                }
            }

            char new_date_dir[16];
            char new_fname[FILE_NAME_LEN + 1];
            uint32_t new_t0 = 0;
            if (pb_locate_file(g_pb.seek_t, new_date_dir, new_fname, &new_t0) != 0) {
                os_printf(KERN_ERR "pb: seek %u no match\n", g_pb.seek_t);
                TciSendPbEndOfEvent(g_pb.handle);
                break;
            }
            os_strncpy(g_pb.cur_date_dir, new_date_dir, sizeof(g_pb.cur_date_dir));
            os_strncpy(g_pb.cur_fname, new_fname, sizeof(g_pb.cur_fname));
            os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                        REC_ROOT_PATH, new_date_dir, new_fname);

            /* 打开 demux, 失败 (例如断电残留的坏 MP4 没 moov box) 自动跳下一个文件,
             * 最多尝试 MAX_SKIP 次, 避免一长串坏文件导致死循环. */
            uint32_t jmp_ms = 0;
            {
                const int MAX_SKIP = 8;
                int skipped = 0;
                uint8_t skip_started = 0;   /* 0=当前是 seek 目标文件; 1=已经在跳坏文件 */
                while (1) {
                    /* 正在录制的文件 moov 尚未完整写入, 直接跳过 (不删). */
                    int is_recording = pb_is_current_recording_file(full_path);
                    if (!is_recording && pb_mp4_is_playable(full_path)) {
                        os_snprintf(demux_name, sizeof(demux_name),
                                    "sd_pb_demux_%u", ++demux_seq);
                        g_pb.demux_msi = mp4_demux_msi_init(demux_name, full_path);
                        if (g_pb.demux_msi) break;
                    }
                    if (is_recording) {
                        os_printf(KERN_INFO "pb: skip current recording: %s\n", full_path);
                    } else {
                        os_printf(KERN_ERR "pb: bad mp4: %s (skip %d)\n", full_path, skipped);
                        pb_delete_bad_file(full_path);
                    }
                    if (++skipped >= MAX_SKIP) {
                        os_printf(KERN_ERR "pb: too many bad files, stop\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    /* 找下一个: 对于"正在录"跳过的情况, 当天没别的文件可播了,
                     * pb_locate_file(seek_t) 会把那个同文件返回又进死循环.
                     * 所以用 (new_t0 + 1) 找严格在它之后的文件; 没有就 stop. */
                    char nxt_date_dir[16];
                    char nxt_fname[FILE_NAME_LEN + 1];
                    uint32_t nxt_t0 = 0;
                    uint32_t lookup_t = is_recording ? (new_t0 + 1) : g_pb.seek_t;
                    if (pb_locate_file(lookup_t, nxt_date_dir, nxt_fname, &nxt_t0) != 0) {
                        os_printf(KERN_ERR "pb: no more file after bad/recording, stop\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    os_strncpy(g_pb.cur_date_dir, nxt_date_dir, sizeof(g_pb.cur_date_dir));
                    os_strncpy(g_pb.cur_fname, nxt_fname, sizeof(g_pb.cur_fname));
                    os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                                REC_ROOT_PATH, nxt_date_dir, nxt_fname);
                    new_t0 = nxt_t0;
                    skip_started = 1;
                }
                /* 若跳过了坏文件, seek 偏移语义失效 → 从文件头播 */
                if (skip_started) {
                    jmp_ms = 0;
                } else if (g_pb.seek_t > new_t0) {
                    jmp_ms = (g_pb.seek_t - new_t0) * 1000;
                }
            }
            /* 使用 SDK 原生 MSI_VIDEO_DEMUX_JMP_TIME: demux 内部直接定位到 jmp_ms
             * 对应的关键帧, 不需要软件丢帧 + fast_output 这套绕行逻辑.
             * 之前崩溃根因是 SDK 在 video-only mp4 (audio_samplerate=0) 时 JMP
             * 分支里 "/ (1024*1000/audio_samplerate)" 触发除零; 已在 mp4_demux_msi.c
             * 里加了保护. 现在 JMP_TIME 对 video-only 安全. */
            pending_jmp_ms = 0;    /* 软件丢帧不再需要 */
            msi_add_output(g_pb.demux_msi, NULL, "sd_pb_recv");
            /* JMP_TIME 必须在 START 之前发, 否则 thread 已经在按 PTS 节奏跑了.
             * SDK 内部 set MP4_DEMUX_JMP 事件, thread 启动后走 JMP 分支定位 */
            if (jmp_ms > 0) {
                msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL,
                           MSI_VIDEO_DEMUX_JMP_TIME, jmp_ms);
            }
            msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);

            /* 打开对应 .alaw 文件 (和 mp4 同前缀, 扩展名 .alaw).
             * 不在这里 fseek, 等 sync_sent 触发时按真实 I 帧 fb->time 定位,
             * 保证音画严格对齐 (软件 seek 落点 = 时间戳 >= jmp_ms 的首帧 I) */
            {
                char alaw_path[96];
                int plen = os_strlen(full_path);
                int elen = os_strlen(REC_EXT_NAME);
                os_snprintf(alaw_path, sizeof(alaw_path), "%.*s%s",
                            plen - elen, full_path, REC_AUDIO_EXT_NAME);
                g_pb_alaw_fp = osal_fopen(alaw_path, "rb");
                os_printf(KERN_INFO "pb: alaw file=%s fp=%p (fseek delayed to sync point)\n",
                          alaw_path, g_pb_alaw_fp);
            }

            pending_sync_t0 = new_t0;
            os_printf(KERN_INFO "pb: seek_t=%u, file_t0=%u, file=%s (waiting first I)\n",
                      g_pb.seek_t, new_t0, full_path);
        }

        /* 暂停状态 */
        if (g_pb.state == PB_ST_PAUSED) {
            os_sleep_ms(50);
            continue;
        }

        /* 取 fb (recv 已在循环外 find, 这里直接用) */
        struct framebuff *fb = msi_get_fb(recv, 0);

        if (!fb) {
            /* 还没消费过任何帧 = demux 还没准备好, 继续等.
             * !!! 不能用 continue 直接跳过循环尾部, 否则 pend 里的音频会饿死.
             *     goto 跳到 after_fb_handle, 走末尾的音频节流发送 + yield sleep */
            if (!frames_consumed) {
                goto pb_after_fb_handle;
            }
            /* 已消费过, 连续 N 次没帧 = 文件播完 */
            empty_fb_count++;
            if (empty_fb_count < EMPTY_FB_THRESHOLD) {
                goto pb_after_fb_handle;
            }
            /* 判定文件播完 */
            os_printf(KERN_INFO "pb: file end\n");
            if (g_pb.demux_msi) {
                msi_destroy(g_pb.demux_msi);
                g_pb.demux_msi = NULL;
            }
            if (g_pb_alaw_fp) {
                osal_fclose(g_pb_alaw_fp);
                g_pb_alaw_fp = NULL;
            }
            empty_fb_count  = 0;
            frames_consumed = 0;
            sync_sent       = 0;
            ts_base_ms      = 0;
            g_pb_audio_ts   = 0;
            wall_start_ms   = 0;   /* 切下一文件: pacing 基准重新对齐 */
            /* 清空 sd_pb_recv 里残留的 fb */
            {
                struct framebuff *rfb;
                while ((rfb = msi_get_fb(recv, 0)) != NULL) {
                    msi_delete_fb(NULL, rfb);
                }
            }

            /* 关键: 用户 seek 命令优先于"自动切下一文件".
             * 如果 APP 刚好发来 PLAY_START, 让下一轮走 seek 分支处理, 避免
             * 这里创建新 demux 后又被 seek 分支立刻 destroy 造成 double-free 崩溃 */
            if (g_pb.seek_req) {
                continue;
            }

            if (g_pb.mode_bit == 1) {
                /* 事件模式: 通知结束并暂停 */
                TciSendPbEndOfEvent(g_pb.handle);
                g_pb.state = PB_ST_PAUSED;
                continue;
            }
            /* 连续模式: 在当前日期目录下找下一个. 碰到坏 MP4 (如断电残留没 moov)
             * 继续跳后续文件, 最多跳 MAX_SKIP 次避免死循环. */
            char next_fname[FILE_NAME_LEN + 1];
            uint32_t next_t0 = 0;
            const int MAX_SKIP = 8;
            int skipped = 0;
            g_pb.demux_msi = NULL;
            /* 第一轮: 用 find_next_in_day(cur_fname) 找下一个;
             * 后续若坏文件被删, cur_fname 在目录里找不到了, 改用 pb_locate_file
             * 以坏文件的 t_start+1 为基准找当天后续最近文件. */
            uint8_t first_lookup = 1;
            while (1) {
                if (first_lookup) {
                    if (find_next_in_day(g_pb.cur_date_dir, g_pb.cur_fname,
                                         next_fname, &next_t0) != 0) {
                        os_printf(KERN_INFO "pb: no next file in %s, stop\n",
                                  g_pb.cur_date_dir);
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    first_lookup = 0;
                } else {
                    /* 上一轮 next_t0 就是刚删掉的坏文件 t_start, 以它+1 找下一个 */
                    char tmp_date[16];
                    if (pb_locate_file(next_t0 + 1, tmp_date,
                                       next_fname, &next_t0) != 0) {
                        os_printf(KERN_INFO "pb: no more file after bad, stop\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    os_strncpy(g_pb.cur_date_dir, tmp_date, sizeof(g_pb.cur_date_dir));
                }
                os_strncpy(g_pb.cur_fname, next_fname, sizeof(g_pb.cur_fname));
                os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                            REC_ROOT_PATH, g_pb.cur_date_dir, next_fname);
                int is_recording = pb_is_current_recording_file(full_path);
                if (!is_recording && pb_mp4_is_playable(full_path)) {
                    os_snprintf(demux_name, sizeof(demux_name),
                                "sd_pb_demux_%u", ++demux_seq);
                    g_pb.demux_msi = mp4_demux_msi_init(demux_name, full_path);
                    if (g_pb.demux_msi) break;
                }
                if (is_recording) {
                    os_printf(KERN_INFO "pb: reached current recording %s, stop\n", full_path);
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
                os_printf(KERN_ERR "pb: bad mp4: %s (skip %d)\n", full_path, skipped);
                pb_delete_bad_file(full_path);
                if (++skipped >= MAX_SKIP) {
                    os_printf(KERN_ERR "pb: too many bad files, stop\n");
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
            }
            msi_add_output(g_pb.demux_msi, NULL, "sd_pb_recv");
            msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);
            /* 同步打开新文件的 .alaw */
            {
                char alaw_path[96];
                int plen = os_strlen(full_path);
                int elen = os_strlen(REC_EXT_NAME);
                os_snprintf(alaw_path, sizeof(alaw_path), "%.*s%s",
                            plen - elen, full_path, REC_AUDIO_EXT_NAME);
                g_pb_alaw_fp = osal_fopen(alaw_path, "rb");
                os_printf(KERN_INFO "pb: next alaw=%s fp=%p\n", alaw_path, g_pb_alaw_fp);
            }
            /* 同步帧等第一帧 I 帧到达再发, is_response_to_PLAY_START=0.
             * 自动切下一文件不涉及 seek, pending_jmp_ms 清零从头播 */
            pending_sync_t0 = next_t0;
            pending_jmp_ms  = 0;
            os_printf(KERN_INFO "pb: next file %s (waiting first I)\n", full_path);
            continue;
        }

        /* 取到 fb, 重置空闲计数并标记已消费 */
        empty_fb_count  = 0;
        frames_consumed = 1;

        /* 诊断计数 */
        if (fb->mtype == F_H264) pb_stat_v++;
        else if (fb->mtype == F_AUDIO) pb_stat_a++;

        /* 探鸽 SDK 对回放流的首帧要求必须是 I 帧才放行, 所以:
         * 1) sync_sent=0 时, 非 I 帧的视频帧 和 所有音频帧 全部丢弃
         * 2) 软件 seek: fb->time < pending_jmp_ms 的 I 帧也丢, 等到时间达标的第一帧 I
         * 3) 收到符合的第一帧 I 帧 -> 发送同步时间帧, sync_sent=1, 跳到 .alaw 对应偏移
         * 4) 之后正常发送视频 P 帧 + 音频 */
        if (!sync_sent) {
            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                if (!priv || priv->type != 1) {
                    msi_delete_fb(NULL, fb);
                    continue;
                }
                /* 防御性保留: SDK JMP_TIME 正常时 pending_jmp_ms=0, 此处不触发.
                 * 万一未来有需要软件丢帧的场景 (如 SDK JMP_TIME 又挂了) 可直接启用 */
                if (pending_jmp_ms > 0 && fb->time < pending_jmp_ms) {
                    msi_delete_fb(NULL, fb);
                    continue;
                }
                /* I 帧到达且时间戳 >= pending_jmp_ms, 触发同步 */
                uint32_t first_utc = pending_sync_t0 + fb->time / 1000;
                TciSendPbSyncFrame(g_pb.handle, first_utc, 1);
                sync_sent = 1;
                ts_base_ms = fb->time;
                /* PTS pacing 基准点: 首 I 帧此刻的 wall clock, 后续帧按
                 * (fb->time - ts_base_ms) 偏移到 wall_start_ms 上发送 */
                wall_start_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies());
                /* alaw 文件定位到这个 I 帧对应时间: 8 字节/ms */
                if (g_pb_alaw_fp) {
                    osal_fseek(g_pb_alaw_fp, fb->time * 8);
                }
                g_pb_audio_ts = 0;   /* 音频 ts 从同步点起重新从 0 开始 */
                os_printf(KERN_INFO "pb: sync_sent utc=%u (file_t0=%u + fb_time=%u ms, ts_base=%u)\n",
                          first_utc, pending_sync_t0, fb->time, ts_base_ms);
                /* 继续走下面的发送逻辑, 不 continue */
            } else {
                /* 还没首 I, 丢掉音频 */
                msi_delete_fb(NULL, fb);
                continue;
            }
        }

        /* 按速度决定是否发送 */
        int skip = 0;
        if (g_pb.speed == PB_SPEED_IKEY) {
            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                if (!priv || priv->type != 1) skip = 1;
            } else {
                skip = 1;   /* 只关键帧模式不发音频 */
            }
        }

        if (!skip) {
            /* 视频 ts = fb->time - ts_base 归零; 首 I 帧 ts=0 对齐同步帧 utc.
             * 音频 ts 独立管理 (g_pb_audio_ts), 固定 40ms 步进, 和视频同基准. */
            uint32_t ts = (fb->time > ts_base_ms) ? (fb->time - ts_base_ms) : 0;

            /* PTS pacing: 严格按文件原始时间节奏发, 防止 demux 出帧太快
             * 把探鸽 P2P 缓冲撑爆 (p2pSendPbStream congestion).
             * expected_wall = wall_start_ms + ts; 还没到就 sleep 等到那时. */
            if (wall_start_ms && fb->mtype == F_H264) {
                uint32_t now_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies());
                uint32_t expected_wall = wall_start_ms + ts;
                int32_t delta = (int32_t)(expected_wall - now_ms);
                if (delta > 0 && delta < 5000) {  /* 上限 5s 防异常 ts 跳变 */
                    os_sleep_ms(delta);
                }
            }

            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                int flags = (priv && priv->type == 1) ? FF_KEYFRAME : 0;

                /* BUSY 重试: 不丢帧, sleep PB_BUSY_SLEEP_MS 后重发同一帧.
                 * 循环中同时检查 state/seek_req, 避免连接断时死循环. */
                int retry = 0;
                int send_ret;
                while (1) {
                    send_ret = TciSendPbFrame(g_pb.handle, TCMEDIA_VIDEO_H264,
                                              fb->data, fb->len, ts, flags);
                    if (send_ret != TCE_NETWORK_BUSY_VAL) break;
                    pb_stat_busy++;
                    if (++retry >= PB_BUSY_MAX_RETRY ||
                        g_pb.state == PB_ST_STOP_REQ || g_pb.state == PB_ST_IDLE ||
                        g_pb.seek_req) {
                        os_printf(KERN_WARNING "pb: video BUSY give up after %d retries\n", retry);
                        break;
                    }
                    os_sleep_ms(PB_BUSY_SLEEP_MS);
                }

                /* 视频驱动音频: 每次收到视频帧后, 根据视频 ts 和音频 ts 的差值,
                 * 补发对应数量的 .alaw 包, 实现音画同步.
                 * 每个 alaw 包 40ms × 320B, 需要补几包就读几包.
                 * 音频 BUSY 时同样 sleep 后重发, 不丢包 (保证音频时序). */
                if (g_pb_alaw_fp) {
                    int pkts_this_round = 0;
                    while (g_pb_audio_ts < ts && pkts_this_round < 8) {
                        int n = osal_fread(g_pb_alaw_buf, 1, PB_PACK_BYTES, g_pb_alaw_fp);
                        if (n != PB_PACK_BYTES) {
                            /* .alaw 文件读完 (视频比音频短) 或读失败, 本次停止.
                             * n=0 时 fp 可能已失效 (FATFS reinit / 卷重挂),
                             * fclose + NULL 化, 否则下次进来又会 fread 出 res:9 刷屏 */
                            if (n == 0) {
                                os_printf(KERN_WARNING "pb: alaw fread=0, close stale fp (may be FATFS reinit) fp=%p\n",
                                          g_pb_alaw_fp);
                                osal_fclose(g_pb_alaw_fp);
                                g_pb_alaw_fp = NULL;
                            }
                            break;
                        }
                        int a_retry = 0;
                        int aret;
                        while (1) {
                            aret = TciSendPbFrame(g_pb.handle, TCMEDIA_AUDIO_G711A,
                                                  g_pb_alaw_buf, PB_PACK_BYTES,
                                                  g_pb_audio_ts, 2);
                            if (aret != TCE_NETWORK_BUSY_VAL) break;
                            pb_stat_busy++;
                            if (++a_retry >= PB_BUSY_MAX_RETRY ||
                                g_pb.state == PB_ST_STOP_REQ || g_pb.state == PB_ST_IDLE ||
                                g_pb.seek_req) {
                                os_printf(KERN_WARNING "pb: audio BUSY give up after %d retries\n",
                                          a_retry);
                                break;
                            }
                            os_sleep_ms(PB_BUSY_SLEEP_MS);
                        }
                        g_pb_audio_ts += PB_PACK_INTERVAL_MS;
                        pkts_this_round++;
                        pb_stat_pkt++;
                    }
                }
            }
            /* mp4 里已没有 F_AUDIO 轨 (audio_encode=0), 音频完全由 .alaw 驱动, 无需处理 */
        }

        msi_delete_fb(NULL, fb);

pb_after_fb_handle:
        /* 每秒打印一次诊断 */
        if (os_jiffies() - pb_stat_t0 >= 1000) {
            os_printf(KERN_INFO "pb stat: v_fb=%u a_pkt=%u busy=%u audio_ts=%u\n",
                      pb_stat_v, pb_stat_pkt, pb_stat_busy, g_pb_audio_ts);
            pb_stat_v = 0;
            pb_stat_a = 0;
            pb_stat_pkt = 0;
            pb_stat_busy = 0;
            pb_stat_t0 = os_jiffies();
        }

        /* 保险 C: 无条件让出 CPU, 无论前面处理了多久 */
        os_sleep_ms(PB_LOOP_YIELD_MS);
    }

pb_exit:
    /* 清理: 只销毁 demux msi; sd_pb_recv 是 rec_playback_init 创建的
     * 常驻 msi, 不销毁以避免同名冲突 */
    if (g_pb.demux_msi) {
        msi_destroy(g_pb.demux_msi);
        g_pb.demux_msi = NULL;
    }
    if (g_pb_alaw_fp) {
        osal_fclose(g_pb_alaw_fp);
        g_pb_alaw_fp = NULL;
    }
    /* 清空 sd_pb_recv 里残留的 fb (下次 pb_start 看到旧帧会混乱), 复用循环外的 recv */
    {
        struct framebuff *fb;
        while ((fb = msi_get_fb(recv, 0)) != NULL) {
            msi_delete_fb(NULL, fb);
        }
    }
    msi_put(recv);
    g_pb.state = PB_ST_IDLE;
    g_pb.thread_alive = 0;
    os_printf(KERN_INFO "pb_thread: exit\n");
}

int pb_start(void *handle, uint32_t t_seek, uint8_t mode_bit)
{
    if (!g_rp_inited) return -1;

    /* 懒初始化 g_pb.lock */
    static uint8_t pb_lock_inited = 0;
    if (!pb_lock_inited) {
        os_mutex_init(&g_pb.lock);
        pb_lock_inited = 1;
    }

    os_mutex_lock(&g_pb.lock, -1);

    if (g_pb.thread_alive) {
        /* 已有线程: 触发 seek */
        g_pb.handle = (p2phandle_t) handle;
        g_pb.seek_t = t_seek;
        g_pb.seek_req = 1;
        g_pb.mode_bit = mode_bit;
        g_pb.state = PB_ST_PLAYING;
        os_mutex_unlock(&g_pb.lock);
        os_printf(KERN_INFO "pb_start: reseek to %u\n", t_seek);
        return 0;
    }

    g_pb.handle = (p2phandle_t) handle;
    g_pb.seek_t = t_seek;
    g_pb.seek_req = 1;
    g_pb.mode_bit = mode_bit;
    g_pb.speed = PB_SPEED_1X;
    g_pb.state = PB_ST_PLAYING;
    g_pb.demux_msi = NULL;

    /* sd_pb_recv 已在 rec_playback_init 创建, 这里不再重建 */

    /* 栈交给 OS 自动分配 (stack=NULL 触发 krhino_task_dyn_create): 走 kernel
     * 系统堆 (SRAM), task 自然 return 时 OS 自动回收, 不会泄漏.
     * 4KB 足够 FatFS + mp4_demux + TciSendPbFrame 链路 */
    void *hdl = os_task_create("pb_thread", (os_task_func_t)pb_thread, NULL,
                               OS_TASK_PRIORITY_NORMAL, 0, NULL, 4096);
    (void)hdl;  /* 不保存 handle, pb_stop 通过 g_pb.thread_alive 同步退出 */

    os_mutex_unlock(&g_pb.lock);
    return 0;
}

int pb_stop(void)
{
    if (!g_pb.thread_alive) return 0;
    g_pb.state = PB_ST_STOP_REQ;
    int wait = 0;
    while (g_pb.thread_alive && wait++ < 100)
        os_sleep_ms(10);
    return 0;
}

int pb_pause(void)
{
    if (g_pb.state == PB_ST_PLAYING) {
        g_pb.state = PB_ST_PAUSED;
        os_printf(KERN_INFO "pb_pause\n");
    }
    return 0;
}

int pb_resume(void)
{
    if (g_pb.state == PB_ST_PAUSED) {
        g_pb.state = PB_ST_PLAYING;
        os_printf(KERN_INFO "pb_resume\n");
    }
    return 0;
}

int pb_set_forward(uint32_t param)
{
    if (param == 0) g_pb.speed = PB_SPEED_1X;
    else if (param == 1) g_pb.speed = PB_SPEED_2X;
    else if (param == 2) g_pb.speed = PB_SPEED_4X;
    else g_pb.speed = PB_SPEED_IKEY;

    /* APP 的交互: PAUSE -> FORWARD 希望从暂停点按新倍速继续播, 所以:
     *   - 若当前处于 PAUSED, 自动切回 PLAYING, 从暂停位置继续
     *   - 若已在 PLAYING, 仅切倍速
     * 不做任何 seek, 文件偏移 / demux 状态保持不变 */
    if (g_pb.state == PB_ST_PAUSED) {
        g_pb.state = PB_ST_PLAYING;
        os_printf(KERN_INFO "pb_set_forward: speed=%d, resume from pause\n", g_pb.speed);
    } else {
        os_printf(KERN_INFO "pb_set_forward: speed=%d\n", g_pb.speed);
    }
    return 0;
}

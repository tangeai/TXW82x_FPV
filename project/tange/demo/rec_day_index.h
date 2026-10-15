/* 单 MP4 模块私有实现：不单独编译，不依赖结构体在磁盘上的布局。
 * record.index 是可重建的辅助索引，录像文件仍是最终依据。
 * 每条记录 32 字节、小端、CRC32；BEGIN 落盘后才改录像目录，COMMIT 最后写。
 * 断电残尾/未提交事务一律回退目录，不根据索引删除录像。
 * 首次挂载后每个访问日期核对一次目录，兼容电脑增删文件和更换 SD 卡。
 */
#if REC_DAY_INDEX_ENABLE
#define REC_IDX_MAX_ITEMS 2048U
#define REC_IDX_MAX_RECORDS 8192U
#define REC_IDX_NAME "record.index"
#define REC_IDX_TEMP "record.index.tmp"
#define REC_IDX_MAGIC 0x31495854U
#define REC_IDX_HEADER 1U
#define REC_IDX_ADD 2U
#define REC_IDX_DEL 3U
#define REC_IDX_BEGIN 4U
#define REC_IDX_COMMIT 5U

extern uint32_t fatfs_storage_generation(void);
extern uint8_t get_fat_isready(void);
static struct os_mutex g_idx_lock;
static uint8_t g_idx_inited;
static uint32_t g_idx_epoch, g_idx_format;
static scan_item_t *g_idx_cache;
static uint32_t g_idx_count;
static char g_idx_date[9];
static long g_idx_timezone;
static char g_idx_checked[16][9];
static unsigned g_idx_checked_next;
static char g_idx_pending[4][9];
static unsigned g_idx_pending_next;
static uint32_t g_idx_retry_at;

static void rec_idx_drop_cache(void)
{
    if (g_idx_cache) _os_free_psram(g_idx_cache);
    g_idx_cache = NULL;
    g_idx_date[0] = 0;
    g_idx_count = 0;
}
static void rec_idx_clear_locked(void)
{
    rec_idx_drop_cache();
    os_memset(g_idx_checked, 0, sizeof(g_idx_checked));
    os_memset(g_idx_pending, 0, sizeof(g_idx_pending));
    g_idx_checked_next = 0;
    g_idx_pending_next = 0;
    g_idx_retry_at = 0;
    g_idx_epoch = fatfs_storage_generation();
    g_idx_format = g_sd_generation;
}
static void rec_idx_init(void)
{
    if (!g_idx_inited && os_mutex_init(&g_idx_lock) == RET_OK) {
        g_idx_inited = 1;
        rec_idx_clear_locked();
    }
}
static void rec_idx_reset(void)
{
    if (!g_idx_inited) return;
    os_mutex_lock(&g_idx_lock, osWaitForever);
    rec_idx_clear_locked();
    os_mutex_unlock(&g_idx_lock);
}
static int rec_idx_media_locked(void)
{
    if (g_idx_epoch != fatfs_storage_generation() || g_idx_format != g_sd_generation)
        rec_idx_clear_locked();
    return !g_sd_formatting && get_fat_isready();
}
static int rec_idx_stable(uint32_t epoch)
{
    return !g_sd_formatting && get_fat_isready() && epoch == fatfs_storage_generation();
}
static void rec_idx_queue(const char *date)
{
    for (unsigned i = 0; i < 4; ++i)
        if (!os_strcmp(g_idx_pending[i], date)) return;
    for (unsigned i = 0; i < 4; ++i) {
        if (!g_idx_pending[i][0]) { os_strcpy(g_idx_pending[i], date); return; }
    }
    /* 队列满不影响录像，下次查询缺失索引时会再次排队。 */
}
static int rec_idx_checked(const char *date)
{
    for (unsigned i = 0; i < 16; ++i)
        if (!os_strcmp(g_idx_checked[i], date)) return 1;
    return 0;
}
static void rec_idx_mark_checked(const char *date)
{
    if (!rec_idx_checked(date)) os_strcpy(g_idx_checked[g_idx_checked_next++ % 16], date);
}
static void rec_idx_uncheck(const char *date)
{
    for (unsigned i = 0; i < 16; ++i)
        if (!os_strcmp(g_idx_checked[i], date)) g_idx_checked[i][0] = 0;
}
static void rec_idx_path(const char *date, const char *name, char *path)
{
    os_snprintf(path, 64, "%s/%s/%s", REC_ROOT_PATH, date, name);
}
static uint32_t rec_idx_crc(const uint8_t *p, unsigned len)
{
    uint32_t crc = 0xffffffffU;
    while (len--) {
        crc ^= *p++;
        for (unsigned n = 0; n < 8; ++n)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
static void rec_idx_pack(uint8_t *b, const uint32_t *w)
{
    for (unsigned i = 0; i < 7; ++i)
        for (unsigned j = 0; j < 4; ++j) b[i * 4 + j] = w[i] >> (j * 8);
    uint32_t crc = rec_idx_crc(b, 28);
    for (unsigned j = 0; j < 4; ++j) b[28 + j] = crc >> (j * 8);
}
static int rec_idx_unpack(const uint8_t *b, uint32_t *w)
{
    for (unsigned i = 0; i < 8; ++i) {
        w[i] = 0;
        for (unsigned j = 0; j < 4; ++j) w[i] |= (uint32_t)b[i * 4 + j] << (j * 8);
    }
    return w[0] == REC_IDX_MAGIC && w[7] == rec_idx_crc(b, 28);
}
static uint32_t rec_idx_daynum(const char *date)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < 8; ++i) n = n * 10 + date[i] - '0';
    return n;
}
/* FIL 含扇区缓冲，动态放 PSRAM，不压回放/后台任务栈。 */
static FIL *rec_idx_open(const char *path, BYTE mode, FRESULT *res)
{
    FIL *fp = _os_malloc_psram(sizeof(*fp));
    if (!fp) { *res = FR_NOT_ENOUGH_CORE; return NULL; }
    *res = f_open(fp, path, mode);
    if (*res != FR_OK) { _os_free_psram(fp); return NULL; }
    return fp;
}
static int rec_idx_close(FIL *fp)
{
    FRESULT res = f_close(fp);
    _os_free_psram(fp);
    return res == FR_OK ? 0 : -1;
}
static int rec_idx_read_record(FIL *fp, uint32_t *w)
{
    uint8_t b[32]; UINT got = 0;
    if (f_read(fp, b, sizeof(b), &got) != FR_OK) return -1;
    return got == sizeof(b) && rec_idx_unpack(b, w) ? 0 : -2;
}
static int rec_idx_write_record(FIL *fp, uint32_t op, uint32_t seq, const char *date,
                                const scan_item_t *it, uint32_t value)
{
    uint32_t w[8] = {REC_IDX_MAGIC, op, seq, rec_idx_daynum(date), 0, 0, value, 0};
    uint8_t b[32]; UINT put = 0;
    if (it) {
        w[4] = it->hh * 3600U + it->mm * 60U + it->ss;
        w[5] = ((uint32_t)it->duration << 8) | it->event;
    }
    rec_idx_pack(b, w);
    return f_write(fp, b, sizeof(b), &put) == FR_OK && put == sizeof(b) ? 0 : -1;
}
static int rec_idx_item(const char *date, const uint32_t *w, scan_item_t *it)
{
    uint16_t y; uint8_t m, d;
    if (parse_dirname(date, &y, &m, &d) || w[4] >= 86400 ||
        !(w[5] >> 8) || (w[5] >> 8) > 65535 || (w[5] & 255) > 99) return -1;
    os_memset(it, 0, sizeof(*it));
    it->hh = w[4] / 3600;
    it->mm = (w[4] / 60) % 60;
    it->ss = w[4] % 60;
    it->duration = w[5] >> 8;
    it->event = w[5] & 255;
    it->t_start = tmval_to_utc(y, m, d, it->hh, it->mm, it->ss);
    return 0;
}
static int rec_idx_equal(const scan_item_t *a, const scan_item_t *b)
{
    return a->hh == b->hh && a->mm == b->mm && a->ss == b->ss &&
           a->event == b->event && a->duration == b->duration;
}
static void rec_idx_cache_set(const char *date, const scan_item_t *arr, uint32_t count)
{
    rec_idx_drop_cache();
    if (count > REC_IDX_MAX_ITEMS) return;
    if (count) {
        g_idx_cache = _os_malloc_psram(count * sizeof(*arr));
        if (!g_idx_cache) return;
        os_memcpy(g_idx_cache, arr, count * sizeof(*arr));
    }
    os_strcpy(g_idx_date, date);
    g_idx_timezone = _tg_timezone_;
    g_idx_count = count;
}
/* -2 表示缺失/结构损坏，可重建；-1 表示 IO/内存失败，不能当空目录。 */
static int rec_idx_load(const char *date, scan_item_t **out, uint32_t *count)
{
    char path[64]; FRESULT res;
    rec_idx_path(date, REC_IDX_NAME, path);
    FIL *fp = rec_idx_open(path, FA_READ, &res);
    if (!fp) return res == FR_NO_FILE || res == FR_NO_PATH ? -2 : -1;
    uint32_t size = f_size(fp), w[8], used = 0, seq = 0, expected = 0;
    int pending = 1, changed = 0, ret = -2;
    scan_item_t *arr = NULL;
    if (size < 64 || size % 32 || size / 32 > REC_IDX_MAX_RECORDS) goto done;
    arr = _os_malloc_psram(REC_IDX_MAX_ITEMS * sizeof(*arr));
    if (!arr) { ret = -1; goto done; }
    for (uint32_t off = 0; off < size; off += 32, ++seq) {
        if (g_sd_formatting) { ret = -1; goto done; }
        int rr = rec_idx_read_record(fp, w);
        if (rr) { ret = rr; goto done; }
        if (w[2] != seq || w[3] != rec_idx_daynum(date)) goto done;
        if (!off) {
            if (w[1] != REC_IDX_HEADER || w[6] != 1) goto done;
            continue;
        }
        if (w[1] == REC_IDX_BEGIN) {
            if (pending || (w[6] != REC_IDX_ADD && w[6] != REC_IDX_DEL)) goto done;
            pending = 1; changed = 0; expected = w[6];
        } else if (w[1] == REC_IDX_COMMIT) {
            if (!pending || (expected && !changed) || w[6] != used) goto done;
            pending = 0;
        } else if (w[1] == REC_IDX_ADD || w[1] == REC_IDX_DEL) {
            scan_item_t it;
            if (!pending || (expected && (changed || expected != w[1])) ||
                (!expected && w[1] != REC_IDX_ADD) || rec_idx_item(date, w, &it)) goto done;
            uint32_t i;
            for (i = 0; i < used && !rec_idx_equal(&arr[i], &it); ++i) {}
            if (w[1] == REC_IDX_ADD) {
                if (i != used || used == REC_IDX_MAX_ITEMS) goto done;
                arr[used++] = it;
            } else {
                if (i == used) goto done;
                arr[i] = arr[--used];
            }
            changed = 1;
        } else goto done;
    }
    if (pending) goto done;
    if (used > 1) qsort(arr, used, sizeof(*arr), cmp_scan_item);
    *out = arr; *count = used; arr = NULL; ret = 0;
done:
    if (arr) _os_free_psram(arr);
    if (rec_idx_close(fp)) ret = -1;
    return ret;
}
static int rec_idx_get_day(const char *date, scan_item_t **out, uint32_t *count)
{
    *out = NULL; *count = 0;
    if (!g_idx_inited) return scan_day_dir(date, out, count);
    if (os_mutex_lock(&g_idx_lock, 1000) != RET_OK) return -1;
    int ret = -1;
    scan_item_t *disk = NULL; uint32_t n = 0;
    if (!rec_idx_media_locked()) goto done;
    uint32_t epoch = g_idx_epoch;
    if (os_strcmp(g_idx_date, date) || g_idx_timezone != _tg_timezone_) {
        rec_idx_drop_cache();
        if (rec_idx_checked(date)) ret = rec_idx_load(date, &disk, &n);
        else ret = -2;
        if (!ret) {
            rec_idx_cache_set(date, disk, n);
            os_printf(KERN_INFO "rec_idx: disk %s count=%u\n", date, n);
        } else if (ret == -2) {
            /* 查询只读目录；持久化重建由后台完成，不能在通信线程写索引。 */
            /* 已持有索引锁，删除事务尚未提交时该文件仍应在索引中。
             * 不能套用旧扫描的“正在删除”过滤，否则锁竞争会造成永久漏项。 */
            ret = scan_day_dir_raw(date, out, count, 0);
            if (!ret && rec_idx_stable(epoch)) {
                rec_idx_cache_set(date, *out, *count);
                rec_idx_queue(date);
                os_printf(KERN_INFO "rec_idx: scan fallback %s count=%u\n", date, *count);
            }
            goto stable;
        } else goto done;
    }
    if (!g_idx_date[0]) { ret = -1; goto done; }  /* 缓存申请失败不能返回空列表。 */
    if (g_idx_count) {
        *out = RP_MALLOC(g_idx_count * sizeof(**out));
        if (!*out) { ret = -1; goto done; }
        os_memcpy(*out, g_idx_cache, g_idx_count * sizeof(**out));
    }
    *count = g_idx_count; ret = 0;
    os_printf(KERN_INFO "rec_idx: cache %s count=%u\n", date, *count);
stable:
    if (!rec_idx_stable(epoch)) {
        if (*out) RP_FREE(*out);
        *count = 0; ret = -1;
        rec_idx_clear_locked();
    }
done:
    if (disk) _os_free_psram(disk);
    os_mutex_unlock(&g_idx_lock);
    return ret;
}
static int rec_idx_rebuild_locked(const char *date)
{
    scan_item_t *arr = NULL; uint32_t count = 0, seq = 0;
    char path[64], temp[64]; FRESULT res;
    uint32_t epoch = g_idx_epoch;
    if (scan_day_dir_raw(date, &arr, &count, 0)) return -1;
    if (count > REC_IDX_MAX_ITEMS) {
        os_printf(KERN_WARNING "rec_idx: day %s exceeds cache limit (%u), use scan\n", date, count);
        RP_FREE(arr); return 1;
    }
    rec_idx_path(date, REC_IDX_NAME, path);
    rec_idx_path(date, REC_IDX_TEMP, temp);
    FIL *fp = rec_idx_open(temp, FA_CREATE_ALWAYS | FA_WRITE, &res);
    if (!fp) {
        RP_FREE(arr);
        /* 查询没有录像的日期不会创建目录，也不能让该日期一直占住重建队列。 */
        return !count && res == FR_NO_PATH && rec_idx_stable(epoch) ? 1 : -1;
    }
    int ret = rec_idx_write_record(fp, REC_IDX_HEADER, seq++, date, NULL, 1);
    for (uint32_t i = 0; !ret && i < count; ++i) {
        if (!rec_idx_stable(epoch)) { ret = -1; break; }
        ret = rec_idx_write_record(fp, REC_IDX_ADD, seq++, date, &arr[i], 0);
    }
    if (!ret) ret = rec_idx_write_record(fp, REC_IDX_COMMIT, seq, date, NULL, count);
    if (!ret && f_sync(fp) != FR_OK) ret = -1;
    if (rec_idx_close(fp)) ret = -1;
    if (!rec_idx_stable(epoch)) ret = -1;
    if (!ret) {
        res = f_unlink(path);
        if (res != FR_OK && res != FR_NO_FILE) ret = -1;
        if (!ret && f_rename(temp, path) != FR_OK) ret = -1;
    }
    if (!rec_idx_stable(epoch)) ret = -1;
    if (!ret) {
        rec_idx_mark_checked(date);
        rec_idx_cache_set(date, arr, count);
        os_printf(KERN_INFO "rec_idx: rebuilt %s count=%u bytes=%u\n", date, count, (count + 2) * 32);
    }
    else rec_idx_uncheck(date);
    if (arr) RP_FREE(arr);
    return ret;
}
static void rec_idx_background(void)
{
    if (!g_idx_inited || g_sd_formatting || rec_pb_is_active()) return;
    if (os_mutex_lock(&g_idx_lock, 0) != RET_OK) return;
    if (rec_idx_media_locked() && (!g_idx_retry_at ||
        (int32_t)((uint32_t)os_jiffies() - g_idx_retry_at) >= 0)) {
        for (unsigned n = 0; n < 4; ++n) {
            unsigned i = (g_idx_pending_next + n) % 4;
            if (!g_idx_pending[i][0]) continue;
            char date[9]; os_strcpy(date, g_idx_pending[i]);
            int ret = rec_idx_rebuild_locked(date);
            /* 单日故障退避后轮转，避免一直阻塞其它日期。 */
            g_idx_pending_next = (i + 1) % 4;
            if (ret >= 0) { g_idx_pending[i][0] = 0; g_idx_retry_at = 0; }
            else g_idx_retry_at = (uint32_t)os_jiffies() + 30000U;
            break;
        }
    }
    os_mutex_unlock(&g_idx_lock);
}
/* 从最终 MP4 路径提取索引条目；UNSYNC/临时文件不进日索引。 */
static int rec_idx_parse_path(const char *path, char *date, scan_item_t *it)
{
    unsigned root = sizeof(REC_ROOT_PATH) - 1;
    if (os_strncmp(path, REC_ROOT_PATH "/", root + 1) || os_strlen(path) < root + 11 ||
        path[root + 9] != '/' || !rec_has_ext(path, REC_EXT_NAME)) return -1;
    os_memcpy(date, path + root + 1, 8); date[8] = 0;
    uint16_t y; uint8_t m, d;
    if (parse_dirname(date, &y, &m, &d)) return -1;
    os_memset(it, 0, sizeof(*it));
    if (parse_filename(path + root + 10, &it->event, &it->duration, &it->hh, &it->mm, &it->ss)) return -1;
    it->t_start = tmval_to_utc(y, m, d, it->hh, it->mm, it->ss);
    return 0;
}
/* 返回已持有的日志文件；无索引时无需日志，待变更后后台建立。
 * 尾部不是完整提交时先移除辅助索引，绝不移除录像文件。 */
static int rec_idx_prepare(const char *date, uint32_t op, const scan_item_t *it,
                           FIL **out, uint32_t *seq, uint32_t *count)
{
    char path[64]; FRESULT res; uint32_t w[8];
    *out = NULL;
    rec_idx_path(date, REC_IDX_NAME, path);
    FIL *fp = rec_idx_open(path, FA_READ | FA_WRITE, &res);
    if (!fp) return res == FR_NO_FILE || res == FR_NO_PATH ? 0 : -1;
    uint32_t size = f_size(fp);
    int rr = rec_idx_read_record(fp, w);
    if (rr == -1) { rec_idx_close(fp); return -1; }
    int valid = !rr && w[1] == REC_IDX_HEADER && w[2] == 0 &&
                w[3] == rec_idx_daynum(date) && w[6] == 1 && size >= 64 && size % 32 == 0;
    if (valid) {
        if (f_lseek(fp, size - 32) != FR_OK) { rec_idx_close(fp); return -1; }
        rr = rec_idx_read_record(fp, w);
        if (rr == -1) { rec_idx_close(fp); return -1; }
        valid = !rr && w[1] == REC_IDX_COMMIT && w[2] == size / 32 - 1 &&
                w[3] == rec_idx_daynum(date) && w[6] <= REC_IDX_MAX_ITEMS;
    }
    if (!valid || size / 32 + 3 > REC_IDX_MAX_RECORDS ||
        (op == REC_IDX_DEL && !w[6]) || (op == REC_IDX_ADD && w[6] == REC_IDX_MAX_ITEMS)) {
        if (rec_idx_close(fp)) return -1;
        if (f_unlink(path) != FR_OK) return -1;
        rec_idx_uncheck(date);
        return 0;
    }
    *seq = size / 32;
    *count = w[6];
    if (rec_idx_write_record(fp, REC_IDX_BEGIN, (*seq)++, date, it, op) || f_sync(fp) != FR_OK) {
        rec_idx_close(fp); return -1;
    }
    *out = fp;
    return 0;
}
static FRESULT rec_idx_change(const char *src, const char *dest)
{
    char date[9]; scan_item_t it;
    if (!g_idx_inited || rec_idx_parse_path(dest ? dest : src, date, &it))
        return dest ? osal_rename(src, dest) : osal_unlink(src);
    /* 删旧可能由编码线程兜底触发，锁忙时直接让出，不能堵住编码队列。 */
    if (os_mutex_lock(&g_idx_lock, dest ? 100 : 0) != RET_OK) return FR_TIMEOUT;
    FRESULT res = FR_NOT_READY;
    FIL *journal = NULL; uint32_t seq = 0, count = 0;
    uint32_t op = dest ? REC_IDX_ADD : REC_IDX_DEL;
    if (!rec_idx_media_locked()) goto done;
    uint32_t epoch = g_idx_epoch;
    rec_idx_drop_cache();
    if (rec_idx_prepare(date, op, &it, &journal, &seq, &count)) {
        rec_idx_uncheck(date); rec_idx_queue(date); res = FR_DISK_ERR; goto done;
    }
    if (!rec_idx_stable(epoch)) goto close;
    res = dest ? osal_rename(src, dest) : osal_unlink(src);
    int committed = 0;
    if (journal && res == FR_OK && rec_idx_stable(epoch)) {
        count = dest ? count + 1 : count - 1;
        committed = !rec_idx_write_record(journal, op, seq++, date, &it, 0) &&
                    !rec_idx_write_record(journal, REC_IDX_COMMIT, seq, date, NULL, count) &&
                    f_sync(journal) == FR_OK;
    }
    if (!committed) { rec_idx_uncheck(date); rec_idx_queue(date); }
    else if (seq > (count + 2) * 2 + 128) rec_idx_queue(date); /* 后台压缩删除记录。 */
close:
    if (journal && rec_idx_close(journal)) { rec_idx_uncheck(date); rec_idx_queue(date); }
    /* 发布已成功而提交失败时保留 MP4，未完成事务会触发重建，不重复发布。 */
done:
    if (res != FR_OK) os_printf(KERN_WARNING "rec_idx: change deferred res=%d %s\n", res, src);
    os_mutex_unlock(&g_idx_lock);
    return res;
}
static void rec_idx_empty_day(const char *date)
{
    if (!g_idx_inited || os_mutex_lock(&g_idx_lock, 0) != RET_OK) return;
    /* 只清理本功能拥有的两个索引文件，目录中其它文件不碰。 */
    rec_idx_drop_cache(); rec_idx_uncheck(date);
    for (unsigned i = 0; i < 4; ++i)
        if (!os_strcmp(g_idx_pending[i], date)) g_idx_pending[i][0] = 0;
    char path[64];
    rec_idx_path(date, REC_IDX_NAME, path); f_unlink(path);
    rec_idx_path(date, REC_IDX_TEMP, path); f_unlink(path);
    os_mutex_unlock(&g_idx_lock);
}
#else
static void rec_idx_init(void) {}
static void rec_idx_reset(void) {}
static void rec_idx_background(void) {}
static void rec_idx_empty_day(const char *date) { (void)date; }
static int rec_idx_get_day(const char *date, scan_item_t **arr, uint32_t *count)
{ return scan_day_dir(date, arr, count); }
static FRESULT rec_idx_change(const char *src, const char *dest)
{ return dest ? osal_rename(src, dest) : osal_unlink(src); }
#endif

/* 仅供单 MP4 录像实现包含，不作为独立编译单元。
 * 只恢复本机 .REC 的已落盘索引前缀；不猜测缺失的帧边界，不处理外部 MP4。
 * 返回 1=恢复成功，0=结构严重损坏，-1=IO/资源异常或操作被取消。 */
typedef struct {
    uint32_t stsz, stco, stts, stss, mdhd, tkhd, stsc;
    uint32_t sizes, offsets, times, keys, scale, keep, time_entries, duration;
} rec_fix_track;
typedef struct {
    F_FILE *fp;
    uint32_t size, mdat, data_end, mvhd, movie_scale, tracks;
    rec_fix_track t[2];
} rec_fix;

static int rec_fix_read(rec_fix *r, uint32_t off, void *buf, uint32_t len)
{
    if (g_sd_formatting || off > r->size || len > r->size - off) return -1;
    return osal_fseek(r->fp, off) == FR_OK &&
           osal_fread(buf, 1, len, r->fp) == len ? 0 : -1;
}
static int rec_fix_u32(rec_fix *r, uint32_t off, uint32_t *value)
{
    uint8_t b[4];
    if (rec_fix_read(r, off, b, 4)) return -1;
    *value = rec_mp4_be32(b);
    return 0;
}
static int rec_fix_write(rec_fix *r, uint32_t off, uint32_t value)
{
    uint8_t b[4] = {value >> 24, value >> 16, value >> 8, value};
    if (g_sd_formatting || osal_fseek(r->fp, off) != FR_OK) return -1;
    return osal_fwrite(b, 1, 4, r->fp) == 4 ? 0 : -1;
}
static int rec_fix_boxes(rec_fix *r, uint32_t begin, uint32_t end, int track, unsigned depth)
{
    if (depth > 5) return 0;
    for (uint32_t pos = begin; pos < end;) {
        uint8_t h[8];
        if (end - pos < 8) return 0;
        if (rec_fix_read(r, pos, h, 8)) return -1;
        uint32_t n = rec_mp4_be32(h);
        /* 本机 mdat 是最后一盒。断电可能发生在改 size 与 truncate 之间；
         * 以文件实际长度为上界，后续只保留索引能证明存在的样本。 */
        if (!depth && rec_mp4_box_is(h + 4, "mdat")) {
            if (r->mdat || n < 8) return 0;
            r->mdat = pos;
            r->data_end = r->size;
            return 1;
        }
        if (n < 8 || n > end - pos) return 0;
        rec_fix_track *t = track >= 0 ? &r->t[track] : NULL;
        if (rec_mp4_box_is(h + 4, "mdat")) {
            if (r->mdat) return 0;
            r->mdat = pos;
            r->data_end = pos + n;
        } else if (rec_mp4_box_is(h + 4, "trak")) {
            if (r->tracks >= 2) return 0;
            int ret = rec_fix_boxes(r, pos + 8, pos + n, r->tracks++, depth + 1);
            if (ret != 1) return ret;
        } else if (rec_mp4_box_is(h + 4, "moov") || rec_mp4_box_is(h + 4, "mdia") ||
                   rec_mp4_box_is(h + 4, "minf") || rec_mp4_box_is(h + 4, "stbl")) {
            int ret = rec_fix_boxes(r, pos + 8, pos + n, track, depth + 1);
            if (ret != 1) return ret;
        } else if (rec_mp4_box_is(h + 4, "mvhd") || (t && rec_mp4_box_is(h + 4, "mdhd"))) {
            uint32_t version, scale;
            if (n < 28) return 0;
            if (rec_fix_u32(r, pos + 8, &version) || rec_fix_u32(r, pos + 20, &scale)) return -1;
            if (version >> 24 || !scale) return 0;
            if (t) { t->mdhd = pos + 24; t->scale = scale; }
            else { r->mvhd = pos + 24; r->movie_scale = scale; }
        } else if (t && rec_mp4_box_is(h + 4, "tkhd")) {
            uint32_t version;
            if (n < 36) return 0;
            if (rec_fix_u32(r, pos + 8, &version)) return -1;
            if (version >> 24) return 0;
            t->tkhd = pos + 28;
        } else if (t && rec_mp4_box_is(h + 4, "stsc")) {
            uint32_t count, first, per, index;
            if (n != 28) return 0;
            if (rec_fix_u32(r, pos + 12, &count) || rec_fix_u32(r, pos + 16, &first) ||
                rec_fix_u32(r, pos + 20, &per) || rec_fix_u32(r, pos + 24, &index)) return -1;
            /* 仅支持本机的一块一帧映射；不能把未知布局当作同一种索引修复。 */
            if (count != 1 || first != 1 || per != 1 || index != 1) return 0;
            t->stsc = pos;
        } else if (t && rec_mp4_box_is(h + 4, "stsz")) {
            uint32_t fixed;
            if (n < 20) return 0;
            if (rec_fix_u32(r, pos + 12, &fixed) || rec_fix_u32(r, pos + 16, &t->sizes)) return -1;
            if (fixed || t->sizes > (n - 20) / 4) return 0;
            t->stsz = pos + 16;
        } else if (t && (rec_mp4_box_is(h + 4, "stco") || rec_mp4_box_is(h + 4, "stts") ||
                         rec_mp4_box_is(h + 4, "stss"))) {
            uint32_t count;
            if (n < 16) return 0;
            if (rec_fix_u32(r, pos + 12, &count)) return -1;
            uint32_t width = rec_mp4_box_is(h + 4, "stts") ? 8 : 4;
            if (count > (n - 16) / width) return 0;
            if (rec_mp4_box_is(h + 4, "stco")) { t->stco = pos + 12; t->offsets = count; }
            else if (rec_mp4_box_is(h + 4, "stts")) { t->stts = pos + 12; t->times = count; }
            else { t->stss = pos + 12; t->keys = count; }
        }
        pos += n;
    }
    return 1;
}

static int rec_fix_index(const char *path)
{
    rec_fix r;
    os_memset(&r, 0, sizeof(r));
    r.fp = osal_fopen(path, "r+");
    if (!r.fp) return -1;
    r.size = osal_fsize(r.fp);
    int ret = rec_fix_boxes(&r, 0, r.size, -1, 0);
    uint32_t last_end = 0, movie_duration = 0;
    uint8_t *allocated = NULL;
    if (ret != 1) goto done;
    if (!r.mdat || !r.mvhd || r.tracks != 2) { ret = 0; goto done; }
    /* 第一遍只读：两条轨道都验证通过后才允许修改。 */
    for (unsigned k = 0; k < r.tracks; ++k) {
        rec_fix_track *t = &r.t[k];
        if (!t->stsz || !t->stco || !t->stts || !t->mdhd || !t->tkhd || !t->stsc) { ret = 0; goto done; }
        uint32_t want = t->sizes < t->offsets ? t->sizes : t->offsets;
        if (!want || want > 10000 || t->times > 10000) { ret = 0; goto done; }
        /* 索引连续读入动态 PSRAM，避免 stco/stsz 每帧交替寻道拖慢正在录像的线程。 */
        if (allocated) { _os_free_psram(allocated); allocated = NULL; }
        allocated = _os_malloc_psram(want * 8U + 126U);
        if (!allocated) { ret = -1; goto done; }
        uint8_t *offsets = (uint8_t *)(((uint32_t)allocated + 63U) & ~63U);
        uint8_t *sizes = offsets + ((want * 4U + 63U) & ~63U);
        if (rec_fix_read(&r, t->stco + 4, offsets, want * 4U) ||
            rec_fix_read(&r, t->stsz + 4, sizes, want * 4U)) { ret = -1; goto done; }
        uint32_t prev_end = r.mdat + 8;
        for (uint32_t i = 0; i < want; ++i) {
            uint32_t off = rec_mp4_be32(offsets + i * 4);
            uint32_t len = rec_mp4_be32(sizes + i * 4);
            if (!len || off < prev_end || off > r.data_end || len > r.data_end - off) break;
            prev_end = off + len;
            ++t->keep;
            if ((i & 63U) == 0) os_sleep_ms(1);
        }
        uint32_t remaining = t->keep;
        uint64_t duration = 0;
        for (uint32_t i = 0; i < t->times && remaining; ++i) {
            uint32_t samples, delta;
            if (rec_fix_u32(&r, t->stts + 4 + i * 8, &samples) ||
                rec_fix_u32(&r, t->stts + 8 + i * 8, &delta)) { ret = -1; goto done; }
            if (!samples || !delta) break;
            if (samples > remaining) samples = remaining;
            remaining -= samples;
            duration += (uint64_t)samples * delta;
            ++t->time_entries;
        }
        t->keep -= remaining;
        if (!t->keep || duration > 0xffffffffULL) { ret = 0; goto done; }
        t->duration = (uint32_t)duration;
        uint32_t off, len;
        if (rec_fix_u32(&r, t->stco + t->keep * 4, &off) ||
            rec_fix_u32(&r, t->stsz + t->keep * 4, &len)) { ret = -1; goto done; }
        if (off + len > last_end) last_end = off + len;
        uint32_t prev_key = 0, keys = 0;
        for (uint32_t i = 0; i < t->keys; ++i) {
            uint32_t key;
            if (rec_fix_u32(&r, t->stss + 4 + i * 4, &key)) { ret = -1; goto done; }
            if (!key || key <= prev_key) break;  /* 尾部未完整提交时保留前面的有效关键帧。 */
            if (key > t->keep) break;
            if (i == 0 && key != 1) { ret = 0; goto done; }
            prev_key = key;
            ++keys;
        }
        t->keys = keys;
        if (k == 0 && (!t->stss || !keys)) { ret = 0; goto done; }
        uint64_t md = duration * r.movie_scale / t->scale;
        if (md > 0xffffffffULL) { ret = 0; goto done; }
        if (md > movie_duration) movie_duration = (uint32_t)md;
    }
    /* 第二遍提交一致的样本计数和时长。任何写失败仍保留 .REC，下次重新检查。 */
    for (unsigned k = 0; k < r.tracks; ++k) {
        rec_fix_track *t = &r.t[k];
        uint32_t remaining = t->keep;
        for (uint32_t i = 0; i < t->time_entries; ++i) {
            uint32_t n;
            if (rec_fix_u32(&r, t->stts + 4 + i * 8, &n)) { ret = -1; goto done; }
            if (n > remaining) n = remaining;
            if (rec_fix_write(&r, t->stts + 4 + i * 8, n)) { ret = -1; goto done; }
            remaining -= n;
        }
        if (rec_fix_write(&r, t->stts, t->time_entries) || rec_fix_write(&r, t->stsz, t->keep) ||
            rec_fix_write(&r, t->stco, t->keep) || (t->stss && rec_fix_write(&r, t->stss, t->keys)) ||
            rec_fix_write(&r, t->mdhd, t->duration) ||
            rec_fix_write(&r, t->tkhd, (uint64_t)t->duration * r.movie_scale / t->scale)) {
            ret = -1; goto done;
        }
    }
    if (rec_fix_write(&r, r.mvhd, movie_duration) ||
        rec_fix_write(&r, r.mdat, last_end - r.mdat) ||
        osal_fseek(r.fp, last_end) != FR_OK || osal_ftruncate(r.fp) != FR_OK ||
        osal_fsync(r.fp) != FR_OK) ret = -1;
done:
    if (allocated) _os_free_psram(allocated);
    if (osal_fclose(r.fp) != FR_OK) ret = -1;
    return ret;
}

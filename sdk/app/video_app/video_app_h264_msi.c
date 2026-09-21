#include "basic_include.h"

#include "lib/multimedia/msi.h"

#include "stream_define.h"
#include "lib/video/h264/h264_drv.h"
#include "osal/work.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "hal/h264.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "gen420_hardware_msi.h"
#include "user_work/user_work.h"
#include "hal/scale.h"
#include "lib/video/vpp/vpp_dev.h"
#include "hal/isp.h"

extern uint32  get_h264_srcID(void *d);
extern uint8_t get_vpp_w_h(uint16_t *w, uint16_t *h);
extern uint8_t get_vpp1_w_h(uint16_t *w, uint16_t *h);
extern uint32  get_h264_w_h(void *d, uint16_t *w, uint16_t *h);
extern uint8_t get_vpp_scale_w_h(uint16_t *w, uint16_t *h);
extern void    set_vpp_scale_w_h(uint8_t en, uint16_t w, uint16_t h);
#ifndef SAVE_COUNT
#define SAVE_COUNT 4
#endif

#define MAX_BYTES 0

#if VIDEO_YUV_RANGE_TYPE

#undef MAX_BYTES
#define MAX_BITS  160
#define MAX_BYTES (MAX_BITS / 8)

// 位缓冲区结构体
typedef struct
{
    uint8_t data[MAX_BYTES]; // 存储数据的字节数组
    int     current_bit_pos; // 当前位位置（0-127）
} BitBuffer;

// 初始化位缓冲区
void bit_buffer_init(BitBuffer *buffer)
{
    // memset(buffer->data, 0, sizeof(buffer->data));
    buffer->current_bit_pos = 0;
}

// 设置位缓冲区
void bit_buffer_set(BitBuffer *buffer, uint8_t data)
{
    buffer->data[buffer->current_bit_pos / 8] = data;
    buffer->current_bit_pos += 8;
}

// 二进制bit左移拼接函数
// 输入：buffer - 位缓冲区指针
//       bits - 要添加的二进制数据（最多16位）
//       bit_count - 二进制位数（1-16）
//       padding - 是否补零到8的倍数（1:是，0:否）
// 返回值：成功添加的位数
int bit_buffer_append(BitBuffer *buffer, uint16_t bits, int bit_count, int padding)
{
    if (bit_count < 1 || bit_count > 16)
    {
        os_printf(KERN_ERR "err:bit_count must 1-16\n");
        return 0;
    }

    if (buffer->current_bit_pos + bit_count > MAX_BITS)
    {
        printf(KERN_ERR "warning:buff full\n");
        bit_count = MAX_BITS - buffer->current_bit_pos;
        if (bit_count <= 0)
        {
            return 0;
        }
    }

    // 确保只取bit_count位
    bits &= (1 << bit_count) - 1;

    // 逐个bit添加
    for (int i = bit_count - 1; i >= 0; i--)
    {
        int current_byte        = buffer->current_bit_pos / 8;
        int current_bit_in_byte = 7 - (buffer->current_bit_pos % 8); // 从高位到低位

        // 获取当前bit的值
        uint8_t bit_value = (bits >> i) & 1;

        // 设置对应的bit位
        if (bit_value)
        {
            buffer->data[current_byte] |= (1 << current_bit_in_byte);
        }
        else
        {
            buffer->data[current_byte] &= ~(1 << current_bit_in_byte);
        }

        buffer->current_bit_pos++;
    }

    // 如果需要补零到8的倍数
    if (padding)
    {
        int remainder = buffer->current_bit_pos % 8;
        if (remainder != 0)
        {
            int bits_to_pad = 8 - remainder;

            if (buffer->current_bit_pos + bits_to_pad > MAX_BITS)
            {
                // printf("warning:buff full when fill zero\n");
                bits_to_pad = MAX_BITS - buffer->current_bit_pos;
            }

            for (int i = 0; i < bits_to_pad; i++)
            {
                int current_byte        = buffer->current_bit_pos / 8;
                int current_bit_in_byte = 7 - (buffer->current_bit_pos % 8);

                // 补零
                buffer->data[current_byte] &= ~(1 << current_bit_in_byte);
                buffer->current_bit_pos++;
            }

            // printf("fill zero %d bit,make byte align\n", bits_to_pad);
        }
    }

    return bit_count;
}

// 打印缓冲区内容（十六进制格式）
void bit_buffer_print_hex(const BitBuffer *buffer)
{
    int bytes_used = (buffer->current_bit_pos + 7) / 8;
    for (int i = 0; i < bytes_used; i++)
    {
        os_printf("buff[%02d]:%02x\n", i, buffer->data[i]);
    }
}

// 获取当前已使用的字节数
int bit_buffer_get_used_bytes(const BitBuffer *buffer)
{
    return (buffer->current_bit_pos + 7) / 8;
}

// 指数哥伦布编码
uint8_t len_exp = 0;
uint8_t len_pre = 0;
uint8_t len_suf = 0;
uint16  exp_out = 0;
void    ue_se_enc(uint16 in_data)
{
    len_exp = 0;
    len_pre = 0;
    len_suf = 0;
    exp_out = 0;

    if ((in_data & 0x1e0) != 0)
    {
        if ((in_data & 0x100) != 0)
        {
            len_exp = 14;
            len_pre = 7;
            len_suf = 8;
        }
        else if ((in_data & 0x080) != 0)
        {
            len_exp = 14;
            len_pre = 6;
            len_suf = 7;
        }
        else if ((in_data & 0x040) != 0)
        {
            len_exp = 12;
            len_pre = 5;
            len_suf = 6;
        }
        else
        {
            len_exp = 10;
            len_pre = 4;
            len_suf = 5;
        }
    }
    else
    {
        if ((in_data & 0x010) != 0)
        {
            len_exp = 8;
            len_pre = 3;
            len_suf = 4;
        }
        else if ((in_data & 0x008) != 0)
        {
            len_exp = 6;
            len_pre = 2;
            len_suf = 3;
        }
        else if ((in_data & 0x004) != 0)
        {
            len_exp = 4;
            len_pre = 1;
            len_suf = 2;
        }
        else if ((in_data & 0x002) != 0)
        {
            len_exp = 2;
            len_pre = 0;
            len_suf = 1;
        }
        else
        {
            len_exp = 0;
            len_pre = 0;
            len_suf = 1;
        }
    }
    len_exp += 1;
    len_pre += 1;
    len_suf += 1;
    exp_out = (in_data & 0x1ff);
}

static void h264_sps_append_ue(BitBuffer *buffer, uint16_t value)
{
    uint32_t code_num = (uint32_t)value + 1;
    uint32_t tmp      = code_num;
    uint8_t  suffix_bits = 0;

    while (tmp > 1)
    {
        tmp >>= 1;
        suffix_bits++;
    }
    if (suffix_bits)
    {
        bit_buffer_append(buffer, 0, suffix_bits, 0);
    }
    bit_buffer_append(buffer, (uint16_t)code_num, suffix_bits + 1, 0);
}

// main  1920 1080
void h264_sps_gen_test(BitBuffer *buffer, uint16_t wrap_w, uint16_t wrap_h, uint8_t fr)
{
    uint8_t level_idc  = 52; // h264 0x50
    uint16  img_x      = (wrap_w + 0xf) / 16;
    uint16  img_y      = (wrap_h + 0xf) / 16;
    uint8_t crop_en    = 0;
    uint16  crop_x     = (img_x * 16 - wrap_w) / 2;
    uint16  crop_y     = (img_y * 16 - wrap_h) / 2;
    uint8_t full_range = fr;

    if (crop_x || crop_y)
    {
        crop_en = 1;
    }

    bit_buffer_init(buffer);
    // fixed
    bit_buffer_set(buffer, 0x00);
    bit_buffer_set(buffer, 0x00);
    bit_buffer_set(buffer, 0x00);
    bit_buffer_set(buffer, 0x01);
    bit_buffer_set(buffer, 0x67);
    bit_buffer_set(buffer, 0x4d);
    bit_buffer_set(buffer, 0x00);
    bit_buffer_set(buffer, level_idc);
    bit_buffer_set(buffer, 0x96);
    bit_buffer_set(buffer, 0x54);
    // img width
    ue_se_enc(img_x);
    bit_buffer_append(buffer, 0x0000, len_pre, 0);
    bit_buffer_append(buffer, exp_out, len_suf, 0);
    // img height
    ue_se_enc(img_y);
    bit_buffer_append(buffer, 0x0000, len_pre, 0);
    bit_buffer_append(buffer, exp_out, len_suf, 0);
    // other
    bit_buffer_append(buffer, 0x0003, 2, 0);
    // img crop
    if (crop_en)
    {
        bit_buffer_append(buffer, 0x0001, 1, 0);
        h264_sps_append_ue(buffer, 0);
        h264_sps_append_ue(buffer, crop_x);
        h264_sps_append_ue(buffer, 0);
        h264_sps_append_ue(buffer, crop_y);
    }
    else
    {
        bit_buffer_append(buffer, 0x0000, 1, 0);
    }
    // full range
    if (full_range)
    {
        bit_buffer_append(buffer, 0x4880, 15, 0);
    }
    else
    {
        bit_buffer_append(buffer, 0x0000, 1, 0);
    }
    // byte align
    bit_buffer_append(buffer, 0x0001, 1, 1);
    // bit_buffer_print_hex(buffer);
}
#endif
enum video_app_h264_enum
{
    VIDEO_APP_H264_START = BIT(0),
};

// data申请空间函数
#define STREAM_MALLOC av_psram_malloc
#define STREAM_FREE   av_psram_free
#define STREAM_ZALLOC av_psram_zalloc

/*******************************************************************************
 * H264 输出 buffer 静态池
 * 目的：消除每帧 malloc/free 导致的 av_psram 碎片化
 * 设计：预分配 N 块固定大小 PSRAM buffer，每帧从池取一块，下游消费完归还
 *       如果池满或单帧超过单块大小，回退到 STREAM_MALLOC（极少触发）
 *
 ******************************************************************************/
/* ----- 三层静态池设计 -----
 * 目的: 消除 av_psram_heap 上的 h264 帧 alloc/free, 根治碎片化.
 *
 * P 帧 size 实测分布:
 *   826 720P (~7200 帧):  <8K=91.9%  8-16K=7.9%  16-24K=0.14%  >=24K=0.08%
 *                         I 帧 max ~38KB
 *   828 1080P (~2800 帧): <8K=79.6%  8-16K=18.9%  16-24K=2.1%  >=24K=0.08%
 *                         I 帧 max ~42KB
 *
 * 分 3 层避免用大块装小帧浪费, 同时小池块数足以容纳 mp4 fbq 堆积:
 *   SMALL: 覆盖绝大多数小 P 帧 (实际命中率 ~90%)
 *   MID:   覆盖中等 P 帧 + 小池满时的应急
 *   BIG:   覆盖 I 帧 + 大 P 帧 + 中池满时的应急
 */
#if defined(__TXW828__)
/* 828: 1080P 主码流 */
#define H264_SMALL_BUF_SIZE     (8  * 1024)
#define H264_SMALL_BUF_COUNT    23
#define H264_MID_BUF_SIZE       (16 * 1024)
#define H264_MID_BUF_COUNT      12
#define H264_BIG_BUF_SIZE       (80 * 1024)
#define H264_BIG_BUF_COUNT      2
#else
/* 826: 720P 主码流, 实测 91.9% P 帧 < 8K */
#define H264_SMALL_BUF_SIZE     (8  * 1024)
#define H264_SMALL_BUF_COUNT    23
#define H264_MID_BUF_SIZE       (16 * 1024)
#define H264_MID_BUF_COUNT      6
#define H264_BIG_BUF_SIZE       (60 * 1024)
#define H264_BIG_BUF_COUNT      2
#endif

static uint8_t          *h264_small_buf[H264_SMALL_BUF_COUNT] = {NULL};
static volatile uint8_t  h264_small_buf_busy[H264_SMALL_BUF_COUNT] = {0};
static uint8_t          *h264_mid_buf[H264_MID_BUF_COUNT] = {NULL};
static volatile uint8_t  h264_mid_buf_busy[H264_MID_BUF_COUNT] = {0};
static uint8_t          *h264_big_buf[H264_BIG_BUF_COUNT] = {NULL};
static volatile uint8_t  h264_big_buf_busy[H264_BIG_BUF_COUNT] = {0};
static volatile uint8_t  h264_static_buf_inited = 0;

static int h264_static_buf_init(void)
{
    if (h264_static_buf_inited)
        return 0;
    for (int i = 0; i < H264_SMALL_BUF_COUNT; i++)
    {
        h264_small_buf[i] = (uint8_t *) STREAM_MALLOC(H264_SMALL_BUF_SIZE);
        if (!h264_small_buf[i])
        {
            os_printf(KERN_ERR "h264_small_buf: alloc failed at %d\r\n", i);
            for (int j = 0; j < i; j++) { STREAM_FREE(h264_small_buf[j]); h264_small_buf[j] = NULL; }
            return -1;
        }
        sys_dcache_invalid_range((uint32_t *) h264_small_buf[i], H264_SMALL_BUF_SIZE);
    }
    for (int i = 0; i < H264_MID_BUF_COUNT; i++)
    {
        h264_mid_buf[i] = (uint8_t *) STREAM_MALLOC(H264_MID_BUF_SIZE);
        if (!h264_mid_buf[i])
        {
            os_printf(KERN_ERR "h264_mid_buf: alloc failed at %d\r\n", i);
            for (int j = 0; j < i; j++) { STREAM_FREE(h264_mid_buf[j]); h264_mid_buf[j] = NULL; }
            for (int j = 0; j < H264_SMALL_BUF_COUNT; j++) { STREAM_FREE(h264_small_buf[j]); h264_small_buf[j] = NULL; }
            return -1;
        }
        sys_dcache_invalid_range((uint32_t *) h264_mid_buf[i], H264_MID_BUF_SIZE);
    }
    for (int i = 0; i < H264_BIG_BUF_COUNT; i++)
    {
        h264_big_buf[i] = (uint8_t *) STREAM_MALLOC(H264_BIG_BUF_SIZE);
        if (!h264_big_buf[i])
        {
            os_printf(KERN_ERR "h264_big_buf: alloc failed at %d\r\n", i);
            for (int j = 0; j < i; j++) { STREAM_FREE(h264_big_buf[j]); h264_big_buf[j] = NULL; }
            for (int j = 0; j < H264_MID_BUF_COUNT; j++) { STREAM_FREE(h264_mid_buf[j]); h264_mid_buf[j] = NULL; }
            for (int j = 0; j < H264_SMALL_BUF_COUNT; j++) { STREAM_FREE(h264_small_buf[j]); h264_small_buf[j] = NULL; }
            return -1;
        }
        sys_dcache_invalid_range((uint32_t *) h264_big_buf[i], H264_BIG_BUF_SIZE);
    }
    h264_static_buf_inited = 1;
    os_printf(KERN_INFO "h264_static_buf: init ok, small=%dx%d mid=%dx%d big=%dx%d (total %dKB)\r\n",
              H264_SMALL_BUF_COUNT, H264_SMALL_BUF_SIZE,
              H264_MID_BUF_COUNT,   H264_MID_BUF_SIZE,
              H264_BIG_BUF_COUNT,   H264_BIG_BUF_SIZE,
              (H264_SMALL_BUF_COUNT * H264_SMALL_BUF_SIZE +
               H264_MID_BUF_COUNT   * H264_MID_BUF_SIZE +
               H264_BIG_BUF_COUNT   * H264_BIG_BUF_SIZE) / 1024);
    return 0;
}

/* 三层池 alloc, 按 size 选合适层, 失败逐层向上应急:
 *   size <= SMALL : 先小池 → 失败再中池 → 再大池
 *   SMALL < size <= MID : 中池 → 失败再大池
 *   MID < size <= BIG : 大池
 *   size > BIG : NULL (caller fallback STREAM_MALLOC)
 * 返回池内 buffer 指针, 或 NULL */
static uint8_t *h264_static_buf_alloc(uint32_t size)
{
    if (!h264_static_buf_inited)
    {
        if (h264_static_buf_init() != 0)
            return NULL;
    }
    if (size > H264_BIG_BUF_SIZE) {
        os_printf(KERN_INFO "h264 frame size %u > big buf %d, fallback\r\n",
                  size, H264_BIG_BUF_SIZE);
        return NULL;
    }

    uint32_t ie = disable_irq();
    /* 1) 小池 (size 合适时优先, 不浪费大块) */
    if (size <= H264_SMALL_BUF_SIZE) {
        for (int i = 0; i < H264_SMALL_BUF_COUNT; i++) {
            if (!h264_small_buf_busy[i]) {
                h264_small_buf_busy[i] = 1;
                enable_irq(ie);
                return h264_small_buf[i];
            }
        }
    }
    /* 2) 中池 (8-16K 帧 + 小池满应急) */
    if (size <= H264_MID_BUF_SIZE) {
        for (int i = 0; i < H264_MID_BUF_COUNT; i++) {
            if (!h264_mid_buf_busy[i]) {
                h264_mid_buf_busy[i] = 1;
                enable_irq(ie);
                return h264_mid_buf[i];
            }
        }
    }
    /* 3) 大池 (I 帧 + 大 P 帧 + 中池满应急) */
    for (int i = 0; i < H264_BIG_BUF_COUNT; i++) {
        if (!h264_big_buf_busy[i]) {
            h264_big_buf_busy[i] = 1;
            enable_irq(ie);
            return h264_big_buf[i];
        }
    }
    enable_irq(ie);

    /* 三层都满, 节流报告 miss. 持续 miss 说明池上限设置不够, 需调大. */
    static uint32_t s_miss_cnt  = 0;
    static uint32_t s_miss_last = 0;
    uint32_t now = os_jiffies_to_msecs(os_jiffies());
    s_miss_cnt++;
    if ((uint32_t)(now - s_miss_last) >= 1000) {
        s_miss_last = now;
        os_printf(KERN_INFO "[h264-pool] miss=%u/s size=%u (all 3 pools exhausted)\r\n",
                  (unsigned)s_miss_cnt, (unsigned)size);
        s_miss_cnt = 0;
    }
    return NULL;
}

/* 尝试归还到静态池 (小/中/大池), 返回 1 表示已归还, 0 表示不在池内 (caller 走 STREAM_FREE) */
static int h264_static_buf_free(void *ptr)
{
    if (!h264_static_buf_inited || !ptr)
        return 0;
    for (int i = 0; i < H264_SMALL_BUF_COUNT; i++) {
        if (h264_small_buf[i] == ptr) {
            h264_small_buf_busy[i] = 0;
            return 1;
        }
    }
    for (int i = 0; i < H264_MID_BUF_COUNT; i++) {
        if (h264_mid_buf[i] == ptr) {
            h264_mid_buf_busy[i] = 0;
            return 1;
        }
    }
    for (int i = 0; i < H264_BIG_BUF_COUNT; i++) {
        if (h264_big_buf[i] == ptr) {
            h264_big_buf_busy[i] = 0;
            return 1;
        }
    }
    return 0;
}

// 结构体申请空间函数
#ifdef MORE_SRAM
#define STREAM_LIBC_MALLOC av_psram_malloc
#define STREAM_LIBC_FREE   av_psram_free
#define STREAM_LIBC_ZALLOC av_psram_zalloc
#else
#define STREAM_LIBC_MALLOC av_malloc
#define STREAM_LIBC_FREE   av_free
#define STREAM_LIBC_ZALLOC av_zalloc
#endif

#define MAX_VIDEO_APP_264 64

// 获取nal的size,从0开始搜索,返回的是的nal头的size,offset相对于头的偏移(通过多次调用,可以用于计算nal_size)
// 返回0代表搜索不到nal的头
static uint8_t get_nal_size(uint8_t *buf, uint32_t size, uint32_t *offset)
{
    uint32_t pos = 0;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
        {
            *offset = pos;
            return 3;
        }

        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
        {
            *offset = pos;
            return 4;
        }

        pos++;
    }
    return 0;
}

// 只是获取pps和sps的nalsize
static uint8_t *get_sps_pps_nal_size(uint8_t *buf, uint32_t size, uint32_t *nal_size, uint8_t *head_size)
{
    uint32_t offset;
    uint8_t  nal_head_size = get_nal_size(buf, size, &offset);
    uint8_t  nal_type;
    uint8_t *ret_buf = NULL;
    // 找到头部,检查类型
    if (nal_head_size && offset + nal_head_size < size)
    {
        nal_type = buf[nal_head_size + offset] & 0x1f;

        // 找到sps和pps就返回长度和偏移(相对buf的偏移)
        if (nal_type == 7 || nal_type == 8)
        {
            // 查找下一个nal
            nal_head_size = get_nal_size(buf + offset + nal_head_size, size - (offset + nal_head_size), nal_size);
            // os_printf("nal_head_size:%d\tnal_size:%d\n",nal_head_size,*nal_size);
            if (nal_head_size)
            {
                // 偏移到nal的头部

                ret_buf    = buf + offset;
                // 返回nal的头size
                *head_size = nal_head_size;
            }
        }
    }

    return ret_buf;
}

uint8_t *skip_sps(uint8_t *next_nal_buf, uint32_t size, uint32_t *skip_size)
{
    uint8_t  sps_times = 0;
    uint8_t  nal_head_size;
    uint32_t nal_size;
    uint8_t  pps_sps_size = 0;
    uint8_t *sps_pps_buf  = NULL;
    while (sps_times < 1)
    {
        sps_pps_buf = get_sps_pps_nal_size(next_nal_buf, 64, &nal_size, &nal_head_size);
        if (sps_pps_buf && (sps_pps_buf[nal_head_size] & 0x1f) == 7)
        {
            pps_sps_size += (nal_size + nal_head_size);
            next_nal_buf = sps_pps_buf + nal_size + nal_head_size;
        }
        // 不匹配,就不再去获取pps或者sps了
        else
        {
            break;
        }
        // os_printf("sps_pps_buf[nal_head_size]& 0x1f):%d\n",sps_pps_buf[nal_head_size]& 0x1f);
        sps_times++;
    }
    if (skip_size)
    {
        *skip_size = pps_sps_size;
    }

    return next_nal_buf;
}

struct video_h264_msi_s
{
    struct os_work      work;
    struct msi         *msi;
    struct h264_device *h264_dev;
    uint8_t             drv1_from : 3, drv2_from : 3, sub_stream_en : 1;
    struct fbpool       tx_pool;
};

#if VIDEO_YUV_RANGE_TYPE
uint8_t h264_dec_sps_src_param(uint32 w, uint32 h, uint8_t *buf)
{
    BitBuffer buffer;
    // uint32    itk;
    buffer.current_bit_pos = 0;
    h264_sps_gen_test(&buffer, w, h, 0);
    memcpy(buf, buffer.data, buffer.current_bit_pos / 8);
    return buffer.current_bit_pos / 8;
}
#endif
/********************************************************************************************
 * I帧和P帧对应的位置图,主要通过寻找nal,定位pps和sps的起始位置并且记录
 * 实际内容也会记录,到终端可以采取是否直接读取结构体的pps和sps(尽量从结构体读取)
 *
 *
 * 00 00 00 01 67 xx xx xx 00 00 00 01 68 xx xx xx 00 00 00 01 65 xx xx xx  I帧
 *             sps_buf                 pps_buf                 start_len
 *
 *
 *
 *
 *
 * 00 00 00 01 61 xx xx xx  P帧
 *             start_len
 *******************************************************************************************/

static int8_t h264_output_msi(struct list_head *get_f, struct video_h264_msi_s *video_h264)
{
    int      ret = RET_ERR;
    uint32_t node_len;
    uint32_t h264_len;
    uint32_t h264_len_tmp;
    uint32_t cp_len;
    uint32_t cp_offset;
    uint8_t *tmp_buf;
    uint32_t timestamp;
    uint8_t  which;
    uint8_t  srcID;
    uint16_t w, h;
    uint8_t  h264_type_frame;
    node_len                = get_h264_node_len_new((void *) get_f);
    h264_len                = get_h264_len(get_f);
    timestamp               = get_h264_timestamp(get_f);
    h264_type_frame         = get_h264_type(get_f); // 获取是否为I帧还是P帧
    which                   = get_h264_which(get_f);
    srcID                   = get_h264_srcID(get_f);
    uint8_t           count = get_h264_loop_num(get_f);
    struct framebuff *fb    = NULL;
    get_h264_w_h((void *) get_f, &w, &h);

    // 新的sps和pps的buf

    if (!h264_len)
    {
        goto h264_output_msi_end;
    }
#if VIDEO_YUV_RANGE_TYPE == 1

    BitBuffer buffer;
    // os_printf("h264_type_frame:%d\n", h264_type_frame);
    //  如果是I帧,就重新生成一下对应的sps和pps
    if (h264_type_frame == 1)
    {
        buffer.current_bit_pos = 0;
        h264_sps_gen_test(&buffer, w, h, 1);
    }
#endif
    fb = fbpool_get(&video_h264->tx_pool, 0, video_h264->msi);
    if (fb)
    {
        // MAX_BYTES是重新生成的sps和pps的最大空间
        // 先从静态池分配, 避免 av_psram 碎片化; 失败再回退到 STREAM_MALLOC
        uint8_t *h264_buf = NULL;
        uint32_t alloc_size = h264_len + SAVE_COUNT + MAX_BYTES;

//        if(alloc_size > H264_BIG_BUF_SIZE){
//            os_printf(KERN_INFO "video frame size too big %u, drop it\r\n", alloc_size);
//            goto h264_output_msi_end;
//        }
        /* P 帧 size 分布统计 (用于决定 h264_static_buf 块 size 改造).
         * h264_type_frame: 1=I 帧, 其他=P 帧.
         * 每 5 秒打一次, 6 个桶覆盖 8K 步进, I 帧单独计数. */
//        {
//            static uint32_t s_p_hist[6] = {0}; /* <8K, 8-16K, 16-24K, 24-32K, 32-48K, >=48K */
//            static uint32_t s_i_max = 0;
//            static uint32_t s_i_cnt = 0;
//            static uint32_t s_last_ms = 0;
//            if (h264_type_frame == 1) {
//                s_i_cnt++;
//                if (alloc_size > s_i_max) s_i_max = alloc_size;
//            } else {
//                uint32_t b;
//                if      (alloc_size < 8u*1024)  b = 0;
//                else if (alloc_size < 16u*1024) b = 1;
//                else if (alloc_size < 24u*1024) b = 2;
//                else if (alloc_size < 32u*1024) b = 3;
//                else if (alloc_size < 48u*1024) b = 4;
//                else                            b = 5;
//                s_p_hist[b]++;
//            }
//            uint32_t now = os_jiffies_to_msecs(os_jiffies());
//            if ((uint32_t)(now - s_last_ms) >= 5000) {
//                s_last_ms = now;
//                printf("[h264-size] P: <8K=%u 8-16K=%u 16-24K=%u 24-32K=%u 32-48K=%u >=48K=%u | I cnt=%u max=%u\r\n",
//                       (unsigned)s_p_hist[0], (unsigned)s_p_hist[1],
//                       (unsigned)s_p_hist[2], (unsigned)s_p_hist[3],
//                       (unsigned)s_p_hist[4], (unsigned)s_p_hist[5],
//                       (unsigned)s_i_cnt, (unsigned)s_i_max);
//            }
//        }

        // 三层静态池, 内部按 size 自动分发; 三池都满才 fallback STREAM_MALLOC
        h264_buf = h264_static_buf_alloc(alloc_size);
        if (!h264_buf){
            if(os_jiffies() > 5000){
                os_printf(KERN_INFO "fallback STREAM_MALLOC %u\r\n", alloc_size);
                h264_buf = (uint8_t *) STREAM_MALLOC(alloc_size);
            }
        }

        if (!h264_buf)
        {
            msi_delete_fb(NULL, fb);
            goto h264_output_msi_end;
        }
        uint32_t reserve_len = 0;

#if VIDEO_YUV_RANGE_TYPE == 1
        uint32_t skip_size;
        if (h264_type_frame == 1)
        {
            uint8_t *new_sps_pps_buf = get_h264_first_buf(get_f);

            skip_sps(new_sps_pps_buf, 128, &skip_size);
            if (skip_size)
            {
                reserve_len = bit_buffer_get_used_bytes(&buffer) - skip_size;
            }
        }

#endif
        // 拷贝264的数据
        h264_len_tmp = h264_len;
        // 设置预留的长度
        cp_offset    = reserve_len;
        while (h264_buf)
        {
            // 图片节点提取
            if (list_empty(get_f) || h264_len_tmp == 0)
            {
                break;
            }
            if (h264_len_tmp > node_len)
            {
                cp_len = node_len;
            }
            else
            {
                cp_len = h264_len_tmp;
            }
            h264_len_tmp -= cp_len;
            tmp_buf = get_h264_first_buf(get_f);
            hw_memcpy(h264_buf + cp_offset, get_h264_first_buf(get_f), cp_len);
            del_h264_first_node(get_f);
            cp_offset += cp_len;
        }

#if VIDEO_YUV_RANGE_TYPE == 1
        // skip_size代表找到sps,替换新的sps
        if (h264_type_frame == 1 && skip_size)
        {
            os_memcpy(h264_buf, buffer.data, bit_buffer_get_used_bytes(&buffer));
        }
#endif

        // 264末尾加一个序号
        if (SAVE_COUNT > 0)
        {
            os_sprintf((char *) h264_buf + h264_len + reserve_len, "%03d", count & 0xff);
            h264_buf[h264_len + reserve_len + 3] = '#';
        }
        sys_dcache_clean_range((uint32_t *) h264_buf, h264_len + SAVE_COUNT + reserve_len);

        fb->data  = (uint8_t *) h264_buf;
        fb->len   = h264_len + SAVE_COUNT + reserve_len;
        fb->mtype = F_H264;
        fb->stype = which + FSTYPE_H264_VPP_DATA0; // 基于stype某个值
        fb->time  = timestamp;
        fb->srcID = srcID;
        // I帧
        if (h264_type_frame == 1)
        {
            // 先去读取sps和pps的长度
            uint8_t  nal_head_size;
            uint32_t nal_size;
            uint8_t *sps_pps_buf = fb->data;

            uint8_t *pps_buf = NULL;
            uint8_t *sps_buf = NULL;
            uint8_t  pps_len = 0, sps_len = 0;
            uint8_t  pps_sps_times = 0;
            uint8_t *next_nal_buf  = fb->data;

            // 读取sps和pps,仅仅读取两次,没有就退出
            while (sps_pps_buf && pps_sps_times < 2)
            {
                sps_pps_buf = get_sps_pps_nal_size(next_nal_buf, 64, &nal_size, &nal_head_size);
                if (sps_pps_buf && (sps_pps_buf[nal_head_size] & 0x1f) == 7)
                {
                    sps_buf      = sps_pps_buf + nal_head_size;
                    sps_len      = nal_size;
                    next_nal_buf = sps_pps_buf + nal_size + nal_head_size;
                }
                else if (sps_pps_buf && (sps_pps_buf[nal_head_size] & 0x1f) == 8)
                {
                    pps_buf      = sps_pps_buf + nal_head_size;
                    pps_len      = nal_size;
                    next_nal_buf = sps_pps_buf + nal_size + nal_head_size;
                }
                // 不匹配,就不再去获取pps或者sps了
                else
                {
                    break;
                }
                // os_printf("sps_pps_buf[nal_head_size]& 0x1f):%d\n",sps_pps_buf[nal_head_size]& 0x1f);
                pps_sps_times++;
            }

            struct fb_h264_s *priv = (struct fb_h264_s *) STREAM_LIBC_ZALLOC(sizeof(struct fb_h264_s));

            // 寻找一下nal头有多少字节
            uint32_t pos;
            uint8_t  h264_nal_size = get_nal_size(next_nal_buf, 16, &pos);

            priv->pps       = pps_buf;
            priv->pps_len   = pps_len;
            priv->sps       = sps_buf;
            priv->sps_len   = sps_len;
            fb->priv        = (void *) priv;
            priv->type      = 1;
            priv->count     = count;
            priv->start_len = next_nal_buf - fb->data + pos + h264_nal_size;
            priv->w         = w;
            priv->h         = h;
            // os_printf("priv->start_len:%d\ttype:%d\n",priv->start_len,fb->data[priv->start_len]&0x1f);
            // os_printf("pps:%d\tsps:%d\n",pps_buf[0]&0x1f,sps_buf[0]&0x1f);
        }
        else
        {
            struct fb_h264_s *priv = (struct fb_h264_s *) STREAM_LIBC_ZALLOC(sizeof(struct fb_h264_s));
            uint32_t          pos;
            uint8_t           h264_nal_size = get_nal_size(fb->data, 16, &pos);
            priv->type                      = 2;
            fb->priv                        = (void *) priv;
            priv->count                     = count;
            priv->start_len                 = pos + h264_nal_size;
            priv->w                         = w;
            priv->h                         = h;
        }
        //_os_printf("H%d", h264_type_frame);
        // 在msi_output_fb后,不要继续调用其他和msi有关的东西,因为有可能在这个之后,会释放对应的内存
        msi_output_fb(video_h264->msi, fb);
        ret = RET_OK;
    }
h264_output_msi_end:
    return ret;
}

static int32 h264_msi_work(struct os_work *work)
{
    struct video_h264_msi_s *video_h264 = (struct video_h264_msi_s *) work;
    uint32_t                 flags;
h264_msi_work_again:
    flags                   = disable_irq();
    struct list_head *get_f = (void *) get_h264_frame();
    enable_irq(flags);
    if (get_f)
    {
        h264_output_msi(get_f, video_h264);
        del_h264_frame(get_f);
        goto h264_msi_work_again;
    }
    os_run_work_delay(work, 1);
    return 0;
}

static int32_t h264_msi_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t                  ret        = RET_OK;
    struct video_h264_msi_s *video_h264 = (struct video_h264_msi_s *) msi->priv;
    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {

            h264_mem_init(0, 0, 0, 0, 0);
            fbpool_destroy(&video_h264->tx_pool);
            STREAM_LIBC_FREE(video_h264);
        }
        break;

        case MSI_CMD_PRE_DESTROY:
        {
            vppdone_func_unregister(VPP_H264_START);
            os_work_cancle2(&video_h264->work, 1);
            h264_close(video_h264->h264_dev);
            if (video_h264->sub_stream_en)
            {
                unregister_gen420_queue(GEN420_QUEUE_H264);
            }

            uint8_t scale1 = 0;
            // 把检查数据源是否为scale1,如果是scale1,同时把scale1一起关闭了
            if (video_h264->drv1_from == SCALER_DATA)
            {
                scale1 = 1;
            }

            if (video_h264->drv2_from == SCALER_DATA)
            {
                scale1 = 1;
            }
            if (scale1)
            {
                struct scale_device *scale_dev = (struct scale_device *) dev_get(HG_SCALE1_DEVID);
                // 关闭scale1
                scale_close(scale_dev);
            }
        }
        break;

        case MSI_CMD_FREE_FB:
        {
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb->data)
            {
                // 先尝试归还到静态池, 不在池内则正常 free
                if (!h264_static_buf_free(fb->data))
                    STREAM_FREE(fb->data);
                fb->data = NULL;
            }
            if (fb->priv)
            {
                STREAM_LIBC_FREE(fb->priv);
                fb->priv = NULL;
            }
            fbpool_put(&video_h264->tx_pool, fb);
            // 不需要内核去释放fb
            ret = RET_OK + 1;
        }
        break;

        default:
            break;
    }
    return ret;
}

static int32_t vpp_start_h264(uint32_t irq_data)
{
    struct video_h264_msi_s *video_h264 = (struct video_h264_msi_s *) irq_data;
    struct h264_device      *h264_dev   = video_h264->h264_dev;
    
    // 拼接或者已经第二个镜头已经done或者是单镜头,则启动
    if (video_msg.video_type_cur == ISP_VIDEO_1 || video_msg.camera_mode == CAM_SINGLE_MASTER_MODE)
    {
        h264_open(h264_dev);
        h264_set_sw_ready(h264_dev);
        return 1;
    }
    else
    {
        return 0;
    }
}
struct msi *h264_msi_init_with_mode(uint32_t drv1_from, uint16_t drv1_w, uint16_t drv1_h, uint16_t drv2_from, uint16_t drv2_w, uint16_t drv2_h)
{
    int                      ret = 0;
    uint8_t                  isnew;
    struct msi              *msi = msi_new(S_H264, 0, &isnew);
    struct video_h264_msi_s *video_h264;
    uint8_t                  vpp_kick = 0;
    //如果需要自适应识别w和h(仅仅适合vpp data0和vpp data1),则传入参数应该为0
    uint32_t auto_w_h = drv1_w|drv1_h|drv2_w|drv2_h;

    if (msi && isnew)
    {
        video_h264 = (struct video_h264_msi_s *) STREAM_LIBC_ZALLOC(sizeof(struct video_h264_msi_s));
        ASSERT(video_h264);
        msi->priv   = (void *) video_h264;
        msi->enable = 1;

        video_h264->h264_dev = (struct h264_device *) dev_get(HG_H264_DEVID);
        if (video_h264->h264_dev)
        {
            if (drv1_from == GEN420_DATA || drv2_from == GEN420_DATA)
            {
                extern int32_t h264_gen420_kick();
                uint32_t       gen_w = drv1_from == GEN420_DATA ? drv1_w : drv2_w;
                uint32_t       gen_h = drv1_from == GEN420_DATA ? drv1_h : drv2_h;
                ret                  = register_gen420_queue(GEN420_QUEUE_H264, gen_w, gen_h, h264_gen420_kick, NULL, (uint32) NULL);
                if (!ret)
                {
                    video_h264->sub_stream_en = 1;
                }
                else
                {
                    video_h264->sub_stream_en = 0;

                    if (drv1_from == GEN420_DATA)
                    {
                        drv1_from = 0;
                        drv1_w    = 0;
                        drv1_h    = 0;
                    }

                    if (drv2_from == GEN420_DATA)
                    {
                        drv2_from = 0;
                        drv2_w    = 0;
                        drv2_h    = 0;
                    }
                }
            }
            // 如果强行配置两个都是VPP_DATA0或者VPP_DATA1,代表是特殊的模式(双镜头),就使用参数即可
            if ((drv1_from == drv2_from) && (drv1_from == VPP_DATA0 || drv1_from == VPP_DATA1) && auto_w_h)
            {
            }
            else
            {
                // 自己适应w和h
                if (drv1_from == VPP_DATA0)
                {
                    vpp_kick++;
                    get_vpp_w_h(&drv1_w, &drv1_h);
                }
                else if (drv1_from == VPP_DATA1)
                {
                    vpp_kick++;
                    get_vpp1_w_h(&drv1_w, &drv1_h);
                }
                else if (drv1_from == SCALER_DATA)
                {
                    if(!drv1_w|| !drv1_h)
                    {
                        ret = get_vpp_scale_w_h(&drv1_w, &drv1_h);
                        if (!ret)
                        {
                            set_vpp_scale_w_h(1, drv1_w, drv1_h);
                        }
                        else
                        {
                            drv1_from = ~0;
                        }
                    }
                }
            }
            video_h264->drv1_from = drv1_from;
            video_h264->drv2_from = drv2_from;
            h264_set_oe_select(video_h264->h264_dev, 0, 0);
            ret = h264_enc(drv1_from, drv1_w, drv1_h, drv2_from, drv2_w, drv2_h);

            if (ret)
            {
                if (video_h264)
                {
                    STREAM_LIBC_FREE(video_h264);
                }
                msi_destroy(msi);
                msi = NULL;
                goto h264_msi_init_with_mode_end;
            }
            else
            {
                // 注册到vpp,尝试用vpp去启动
                // h264_open(video_h264->h264_dev);
                // h264_set_sw_ready(video_h264->h264_dev);
                if (vpp_kick)
                {
                    vppdone_func_register(VPP_H264_START, vpp_start_h264, (uint32_t) video_h264);
                }
                else
                {
                    h264_open(video_h264->h264_dev);
                    h264_set_sw_ready(video_h264->h264_dev);
                }
                msi->action     = h264_msi_action;
                video_h264->msi = msi;
                fbpool_init(&video_h264->tx_pool, MAX_VIDEO_APP_264);
            }
        }
        // 启动workqueue
        // 创建一个任务去做h264的工作
        // OS_TASK_INIT("video_h264", &video_h264->task, h264_msi_thread, (void*)video_h264, OS_TASK_PRIORITY_ABOVE_NORMAL, NULL, 1024);
        OS_WORK_INIT(&video_h264->work, h264_msi_work, 0);
        os_run_work_delay(&video_h264->work, 1);
    }
h264_msi_init_with_mode_end:
    return msi;
}

struct msi *h264_msi_init_with_timeLapse(uint32_t drv1_from, uint16_t drv1_w, uint16_t drv1_h, uint32_t timer)
{
    int                      ret   = 0;
    uint8_t                  isnew = 0;
    struct msi              *msi   = msi_new(S_H264, 0, &isnew);
    struct video_h264_msi_s *video_h264;
    uint8_t                  vpp_kick = 0;
    if (msi && isnew)
    {
        video_h264 = (struct video_h264_msi_s *) STREAM_LIBC_ZALLOC(sizeof(struct video_h264_msi_s));
        ASSERT(video_h264);
        msi->priv   = (void *) video_h264;
        msi->enable = 1;

        video_h264->h264_dev = (struct h264_device *) dev_get(HG_H264_DEVID);
        if (video_h264->h264_dev)
        {
            // 自己适应w和h
            if (drv1_from == VPP_DATA0)
            {
                vpp_kick++;
                get_vpp_w_h(&drv1_w, &drv1_h);
            }
            else if (drv1_from == VPP_DATA1)
            {
                vpp_kick++;
                get_vpp1_w_h(&drv1_w, &drv1_h);
            }
            else if (drv1_from == SCALER_DATA)
            {
                ret = get_vpp_scale_w_h(&drv1_w, &drv1_h);
                if (!ret)
                {
                    set_vpp_scale_w_h(1, drv1_w, drv1_h);
                }
                else
                {
                    drv1_from = ~0;
                }
            }

            video_h264->drv1_from = drv1_from;
            video_h264->drv2_from = ~0;
            h264_set_oe_select(video_h264->h264_dev, 0, 0);
            ret = h264_enc_with_timeLapse(drv1_from, drv1_w, drv1_h, timer);

            if (ret)
            {
                if (video_h264)
                {
                    STREAM_LIBC_FREE(video_h264);
                }
                msi_destroy(msi);
                msi = NULL;
                goto h264_msi_init_with_mode_end;
            }
            else
            {
                if (vpp_kick)
                {
                    vppdone_func_register(VPP_H264_START, vpp_start_h264, (uint32_t) video_h264);
                }
                else
                {
                    h264_open(video_h264->h264_dev);
                    h264_set_sw_ready(video_h264->h264_dev);
                }
                msi->action     = h264_msi_action;
                video_h264->msi = msi;
                fbpool_init(&video_h264->tx_pool, MAX_VIDEO_APP_264);
            }
        }
        // 启动workqueue
        // 创建一个任务去做h264的工作
        // OS_TASK_INIT("video_h264", &video_h264->task, h264_msi_thread, (void*)video_h264, OS_TASK_PRIORITY_ABOVE_NORMAL, NULL, 1024);
        OS_WORK_INIT(&video_h264->work, h264_msi_work, 0);
        os_run_work_delay(&video_h264->work, 1);
    }
h264_msi_init_with_mode_end:
    return msi;
}

struct msi *h264_msi_init_with_mode_for_264wq(uint32_t drv1_from, uint16_t drv1_w, uint16_t drv1_h)
{
    int                      ret = 0;
    uint8_t                  isnew;
    struct msi              *msi = msi_new(S_H264, 0, &isnew);
    struct video_h264_msi_s *video_h264;

    if (msi && isnew)
    {
        video_h264 = (struct video_h264_msi_s *) STREAM_LIBC_ZALLOC(sizeof(struct video_h264_msi_s));
        ASSERT(video_h264);
        msi->priv   = (void *) video_h264;
        msi->enable = 1;

        video_h264->h264_dev = (struct h264_device *) dev_get(HG_H264_DEVID);
        if (video_h264->h264_dev)
        {
            if (drv1_from == GEN420_DATA)
            {
                extern int32_t h264_gen420_kick();
                uint32_t       gen_w = drv1_w;
                uint32_t       gen_h = drv1_h;
                ret                  = register_gen420_queue(GEN420_QUEUE_H264, gen_w, gen_h, h264_gen420_kick, NULL, (uint32) NULL);
                if (!ret)
                {
                    video_h264->sub_stream_en = 1;
                }
                else
                {
                    video_h264->sub_stream_en = 0;

                    if (drv1_from == GEN420_DATA)
                    {
                        drv1_from = 0;
                        drv1_w    = 0;
                        drv1_h    = 0;
                    }
                }
            }

            // 自己适应w和h
            if (drv1_from == VPP_DATA0)
            {
                get_vpp_w_h(&drv1_w, &drv1_h);
            }
            else if (drv1_from == VPP_DATA1)
            {
                get_vpp1_w_h(&drv1_w, &drv1_h);
            }

            h264_set_oe_select(video_h264->h264_dev, 0, 0);
            ret = h264_enc(drv1_from, drv1_w, drv1_h, -1, 0, 0);

            if (ret)
            {
                if (video_h264)
                {
                    STREAM_LIBC_FREE(video_h264);
                }
                msi_destroy(msi);
                msi = NULL;
                goto h264_msi_init_with_mode_end;
            }
            else
            {
                h264_open(video_h264->h264_dev);
                msi->action     = h264_msi_action;
                video_h264->msi = msi;
                fbpool_init(&video_h264->tx_pool, MAX_VIDEO_APP_264);
            }
        }
        // 启动workqueue
        // 创建一个任务去做h264的工作
        // OS_TASK_INIT("video_h264", &video_h264->task, h264_msi_thread, (void*)video_h264, OS_TASK_PRIORITY_ABOVE_NORMAL, NULL, 1024);
        OS_WORK_INIT(&video_h264->work, h264_msi_work, 0);
        os_run_work_delay(&video_h264->work, 1);
    }
h264_msi_init_with_mode_end:
    return msi;
}


struct msi *h264_msi_init_with_gen420(uint16_t drv1_w, uint16_t drv1_h)
{
    int                      ret   = 0;
    uint8_t                  isnew = 0;
    struct msi              *msi   = msi_new(S_H264, 0, &isnew);
    struct video_h264_msi_s *video_h264;

    if (msi && isnew)
    {
        video_h264 = (struct video_h264_msi_s *) STREAM_LIBC_ZALLOC(sizeof(struct video_h264_msi_s));
        ASSERT(video_h264);
        msi->priv   = (void *) video_h264;
        msi->enable = 1;

        video_h264->h264_dev = (struct h264_device *) dev_get(HG_H264_DEVID);
        if (video_h264->h264_dev)
        {
            video_h264->drv1_from = GEN420_DATA;
            video_h264->drv2_from = ~0;
            h264_set_oe_select(video_h264->h264_dev, 0, 0);
            ret = h264_enc(video_h264->drv1_from, drv1_w, drv1_h, -1, 0, 0);

            if (ret)
            {
                if (video_h264)
                {
                    STREAM_LIBC_FREE(video_h264);
                }
                msi_destroy(msi);
                msi = NULL;
                goto h264_msi_init_gen420_end;
            }
            else
            {
                h264_open(video_h264->h264_dev);
                msi->action     = h264_msi_action;
                video_h264->msi = msi;
                fbpool_init(&video_h264->tx_pool, MAX_VIDEO_APP_264);
            }
        }
        // 启动workqueue
        // 创建一个任务去做h264的工作
        // OS_TASK_INIT("video_h264", &video_h264->task, h264_msi_thread, (void*)video_h264, OS_TASK_PRIORITY_ABOVE_NORMAL, NULL, 1024);
        OS_WORK_INIT(&video_h264->work, h264_msi_work, 0);
        os_run_work_delay(&video_h264->work, 1);
    }
h264_msi_init_gen420_end:
    return msi;
}

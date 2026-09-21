#include "lib/heap/av_psram_heap.h"
#include "zbar_api.h"
#define STREAM_MALLOC av_psram_malloc
#define STREAM_FREE   av_psram_free
#define STREAM_ZALLOC av_psram_zalloc

// zbar_image_t 扫描用的临时缓冲由外部提供,避免 zbar 内部动态分配
void zbar_image_set_qr_buffers (zbar_image_t *img,
                                unsigned char *qr_mask_buf,
                                unsigned qr_mask_size,
                                unsigned *qr_sums_buf,
                                unsigned qr_sums_size);

typedef struct
{
    zbar_image_scanner_t *scanner;
    zbar_image_t         *image;
    uint8_t              *tmp_buf;
    uint32_t              tmp_buf_size;
} zbar_stream_t;

// 创建 zbar 解码器
static zbar_stream_t *zbar_stream_create(void)
{
    zbar_stream_t *st;

    st = (zbar_stream_t *) STREAM_MALLOC(sizeof(zbar_stream_t));
    if (!st)
    {
        return NULL;
    }
    memset(st, 0, sizeof(zbar_stream_t));

    st->scanner = zbar_image_scanner_create();
    if (!st->scanner)
    {
        goto err;
    }

    zbar_image_scanner_set_config(st->scanner, 0, ZBAR_CFG_ENABLE, 1);
    zbar_image_scanner_set_config(st->scanner, ZBAR_QRCODE, ZBAR_CFG_UNCERTAINTY, 1);

    st->image = zbar_image_create();
    if (!st->image)
    {
        goto err;
    }
    zbar_image_set_format(st->image, *(int *) "Y800");

    return st;

err:
    if (st->image)
    {
        zbar_image_destroy(st->image);
    }
    if (st->scanner)
    {
        zbar_image_scanner_destroy(st->scanner);
    }
    STREAM_FREE(st);
    return NULL;
}

// 销毁 zbar 解码器
static void zbar_stream_destroy(zbar_stream_t *st)
{
    if (!st)
    {
        return;
    }
    if (st->image)
    {
        zbar_image_destroy(st->image);
    }
    if (st->scanner)
    {
        zbar_image_scanner_destroy(st->scanner);
    }
    if (st->tmp_buf)
    {
        STREAM_FREE(st->tmp_buf);
    }
    STREAM_FREE(st);
}

// 输入一帧 YUV(Y800) 灰度数据解码,识别到的每个 symbol 都会通过回调返回
void zbar_stream_decode_yuv(const uint8_t *yuv, uint32_t w, uint32_t h, zbar_stream_result_cb cb,void *userdata)
{
    zbar_stream_t       *st;
    const zbar_symbol_t *symbol;
    uint32_t             need = (uint32_t) w * h;

    if (!yuv || !w || !h)
    {
        return;
    }

    st = zbar_stream_create();
    if (!st)
    {
        return;
    }

    uint8_t *buf = (uint8_t *) STREAM_MALLOC(need);
    if (!buf)
    {
        printf("%s malloc failed\n", __FUNCTION__);
        zbar_stream_destroy(st);
        return;
    }

    zbar_image_set_qr_buffers(st->image, buf, need, NULL, 0);
    zbar_image_set_size(st->image, w, h);
    zbar_image_set_data(st->image, yuv, need, NULL);
    zbar_scan_image(st->scanner, st->image);

    for (symbol = zbar_image_first_symbol(st->image); symbol; symbol = zbar_symbol_next(symbol))
    {
        if (cb)
        {
            cb(zbar_symbol_get_type(symbol), zbar_symbol_get_data(symbol),userdata);
        }
    }
    if (buf)
    {
        STREAM_FREE(buf);
    }
    zbar_stream_destroy(st);
}
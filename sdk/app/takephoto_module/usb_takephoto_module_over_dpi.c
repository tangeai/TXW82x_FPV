#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "stream_define.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "user_work/user_work.h"
#include "video_app/file_common_api.h"
#include "lib/heap/av_heap.h"
#include "video_msi.h"

extern struct msi *jpg_decode_msg_msi(const char *name, uint16_t out_w, uint16_t out_h, uint16_t step_w, uint16_t step_h, uint32_t filter);
extern struct msi *jpg_decode_msi(const char *name);
extern void        common_takephoto_over_dpi_init(uint8_t jpg_num);
extern int         takephoto_over_dpi_get_magic(uint32_t *normal_magic, uint32_t *thumb_magic);

#define STREAM_LIBC_ZALLOC av_zalloc
#define STREAM_LIBC_FREE   av_free

#define USB_OVER_DPI_MAX     12
#define USB_OVER_DPI_TIMEOUT 3000

struct usb_over_dpi_pending_s
{
    uint8_t used;
    uint8_t datatag;
    uint8_t normal_done;
    uint8_t thumb_done;
    uint16_t time16;
    char    filename[64];
};

struct usb_over_dpi_takephoto_s
{
    struct os_work                 work;
    struct msi                    *msi;
    uint32_t                       normal_magic;
    uint32_t                       thumb_magic;
    struct msi                    *normal_decode_msg_msi;
    struct msi                    *thumb_decode_msg_msi;
    uint8_t                        take_count;
    uint8_t                        use_watermark;
    struct usb_over_dpi_pending_s  pending[USB_OVER_DPI_MAX];
};

static void usb_over_dpi_cleanup_pending(struct usb_over_dpi_takephoto_s *takephoto);

static uint8_t usb_over_dpi_pending_timeout(struct usb_over_dpi_pending_s *pending)
{
    uint16_t now16;

    if (!pending || !pending->used)
    {
        return 0;
    }

    now16 = (uint16_t) (os_jiffies() & 0xffff);
    return (uint16_t) (now16 - pending->time16) > USB_OVER_DPI_TIMEOUT;
}

static struct usb_over_dpi_pending_s *usb_over_dpi_find_pending(struct usb_over_dpi_takephoto_s *takephoto, uint8_t datatag)
{
    uint8_t i;

    if (!takephoto)
    {
        return NULL;
    }

    for (i = 0; i < USB_OVER_DPI_MAX; i++)
    {
        struct usb_over_dpi_pending_s *pending = &takephoto->pending[i];
        if (usb_over_dpi_pending_timeout(pending))
        {
            pending->used = 0;
        }
        else if (pending->used && pending->datatag == datatag)
        {
            return pending;
        }
    }

    return NULL;
}

static struct usb_over_dpi_pending_s *usb_over_dpi_alloc_pending(struct usb_over_dpi_takephoto_s *takephoto, uint8_t datatag)
{
    uint8_t i;

    if (!takephoto || usb_over_dpi_find_pending(takephoto, datatag))
    {
        return NULL;
    }

    usb_over_dpi_cleanup_pending(takephoto);
    for (i = 0; i < USB_OVER_DPI_MAX; i++)
    {
        struct usb_over_dpi_pending_s *pending = &takephoto->pending[i];
        if (!pending->used)
        {
            os_memset(pending, 0, sizeof(*pending));
            if (takephoto_name_no_dir(pending->filename, sizeof(pending->filename)))
            {
                os_memset(pending, 0, sizeof(*pending));
                return NULL;
            }

            pending->used    = 1;
            pending->datatag = datatag;
            pending->time16  = (uint16_t) (os_jiffies() & 0xffff);
            return pending;
        }
    }

    return NULL;
}

static void usb_over_dpi_cleanup_pending(struct usb_over_dpi_takephoto_s *takephoto)
{
    uint8_t i;

    if (!takephoto)
    {
        return;
    }

    for (i = 0; i < USB_OVER_DPI_MAX; i++)
    {
        struct usb_over_dpi_pending_s *pending = &takephoto->pending[i];
        if (usb_over_dpi_pending_timeout(pending) || (pending->used && pending->normal_done && pending->thumb_done))
        {
            pending->used = 0;
        }
    }
}

static void usb_over_dpi_finish_pending(struct usb_over_dpi_takephoto_s *takephoto, struct usb_over_dpi_pending_s *pending)
{
    if (pending && pending->normal_done && pending->thumb_done)
    {
        pending->used = 0;
        usb_over_dpi_cleanup_pending(takephoto);
    }
}

static uint8_t usb_over_dpi_has_pending(struct usb_over_dpi_takephoto_s *takephoto)
{
    uint8_t i;

    if (!takephoto)
    {
        return 0;
    }

    usb_over_dpi_cleanup_pending(takephoto);
    for (i = 0; i < USB_OVER_DPI_MAX; i++)
    {
        if (takephoto->pending[i].used)
        {
            return 1;
        }
    }

    return 0;
}

static void usb_over_dpi_update_enable(struct usb_over_dpi_takephoto_s *takephoto)
{
    if (takephoto)
    {
        usb_over_dpi_cleanup_pending(takephoto);
    }

    if (takephoto && takephoto->msi && !takephoto->take_count && !usb_over_dpi_has_pending(takephoto))
    {
        takephoto->msi->enable = 0;
        if (takephoto->thumb_decode_msg_msi)
        {
            takephoto->thumb_decode_msg_msi->enable = 0;
        }
        if (takephoto->normal_decode_msg_msi)
        {
            takephoto->normal_decode_msg_msi->enable = 0;
        }
    }
}

static struct framebuff *usb_over_dpi_clone_takephoto_yuv(struct usb_over_dpi_takephoto_s *takephoto, struct framebuff *fb, struct usb_over_dpi_pending_s *pending, uint32_t magic)
{
    struct yuv_arg_s           *src_arg;
    struct takephoto_yuv_arg_s *dst_arg;
    struct framebuff           *send_fb;

    if (!fb || !fb->priv || !pending)
    {
        return NULL;
    }

    dst_arg = (struct takephoto_yuv_arg_s *) STREAM_LIBC_ZALLOC(sizeof(struct takephoto_yuv_arg_s));
    if (!dst_arg)
    {
        return NULL;
    }

    send_fb = fb_clone(fb, F_YUV << 8 | FSTYPE_YUV_TAKEPHOTO, takephoto->msi);
    if (!send_fb)
    {
        STREAM_LIBC_FREE(dst_arg);
        return NULL;
    }

    src_arg = (struct yuv_arg_s *) fb->priv;
    os_memcpy(&dst_arg->yuv_arg, src_arg, sizeof(struct yuv_arg_s));
    dst_arg->yuv_arg.type  = YUV_ARG_TAKEPHOTO;
    dst_arg->yuv_arg.magic = magic;
    os_memcpy(dst_arg->name, pending->filename, os_strlen(pending->filename) + 1);
    send_fb->priv = dst_arg;
    return send_fb;
}

static int32_t usb_over_dpi_takephoto_work(struct os_work *work)
{
    struct usb_over_dpi_takephoto_s *takephoto = (struct usb_over_dpi_takephoto_s *) work;
    struct framebuff                *fb;

    while ((fb = msi_get_fb(takephoto->msi, 0)))
    {
        if (fb->mtype == F_YUV)
        {
            struct usb_over_dpi_pending_s *pending = usb_over_dpi_find_pending(takephoto, fb->datatag);
            if (!pending)
            {
                msi_delete_fb(NULL, fb);
                continue;
            }

            if ((fb->stype == FSTYPE_JPG_GEN420_REJPG || fb->stype == FSTYPE_USB_TAKEPHOTO) && !pending->normal_done)
            {
                struct framebuff *send_fb = usb_over_dpi_clone_takephoto_yuv(takephoto, fb, pending, takephoto->normal_magic);
                if (send_fb)
                {
                    msi_output_fb(takephoto->msi, send_fb);
                }
                pending->normal_done = 1;
            }
            else if (fb->stype == FSTYPE_OVER_DPI_THUMB_JPG && !pending->thumb_done)
            {
                struct framebuff *send_fb = usb_over_dpi_clone_takephoto_yuv(takephoto, fb, pending, takephoto->thumb_magic);
                if (send_fb)
                {
                    msi_output_fb(takephoto->msi, send_fb);
                }
                pending->thumb_done = 1;
            }

            usb_over_dpi_finish_pending(takephoto, pending);
        }

        msi_delete_fb(NULL, fb);
    }

    usb_over_dpi_update_enable(takephoto);
    return 0;
}

static int32_t usb_over_dpi_takephoto_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t                         ret       = RET_OK;
    struct usb_over_dpi_takephoto_s *takephoto = (struct usb_over_dpi_takephoto_s *) msi->priv;
    struct framebuff                *fb;

    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {
            os_work_cancle2(&takephoto->work, 1);
            STREAM_LIBC_FREE(takephoto);
        }
        break;

        case MSI_CMD_PRE_DESTROY:
        {
            os_work_cancle2(&takephoto->work, 1);
        }
        break;

        case MSI_CMD_FREE_FB:
        {
            fb = (struct framebuff *) param1;
            if (fb && fb->mtype == F_YUV && fb->stype == FSTYPE_YUV_TAKEPHOTO && fb->priv)
            {
                STREAM_LIBC_FREE(fb->priv);
                fb->priv = NULL;
            }
        }
        break;

        case MSI_CMD_JPG_THUMB:
        {
            uint32_t cmd_self = param1;
            uint32_t arg      = param2;

            switch (cmd_self)
            {
                case MSI_JPG_THUMB_TAKEPHOTO:
                {
                    takephoto->take_count = arg;
                    msi->enable           = 1;
                    os_run_work(&takephoto->work);
                }
                break;

                case MSI_JPG_THUMB_TAKEPHOTO_SETPATH:
                {
                    msi_output_cmd(msi, MSI_CMD_JPG_THUMB, MSI_JPG_THUMB_TAKEPHOTO_SETPATH, arg);
                }
                break;

                default:
                    break;
            }
        }
        break;

        case MSI_CMD_TRANS_FB:
        {
            fb  = (struct framebuff *) param1;
            ret = RET_ERR;

            if (fb)
            {
                if (fb->mtype == F_YUV)
                {
                    if (takephoto->use_watermark && fb->stype == FSTYPE_JPG_GEN420_REJPG && fb->priv)
                    {
                        struct usb_over_dpi_pending_s *pending;
                        if (takephoto->take_count && (pending = usb_over_dpi_alloc_pending(takephoto, fb->datatag)))
                        {
                            takephoto->take_count--;
                            if (takephoto->thumb_decode_msg_msi)
                            {
                                takephoto->thumb_decode_msg_msi->enable = 1;
                            }
                            else
                            {
                                pending->thumb_done = 1;
                            }
                            ret = RET_OK;
                        }
                    }
                    else if (!takephoto->use_watermark && fb->stype == FSTYPE_USB_TAKEPHOTO && fb->priv)
                    {
                        if (usb_over_dpi_find_pending(takephoto, fb->datatag))
                        {
                            ret = RET_OK;
                        }
                    }
                    else if (fb->stype == FSTYPE_OVER_DPI_THUMB_JPG && fb->priv)
                    {
                        struct yuv_arg_s *arg = (struct yuv_arg_s *) fb->priv;
                        if (arg->magic == takephoto->thumb_magic && usb_over_dpi_find_pending(takephoto, fb->datatag))
                        {
                            ret = RET_OK;
                        }
                    }
                }
                else if (fb->mtype == F_JPG)
                {
                    if (takephoto->use_watermark && fb->stype == FSTYPE_JPG_GEN420_REJPG)
                    {
                        struct usb_over_dpi_pending_s *pending = usb_over_dpi_find_pending(takephoto, fb->datatag);
                        if (pending && !pending->thumb_done && takephoto->thumb_decode_msg_msi)
                        {
                            takephoto->thumb_decode_msg_msi->enable = 1;
                            fb_get(fb);
                            msi_output_fb(msi, fb);
                            ret = RET_OK + 1;
                        }
                    }
                    else if (!takephoto->use_watermark && fb->stype == FSTYPE_USB_CAM0)
                    {
                        struct usb_over_dpi_pending_s *pending;
                        if (takephoto->take_count && takephoto->normal_decode_msg_msi && (pending = usb_over_dpi_alloc_pending(takephoto, fb->datatag)))
                        {
                            takephoto->take_count--;
                            takephoto->normal_decode_msg_msi->enable = 1;
                            if (takephoto->thumb_decode_msg_msi)
                            {
                                takephoto->thumb_decode_msg_msi->enable = 1;
                            }
                            else
                            {
                                pending->thumb_done = 1;
                            }
                            fb_get(fb);
                            msi_output_fb(msi, fb);
                            ret = RET_OK + 1;
                        }
                    }
                    else
                    {
                        usb_over_dpi_update_enable(takephoto);
                    }
                }
            }
        }
        break;

        case MSI_CMD_TRANS_FB_END:
        {
            usb_over_dpi_update_enable(takephoto);
            os_run_work(&takephoto->work);
        }
        break;

        default:
            break;
    }

    return ret;
}

struct msi *usb_odpi_takephoto_init(uint8_t jpg_num, const char *photo_src, const char *thumb_src, uint8_t use_watermark)
{
    uint8_t                         isnew;
    uint32_t                        normal_magic = 0;
    uint32_t                        thumb_magic  = 0;
    struct msi                     *msi;
    struct usb_over_dpi_takephoto_s *takephoto;
    struct msi                     *thumb_decode_msg_msi;
    struct msi                     *normal_decode_msg_msi = NULL;
    struct msi                     *decode_msi;

    if (takephoto_over_dpi_get_magic(&normal_magic, &thumb_magic))
    {
        common_takephoto_over_dpi_init(jpg_num);
        takephoto_over_dpi_get_magic(&normal_magic, &thumb_magic);
    }

    if (!normal_magic || !thumb_magic)
    {
        os_printf(KERN_ERR "usb over dpi takephoto magic invalid\n");
        return NULL;
    }

    msi = msi_new(USB_ODPI_TAKEPHOTO_CTRL, USB_OVER_DPI_MAX, &isnew);
    if (!msi)
    {
        return NULL;
    }

    takephoto = (struct usb_over_dpi_takephoto_s *) msi->priv;
    if (isnew || !takephoto)
    {
        takephoto = (struct usb_over_dpi_takephoto_s *) STREAM_LIBC_ZALLOC(sizeof(struct usb_over_dpi_takephoto_s));
        if (!takephoto)
        {
            return NULL;
        }
        takephoto->msi = msi;
        msi->priv      = takephoto;
        msi->action    = usb_over_dpi_takephoto_action;
        OS_WORK_INIT(&takephoto->work, usb_over_dpi_takephoto_work, 0);
    }

    takephoto->normal_magic  = normal_magic;
    takephoto->thumb_magic   = thumb_magic;
    takephoto->use_watermark = use_watermark;
    msi->enable              = 0;

    if (takephoto->normal_decode_msg_msi)
    {
        takephoto->normal_decode_msg_msi->enable = 0;
        takephoto->normal_decode_msg_msi         = NULL;
    }

    thumb_decode_msg_msi = jpg_decode_msg_msi(USB_OVER_DPI_THUMB_DECODE, 320, 180, 320, 180, 0);
    if (thumb_decode_msg_msi)
    {
        thumb_decode_msg_msi->enable = 0;
        msi_do_cmd(thumb_decode_msg_msi, MSI_CMD_DECODE_JPEG_MSG, MSI_JPEG_DECODE_FORCE_TYPE, FSTYPE_OVER_DPI_THUMB_JPG);
        msi_do_cmd(thumb_decode_msg_msi, MSI_CMD_DECODE_JPEG_MSG, MSI_JPEG_DECODE_MAGIC, thumb_magic);
    }

    decode_msi = jpg_decode_msi(S_JPG_DECODE);
    if (thumb_decode_msg_msi && decode_msi)
    {
        msi_add_output(msi, NULL, thumb_decode_msg_msi->name);
        msi_add_output(thumb_decode_msg_msi, NULL, decode_msi->name);
        msi_add_output(decode_msi, NULL, msi->name);
        takephoto->thumb_decode_msg_msi = thumb_decode_msg_msi;
    }
    else
    {
        takephoto->thumb_decode_msg_msi = NULL;
    }

    if (takephoto->use_watermark && photo_src && thumb_src)
    {
        msi_add_output(NULL, photo_src, msi->name);
        msi_add_output(NULL, thumb_src, msi->name);
    }
    else if (!takephoto->use_watermark && photo_src)
    {
        normal_decode_msg_msi = jpg_decode_msg_msi(USB_OVER_DPI_NORMAL_DECODE, 1280, 720, 1280, 720, FSTYPE_USB_CAM0);
        if (normal_decode_msg_msi)
        {
            normal_decode_msg_msi->enable = 0;
            if (decode_msi)
            {
                msi_do_cmd(normal_decode_msg_msi, MSI_CMD_DECODE_JPEG_MSG, MSI_JPEG_DECODE_FORCE_TYPE, FSTYPE_USB_TAKEPHOTO);
                msi_do_cmd(normal_decode_msg_msi, MSI_CMD_DECODE_JPEG_MSG, MSI_JPEG_DECODE_MAGIC, normal_magic);
                msi_add_output(msi, NULL, normal_decode_msg_msi->name);
                msi_add_output(normal_decode_msg_msi, NULL, decode_msi->name);
                takephoto->normal_decode_msg_msi = normal_decode_msg_msi;
            }
        }
        msi_add_output(NULL, photo_src, msi->name);
    }

    msi_add_output(msi, NULL, SR_OVER_DPI_JPG);
    msi_add_output(msi, NULL, SR_OVER_DPI_THUMB_JPG);

    return msi;
}

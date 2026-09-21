#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "lib/fs/fatfs/osal_file.h"
#include "stream_define.h"
#include "video_app/file_thumb.h"
#include "user_work/user_work.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "video_msi.h"
#include "video_app/file_common_api.h"
#include "scale_msi/scale3_normal_msi.h"
#include "yuv_from_cmd_msi.h"

static struct msi *compat_get_msi = NULL;

// 兼容旧版scale3生成yuv的接口
void compat_get_scale3_msi_init()
{
    if (!compat_get_msi)
    {
        compat_get_msi = get_yuv_msi(S_COMPAT_SCALE3_YUV, 0);
        msi_add_output(compat_get_msi, NULL, S_PREVIEW_SCALE3);
    }
}
/*************************************************************************************************************************
通用的获取yuv命令,这个仅仅为了兼容旧版本缩略图和原图,如果是其他自己数据流,参考去实现
************************************************************************************************************************ */
void get_yuv_from_scale3(uint8_t force_type, struct yuv_msg_s *msg1, struct yuv_msg_s *msg2, uint16_t count)
{
    uint32_t                  fb_size;
    struct framebuff         *fb;
    struct scale3_fb_input_s *fb_input;
    struct scale3_fb_data_s  *cmd;
    uint16_t                  i;
    uint16_t                  cmd_count;
    struct msi               *msi = compat_get_msi;
    if (!msi)
    {
        os_printf("%s:%d\tcompat_get_msi is NULL\n", __func__, __LINE__);
        return;
    }
    cmd_count = count * ((msg1 != NULL) + (msg2 != NULL));
    if (!cmd_count)
    {
        return;
    }

    fb_size = sizeof(struct scale3_fb_input_s) + sizeof(struct scale3_fb_data_s) * cmd_count;
    fb      = fb_alloc(NULL, fb_size, F_YUV_CMD << 8 | FSTYPE_NONE, msi);
    if (!fb)
    {
        return;
    }

    os_memset(fb->data, 0, fb_size);
    fb_input             = (struct scale3_fb_input_s *) fb->data;
    fb_input->count      = cmd_count;
    fb_input->force_type = force_type;
    fb_input->cmd        = (struct scale3_fb_data_s *) (fb_input + 1);
    cmd                  = fb_input->cmd;

    for (i = 0; i < count; i++)
    {
        if (msg1)
        {
            cmd->w          = msg1->w;
            cmd->h          = msg1->h;
            cmd->encode_w   = msg1->encode_w;
            cmd->encode_h   = msg1->encode_h;
            cmd->forward_en = 0;
            cmd->seq        = (uint8_t) i;
            cmd->magic      = msg1->magic;
            cmd++;
        }
        if (msg2)
        {
            cmd->w          = msg2->w;
            cmd->h          = msg2->h;
            cmd->forward_en = 0;
            cmd->seq        = (uint8_t) i;
            cmd->magic      = msg2->magic;
            cmd++;
        }
    }
    msi_output_fb(msi, fb);
}

/********************************************************************
这个模块主要是通过命令来获取yuv数据
一般是从scale3获取yuv数据
******************************************************************* */
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



static int32_t get_yuv_msi_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t ret = RET_OK;
    switch (cmd_id)
    {
        // 接收scale3的yuv数据后,直接给到下游
        case MSI_CMD_TRANS_FB:
        {
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb && fb->mtype == F_YUV)
            {
                fb_get(fb);
                msi_output_fb(msi, fb);
            }
            ret = RET_OK + 1;
        }
        break;
        case MSI_CMD_FREE_FB:
        {
        }
        break;
        default:
            break;
    }
    return ret;
}
struct msi *get_yuv_msi(const char *msi_name, uint8_t recv_count)
{
    uint8_t     isnew = 0;
    struct msi *m     = msi_new(msi_name, recv_count, &isnew);
    if (m && isnew)
    {
        m->action = get_yuv_msi_action;
        m->enable = 1;
    }
    return m;
}


#ifndef __SCALE3_NORMAL_MSI_H__
#define __SCALE3_NORMAL_MSI_H__
#include "basic_include.h"

// 动态输出流最大槽位数,可在此调整
#define SCALE3_MAX_STREAM 4

struct scale3_normal_cmd_s
{
    uint16_t       w;
    uint16_t       h;
    uint32_t       magic;
    uint32_t       force_type;
    uint8_t        is_thumb;
    struct timeval t;
};

// 动态输出流配置:指定哪个镜头输出什么规格的yuv
struct scale3_stream_cfg_s
{
    uint8_t  cam_id; // 匹配video_msg.video_type_cur(ISP_VIDEO_0/1/2),0xff=任意镜头
    uint16_t x, y;   // 输出帧yuv_arg携带的摆放坐标
    uint16_t w, h;   // 输出size
    uint8_t  force_stype;
};

// seq修改后,重置部分信息
struct scale3_fb_data_s
{
    uint16_t w;
    uint16_t h;
    uint16_t encode_w;
    uint16_t encode_h;
    uint8_t  is_thumb : 1, forward_en : 1, rev : 6;
    uint8_t  seq;
    uint32_t magic;
};

// scala3接收fb的私有结构体
struct scale3_fb_input_s
{
    uint8_t                  force_type;
    uint8_t                  rev;   // 预留暂时没用
    uint16_t                 count; // 当前有多少命令
    struct scale3_fb_data_s *cmd;
};

struct msi *scale3_normal_msi2(const char *name, uint8_t force_stype, uint16_t ow, uint16_t oh);
struct msi *scale3_msi_no_lcd(const char *name, uint8_t splice, uint8_t force_stype, uint16_t ow, uint16_t oh);
struct msi *scale3_normal_msi(const char *name, uint16_t ow, uint16_t oh);
// 动态多流模式:不注册默认流,输出规格完全由scale3_normal_msi_add_stream配置
// splice=是否拼接, max_fb_count=fb结构池深度(0=不预分配)
struct msi *scale3_normal_msi_multi(const char *name, uint8_t splice, uint8_t max_fb_count);
// 动态增加/删除输出流,删除时在途帧会自然走完,返回RET_OK/RET_ERR
int32_t     scale3_normal_msi_add_stream(struct msi *msi, const struct scale3_stream_cfg_s *cfg);
int32_t     scale3_normal_msi_del_stream(struct msi *msi, uint8_t cam_id);
#endif
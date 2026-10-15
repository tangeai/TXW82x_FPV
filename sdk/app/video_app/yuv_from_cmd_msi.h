#ifndef __YUV_FROM_CMD_MSI_H__
#define __YUV_FROM_CMD_MSI_H__
// 配置获取yuv的参数结构体
struct yuv_msg_s
{
    uint16_t w;
    uint16_t h;
    // 设置为0,则代表编码与w和h一致
    uint16_t encode_w;
    uint16_t encode_h;
    uint32_t magic;
};

struct msi *get_yuv_msi(const char *msi_name, uint8_t recv_count);
#endif

#ifndef _SCALE_DEV_H_
#define _SCALE_DEV_H_
#include "hal/scale.h"

enum scale_loc_mode {
    SCALE_ALIGN_CENTER,
    SCALE_UPPER_LEFT,
    SCALE_LOWER_RIGHT,
    SCALE_MANUAL,
};

struct scale_cfg {
    struct scale_device *scale_dev;
    uint32_t yinbuf;
    uint32_t uinbuf;
    uint32_t vinbuf;
    uint32_t yuvoutbuf;
    uint16_t in_w;
    uint16_t in_h;
    uint16_t out_w;
    uint16_t out_h;
    uint16_t start_x;
    uint16_t start_y;
    uint16_t tailor_w;
    uint16_t tailor_h;
    uint8_t  stream_type;   // 0:jpeg 1:h264 2:yuv420
    uint8_t  loc_mode;
};


void scale_from_vpp(struct scale_device *scale_dev,uint32 yuvbuf_addr,uint32 s_w,uint32 s_h,uint32 d_w,uint32 d_h);
void scale3_all_frame(struct scale_device *scale_dev,uint32_t in_w,uint32_t in_h,uint32_t out_w,uint32_t out_h,uint8_t input_format,uint8_t output_format,uint32_t src,uint32_t dst);
void scale_from_jpeg_config(struct scale_device *scale_dev,uint8_t dirtolcd,uint32 in_w,uint32 in_h,uint32 out_w,uint32 out_h,uint8_t larger);
void scale2_from_jpeg_config_for_msi(struct scale_device *scale_dev,uint32_t yinsram,uint32_t uinsram,uint32_t vinsram,uint32_t yuvoutbuf,uint32 in_w,uint32 in_h,uint32 out_w,uint32 out_h,uint8_t larger);
void scale_from_h264_config(struct scale_device *scale_dev,uint32 in_w,uint32 in_h,uint32 out_w,uint32 out_h,uint8_t larger);
void scale2_all_frame(struct scale_device *scale_dev,uint8_t type,uint32 in_w,uint32 in_h,uint32 out_w,uint32 out_h,uint32 src_addr,uint32 des_addr);
void scale_to_lcd_config(uint32_t iw,uint32_t ih);
void scale2_mutex_init();
void scale3_to_memory_for_thumb(uint16_t iw,uint16_t ih,uint16_t ow,uint16_t oh,uint32_t outadr,uint32_t yuvsram,uint16_t lanenum);
void scale_soft_from_psram_to_enc(struct scale_device *scale_dev,uint8_t * psram_data,uint32_t w,uint32 h,uint32_t ow,uint32_t oh);
void scale2_from_h264_config_for_msi(struct scale_device *scale_dev,uint32_t yinsram,uint32_t uinsram,uint32_t vinsram,uint32_t yuvoutbuf,uint32 in_w,uint32 in_h,uint32 out_w,uint32 out_h,uint8_t larger);
void scale2_config_for_msi(struct scale_cfg *scale_cfg);

#endif

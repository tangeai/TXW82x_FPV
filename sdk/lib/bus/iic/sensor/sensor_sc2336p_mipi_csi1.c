#include "sys_config.h"
#include "typesdef.h"
#include "lib/video/dvp/cmos_sensor/csi.h"
#include "tx_platform.h"
#include "list.h"
#include "dev.h"
#include "hal/isp.h"

#if DEV_SENSOR_SC2336P_CSI1

#define SC2336P_1920X1024_12FPS   1
#define SC2336P_1920X1080_25FPS   0

SENSOR_INIT_SECTION static const unsigned char sc2336p_1920x1024_slave_1line[CMOS_INIT_LEN]= 
{	
    0x01,0x03,0x01,
    0x01,0x00,0x00,
    0x36,0xe9,0x80,
    0x37,0xf9,0x80,
    0x30,0x18,0x12,
    0x30,0x19,0x0e,
    0x30,0x1f,0x68,
    0x31,0x06,0x05,
    0x32,0x00,0x00,
    0x32,0x01,0x00,
    0x32,0x02,0x00,
    0x32,0x03,0x00,
    0x32,0x04,0x07,
    0x32,0x05,0x87,
    0x32,0x06,0x04,
    0x32,0x07,0x41,
    0x32,0x08,0x07,
    0x32,0x09,0x88,
    0x32,0x0a,0x04,
    0x32,0x0b,0x3e,
    0x32,0x0c,0x08,
    0x32,0x0d,0x98,
    0x32,0x0e,0x04,
    0x32,0x0f,0x70,
    0x32,0x10,0x00,
    0x32,0x11,0x00,
    0x32,0x12,0x00,
    0x32,0x13,0x02,
    0x32,0x48,0x04,
    0x32,0x49,0x0b,
    0x32,0x50,0x40,
    0x32,0x53,0x08,
    0x33,0x01,0x09,
    0x33,0x02,0xff,
    0x33,0x03,0x10,
    0x33,0x06,0x80,
    0x33,0x07,0x02,
    0x33,0x09,0xc8,
    0x33,0x0a,0x01,
    0x33,0x0b,0x30,
    0x33,0x0c,0x16,
    0x33,0x0d,0xff,
    0x33,0x18,0x02,
    0x33,0x1f,0xb9,
    0x33,0x21,0x0a,
    0x33,0x27,0x0e,
    0x33,0x2b,0x12,
    0x33,0x33,0x10,
    0x33,0x34,0x40,
    0x33,0x5e,0x06,
    0x33,0x5f,0x0a,
    0x33,0x64,0x1f,
    0x33,0x7c,0x02,
    0x33,0x7d,0x0e,
    0x33,0x90,0x09,
    0x33,0x91,0x0f,
    0x33,0x92,0x1f,
    0x33,0x93,0x20,
    0x33,0x94,0x20,
    0x33,0x95,0xe0,
    0x33,0xa2,0x04,
    0x33,0xb1,0x80,
    0x33,0xb2,0x68,
    0x33,0xb3,0x42,
    0x33,0xf9,0x90,
    0x33,0xfb,0xd0,
    0x33,0xfc,0x0f,
    0x33,0xfd,0x1f,
    0x34,0x9f,0x03,
    0x34,0xa6,0x0f,
    0x34,0xa7,0x1f,
    0x34,0xa8,0x42,
    0x34,0xa9,0x18,
    0x34,0xaa,0x01,
    0x34,0xab,0x43,
    0x34,0xac,0x01,
    0x34,0xad,0x80,
    0x36,0x30,0xf4,
    0x36,0x32,0x44,
    0x36,0x33,0x22,
    0x36,0x39,0xf4,
    0x36,0x3c,0x47,
    0x36,0x70,0x09,
    0x36,0x74,0xf4,
    0x36,0x75,0xfb,
    0x36,0x76,0xed,
    0x36,0x7c,0x09,
    0x36,0x7d,0x0f,
    0x36,0x90,0x22,
    0x36,0x91,0x22,
    0x36,0x92,0x22,
    0x36,0x98,0x89,
    0x36,0x99,0x96,
    0x36,0x9a,0xd0,
    0x36,0x9b,0xd0,
    0x36,0x9c,0x09,
    0x36,0x9d,0x0f,
    0x36,0xa2,0x09,
    0x36,0xa3,0x0f,
    0x36,0xa4,0x1f,
    0x36,0xd0,0x01,
    0x36,0xea,0x19,
    0x36,0xeb,0x0c,
    0x36,0xec,0x0c,
    0x36,0xed,0x28,
    0x37,0x22,0xc1,
    0x37,0x24,0x41,
    0x37,0x25,0xc1,
    0x37,0x28,0x20,
    0x37,0xfa,0x19,
    0x37,0xfb,0x32,
    0x37,0xfc,0x11,
    0x37,0xfd,0x07,
    0x39,0x00,0x0d,
    0x39,0x05,0x98,
    0x39,0x19,0x04,
    0x39,0x1b,0x81,
    0x39,0x1c,0x10,
    0x39,0x33,0x81,
    0x39,0x34,0xd0,
    0x39,0x40,0x75,
    0x39,0x41,0x00,
    0x39,0x42,0x01,
    0x39,0x43,0xd1,
    0x39,0x52,0x02,
    0x39,0x53,0x0f,
    0x3e,0x01,0x46,
    0x3e,0x02,0xa0,
    0x3e,0x08,0x1f,
    0x3e,0x1b,0x14,
    0x45,0x09,0x38,
    0x48,0x00,0x44,
    0x57,0x99,0x06,
    0x5a,0xe0,0xfe,
    0x5a,0xe1,0x40,
    0x5a,0xe2,0x30,
    0x5a,0xe3,0x28,
    0x5a,0xe4,0x20,
    0x5a,0xe5,0x30,
    0x5a,0xe6,0x28,
    0x5a,0xe7,0x20,
    0x5a,0xe8,0x3c,
    0x5a,0xe9,0x30,
    0x5a,0xea,0x28,
    0x5a,0xeb,0x3c,
    0x5a,0xec,0x30,
    0x5a,0xed,0x28,
    0x5a,0xee,0xfe,
    0x5a,0xef,0x40,
    0x5a,0xf4,0x30,
    0x5a,0xf5,0x28,
    0x5a,0xf6,0x20,
    0x5a,0xf7,0x30,
    0x5a,0xf8,0x28,
    0x5a,0xf9,0x20,
    0x5a,0xfa,0x3c,
    0x5a,0xfb,0x30,
    0x5a,0xfc,0x28,
    0x5a,0xfd,0x3c,
    0x5a,0xfe,0x30,
    0x5a,0xff,0x28,
    0x32,0x00,0x00,
    0x32,0x01,0x00,
    0x32,0x02,0x00,
    0x32,0x03,0x00,
    0x32,0x04,0x07,
    0x32,0x05,0x87,
    0x32,0x06,0x04,
    0x32,0x07,0x3f,
    0x32,0x08,0x07,
    0x32,0x09,0x80,
#if SC2336P_1920X1024_12FPS
    0x32,0x0a,0x04,//h size h
    0x32,0x0b,0x00,//h size l
    0x32,0x2e,0x0B,//Active Rows + Blank Rows
    0x32,0x2f,0x13,
    0x32,0x0e,0x0B,//vts h
    0x32,0x0f,0x17,//vts l
#elif SC2336P_1920X1080_25FPS
    0x32,0x0a,0x04,//h size h
    0x32,0x0b,0x38,//h size l
    0x32,0x2e,0x05,//Active Rows,Blank Rows
    0x32,0x2f,0x4f,   
    0x32,0x0e,0x05,//vts h, Active Rows + Blank Rows = VTS - RB_Rows
    0x32,0x0f,0x53,//vts l
#endif
    0x32,0x10,0x00,
    0x32,0x11,0x04,
    0x32,0x12,0x00,
    0x32,0x13,0x04,
    0x32,0x22,0x02,
    0x32,0x30,0x00,//RB_Rows
    0x32,0x31,0x04,
    0x32,0x24,0x82,//0x83下降沿触发,0x82上升沿触发
    0x36,0xe9,0x50,
    0x37,0xf9,0x23,
    0x01,0x00,0x01,
    0x32,0x21,0x66, // bit[2:1]mirror,bit[6:5]flip;00=off,11=on
    0xff,0xff,0xff,

};

static const _Sensor_CCM sc2336p_ccm_init =
{
    480,  -95,  -64,
   -208,  456, -200,
    -16, -105,  520,
      0,    0,    0,
};

static const _Sensor_BLC sc2336p_blc_init =
{
    256, 256, 256, 256,
};

static const _Sensor_AWB sc2336p_awb_init = 
{
//    r,  gr,  gb,   b
    .default_gain   = {383, 256, 256, 478},
    .awb_min_gain   = {256, 256, 256, 256},
    .awb_max_gain   = {800, 256, 256, 800},

    .coarse_constraint = {
        .coarse_min_bg =  70,
        .coarse_lb_bg  = 100,
        .coarse_rt_bg  = 120,
        .coarse_max_bg = 210,
        .coarse_min_rg = 100,
        .coarse_lb_rg  = 150,
        .coarse_rt_rg  = 145,
        .coarse_max_rg = 270,
    },

    .constraint = {
        .section_num = 4,
        .color_temp = { 6500, 5000, 4000, 2856, 0, 0, 0, 0},
        .sec_line_slope = {1.05882353, 1.28571429, 1.63157895, 1.62962963, 0, 0, 0, 0},
        .sec_line_offset = {22.05882353, -48.71428571, -142.00000000, -240.37037037, 0, 0, 0, 0},
        .sec_line_sqrtk2add1 = {0.68662353, 0.61394061, 0.52256206, 0.52301622, 0, 0, 0, 0},
        .center_line_slope = {-0.94444444, -0.61111111, -0.61363636, 0, 0, 0, 0},
        .center_line_offset = {292.50000000, 241.50000000, 241.93181818, 0, 0, 0, 0},
        .lower_line_slope = {-4.95081397, -0.62333997, -0.18264122, 0, 0, 0, 0},
        .lower_line_offset = {771.46450775, 202.34940780, 135.05015736, 0, 0, 0, 0},
        .upper_line_slope = {-0.95003818, -0.60443022, -0.61363631, 0, 0, 0, 0},
        .upper_line_offset = {313.94513029, 257.88437005, 259.53077303, 0, 0, 0, 0},
        .corner_limit = {124.70064701, 154.09480271, 145.29935299, 175.90519729, 207.15475670, 97.21515907, 222.84524330, 122.78484093},
    },
};

static const _Sensor_AE sc2336p_ae_init = 
{
    .max_frame_length      = 1200,
    .min_frame_vb          = 6,
    .max_analog_gain       = 128<<8,
    .min_analog_gain       = 1<<8,
    .default_exposure_line = 1200-6,
    .max_exposure_line     = 1200-6,
    .min_exposure_line     = 2,
    .row_time_us           = 33.33,
    .expo_frame_interval   = 2,
    .curr_fps              = 25 << 8,
    .dark_scene_target_lut = {35, 55},
    .dark_scene_bv_lut     = {34, 280},
    .hs_scene_limit_lut    = {55, 58},
    .hs_scene_bv_lut       = {280, 3534},
    .lowlight_lsb_bv_lut   = {34, 115, 222, 400, 791, 1599, 3534,  1e30},
    .lowlight_lsb_gain_lut = {64, 52,  46,   35,  25,  20,   16,   16},  // u7.4
};

static const _Sensor_DPC sc2336p_dpc_init = 
{
    .static_psram_addr      = (uint32)0,
    .white_threshold        = 115,
    .black_threshold        = 115,
    .white_threshold_min    = 30,
    .black_threshold_min    = 30,
    .sensitivity_value      = 128,
    .dynamic_white_strength = 4,
    .dynamic_black_strength = 4,
};


static const _Sensor_GAMMA_BV sc2336p_gamma_map = 
{
    .adj_by_bv = 1,

    .bv = {
        29491, 3534, 1599, 347, 222, 115, 57, 34,
    },

    .y_alpha = {
        255,  255, 192, 192, 128, 128, 64, 64,                         
    },

    .rgb_alpha = {
        255,  255, 192, 192, 128, 128, 64, 64,     
    },
};

static const _Sensor_CSC sc2336p_csc_init = 
{
    .rgb2yuv_gamut         = ISP_YUV_GAMUT_BT709,
    .rgb2yuv_range         = ISP_YUV_RANGE_NARROW,
    .yuv2rgb_in_gamut      = ISP_YUV_GAMUT_BT709,
    .yuv2rgb_in_range      = ISP_YUV_RANGE_NARROW,
    .yuv2rgb_out_gamut     = ISP_YUV_GAMUT_BT709,
    .yuv2rgb_out_range     = ISP_YUV_RANGE_NARROW,
    .y_gamma_alpha         = 0xff,
    .rgb_gamma_alpha       = 0xff,
    .gamma_alpha_map       = (void *)&sc2336p_gamma_map,
};


static const _Sensor_GIC sc2336p_gic_init = 
{
    .w_thres  = 14,
    .w_slope  = 16,
    .w_str    = 127,
    .mu_thres = 5,
    .mu_slope = 16,
};
static const _Sensor_CSUPP sc2336p_csupp_init = {
// u8  U_luma_thr_lo, U_luma_slop_lo, U_luma_shfb_lo, U_luma_gmin_lo,
// u8  U_luma_thr_hi, U_luma_slop_hi, U_luma_shfb_hi, U_luma_gmin_hi,
// u8  V_luma_thr_lo, V_luma_slop_lo, V_luma_shfb_lo, V_luma_gmin_lo,
// u8  V_luma_thr_hi, V_luma_slop_hi, V_luma_shfb_hi, V_luma_gmin_hi,
// u8  chroma_thr_lo, chroma_slop_lo, chroma_shfb_lo, chroma_gmin_lo,
     31,   4,   0,   0,
    209,   4,   0,   0,
     31,   4,   0,   0,
    209,   4,   0,   0,
     31,   4,   0,   0,
};

static const _Sensor_SHARP sc2336p_sharp_init = {
    .filt_alpha      = 128 ,
    .shrink_thr      = 5   ,
    .filt_clip_hi    = 127 ,
    .filt_clip_lo    = 127 ,
    
    .sp_thr2 		 = 20  ,    
    .sp_thr1 	 	 = 10  ,
    .enha_clip_hi 	 = 127 ,
    .enha_clip_lo	 = 127 ,
    
    .e1 = 8,  .e2 = 28, .e3 = 40,
    .k0 = 96, .k1 = 32, .k2 = 32, .k3 = 16,
    .y1 = 24,              // y1 = k0*e1
    .y2 = 44,              // y2 = k1*e2 + (y1 - k1*e1) = k1 * (e2 - e1) + y1
    .y3 = 56,              // y3 = k2*e3 + (y2 - k2*e2) = k2 * (e3 - e2) + y2

    // --- Unsharp Mask ---
    .filt_w11 = 7,  .filt_w12 = 9,  .filt_w13 = 10,
    .filt_w21 = 9,  .filt_w22 = 11, .filt_w23 = 12,
    .filt_w31 = 10, .filt_w32 = 12, .filt_w33 = 24,
    .filt_type = 1,
    .filt_sbit = 8,
    .lpf_scale = 2,

    // --- Sharpen Mask --- 
    // .filt_w11 = -2,  .filt_w12 = -12, .filt_w13 = -19,
    // .filt_w21 = -12, .filt_w22 = -24, .filt_w23 =  20,
    // .filt_w31 = -19, .filt_w32 =  20, .filt_w33 = 196,
    // .filt_type = 0,
    // .filt_sbit = 8,
    // .lpf_scale = 0,

    // .filt_w11 = 0,  .filt_w12 = 0,  .filt_w13 = -1,
    // .filt_w21 = 0,  .filt_w22 = -1, .filt_w23 = -2,
    // .filt_w31 = -1, .filt_w32 = -2, .filt_w33 = 16,
    // .filt_type = 0,
    // .filt_sbit = 4,
    // .lpf_scale = 0,

    .strength_lut    = {32,  255},
    .strength_bv_lut = {791, 9930},
};

static const _Sensor_YUVNR sc2336p_yuvnr_init = {
    .y_thr_tal0 = 20,
	.y_thr_tal1 = 20,
	.y_thr_tal2 = 20,
	.y_thr_tal3 = 20,
	.y_thr_tal4 = 20,
	.y_thr_tal5 = 20,
	.y_thr_tal6 = 20,
	.y_thr_tal7 = 20,
	.y_alfa     = 205,
	.c_alfa     = 255,
	.y_win_size = 0,
};

static const _Sensor_COLENH_BV sc2336p_ce_map[BV2COLENH_ARRAY_NUM] = {
    {.bv =   29491, .hue = 0, .luma = 58, .contrast = 68, .saturation = 70},
    {.bv =    3534, .hue = 0, .luma = 58, .contrast = 68, .saturation = 70},
    {.bv =    4531, .hue = 0, .luma = 50, .contrast = 68, .saturation = 65},
    {.bv =     400, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =     222, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =     115, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =      57, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =      34, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
};

static const _Sensor_COLENH sc2336p_colenh_init = {
    .yuv_range  = 0,
    .luma       = 50, // range: 0 ~ 100
    .contrast   = 50, // range: 0 ~ 100
    .saturation = 70, // range: 0 ~ 100
    .hue        = 0, // range: -180 ~ 180
    .ce_in_ofs_y   = 200,
    .ce_in_ofs_cb  = 128,
    .ce_in_ofs_cr  = 128, // range: -128 ~ 128
    .ce_out_ofs_y  = 200, 
    .ce_out_ofs_cb = 128, 
    .ce_out_ofs_cr = 128, // range: -128 ~ 128
    .adj_by_bv_en  = 1,
    .bv2colenh_map = (void *)sc2336p_ce_map,
};
// 1.0 - 0.01lux TBD
static const _Sensor_BV2NR sc2336p_bv2nr_init[BV2RAWNR_ARRAY_NUM] = {
    //        bv, bnr_range_weight_index, bnr_invksigma, bnr_intensity_threshold, yuvnr_idx, csupp_idx,  h264_3dnr_lev, h264_3dnr_en
    {      29491,                     6 ,           511,                      63,         0,         0,         0,              0},    // 320lux
    {       3534,                     8 ,           407,                      63,         0,         0,         1,              1},    // 40lux
    {       1599,                     8 ,           271,                      63,         1,         0,         1,              1},    // 20lux
    {        791,                     16,           271,                      63,         1,         1,         2,              1},    // 10lux
    {        222,                     16,           135,                      48,         2,         1,         2,              1},    // 5p03lux
    {        115,                     16,           135,                      48,         2,         1,         2,              1},    // 2p5lux
    {         57,                     20,           101,                      32,         3,         1,         2,              1},    // 1p25lux
    {         34,                     24,            62,                      32,         4,         1,         2,              1},    // 0p62lux
    {         26,                     26,            50,                      32,         4,         1,         2,              1},    // 0p31lux
    {         21,                     28,            40,                      32,         5,         2,         2,              1},    // 0p1lux
    {         16,                     31,            25,                      32,         5,         2,         2,              1},    // 0p01lux
};
static const uint32 sc2336p_lsc_tbl[] = {
//R channel
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
//GR channel
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
//GB channel
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
//B channel
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,0x00040100,
};

static const _Sensor_LSC          sc2336p_lsc_init = {
    .p_lsc_tbl = (uint32 *)sc2336p_lsc_tbl,
};

static const _Sensor_LHS sc2336p_lhs_map[9] = {
    // region defination: lower -> center -> upper(direction: anticlockwise)
    // region_lower, region_center, region_upper, hue adjust value, saturation adjust value
    //   (9 bits)      (9 bits)       (9 bits)          (9 bits)           (8 bits)
    {            24,            52,           80,                0,                       0},  // magenta,          range: 28
    {            80,           109,          138,                0,                       0},  // red,              range: 29
    {           140,           171,          202,                0,                       0},  // yellow,           range: 31
    {           204,           232,          260,                0,                       0},  // green,            range: 28
    {           261,           289,          317,                0,                       0},  // cyan,             range: 28
    {           320,           351,           22,                0,                       0},  // blue,             range: 31
    {           109,           132,          156,                0,                       0},  // skin enhance,     range:
    {           160,           203,          247,                0,                       0},  // green enhance(plants),    range:
    {           296,           318,          340,                0,                       0}   // blue enhance,     range:
};

// 预设的Gamma曲线和对应的BV值
static const _Sensor_YGAMMA sc2336p_ygamma_tbl[NUM_CURVES] = {
    {
     .bv = 200,
     .packed_lut = {
        0x01002000, 0x02006010, 0x0300A020, 0x0400E030, 0x05012040, 0x06016050, 0x0701A060, 0x0801E070, 
        0x09022080, 0x0A026090, 0x0B02A0A0, 0x0C02E0B0, 0x0D0320C0, 0x0E0360D0, 0x0F03A0E0, 0x1003E0F0, 
        0x11042100, 0x12046110, 0x1304A120, 0x1404E130, 0x15052140, 0x16056150, 0x1705A160, 0x1805E170, 
        0x19062180, 0x1A066190, 0x1B06A1A0, 0x1C06E1B0, 0x1D0721C0, 0x1E0761D0, 0x1F07A1E0, 0x2007E1F0, 
        0x21082200, 0x22086210, 0x2308A220, 0x2408E230, 0x25092240, 0x26096250, 0x2709A260, 0x2809E270, 
        0x290A2280, 0x2A0A6290, 0x2B0AA2A0, 0x2C0AE2B0, 0x2D0B22C0, 0x2E0B62D0, 0x2F0BA2E0, 0x300BE2F0, 
        0x310C2300, 0x320C6310, 0x330CA320, 0x340CE330, 0x350D2340, 0x360D6350, 0x370DA360, 0x380DE370, 
        0x390E2380, 0x3A0E6390, 0x3B0EA3A0, 0x3C0EE3B0, 0x3D0F23C0, 0x3E0F63D0, 0x3F0FA3E0, 0x3FFFE3F0,}},
    {
     .bv = 500,
     .packed_lut = {
        0x01002000, 0x02006010, 0x0300A020, 0x0400E030, 0x05012040, 0x06016050, 0x0701A060, 0x0801E070, 
        0x09022080, 0x0A026090, 0x0B02A0A0, 0x0C02E0B0, 0x0D0320C0, 0x0E0360D0, 0x0F03A0E0, 0x1003E0F0, 
        0x11042100, 0x12046110, 0x1304A120, 0x1404E130, 0x15052140, 0x16056150, 0x1705A160, 0x1805E170, 
        0x19062180, 0x1A066190, 0x1B06A1A0, 0x1C06E1B0, 0x1D0721C0, 0x1E0761D0, 0x1F07A1E0, 0x2007E1F0, 
        0x21082200, 0x22086210, 0x2308A220, 0x2408E230, 0x25092240, 0x26096250, 0x2709A260, 0x2809E270, 
        0x290A2280, 0x2A0A6290, 0x2B0AA2A0, 0x2C0AE2B0, 0x2D0B22C0, 0x2E0B62D0, 0x2F0BA2E0, 0x300BE2F0, 
        0x310C2300, 0x320C6310, 0x330CA320, 0x340CE330, 0x350D2340, 0x360D6350, 0x370DA360, 0x380DE370, 
        0x390E2380, 0x3A0E6390, 0x3B0EA3A0, 0x3C0EE3B0, 0x3D0F23C0, 0x3E0F63D0, 0x3F0FA3E0, 0x3FFFE3F0,}},
    {
     .bv = 1000,
     .packed_lut = {
        0x01002000, 0x02006010, 0x0300A020, 0x0400E030, 0x05012040, 0x06016050, 0x0701A060, 0x0801E070, 
        0x09022080, 0x0A026090, 0x0B02A0A0, 0x0C02E0B0, 0x0D0320C0, 0x0E0360D0, 0x0F03A0E0, 0x1003E0F0, 
        0x11042100, 0x12046110, 0x1304A120, 0x1404E130, 0x15052140, 0x16056150, 0x1705A160, 0x1805E170, 
        0x19062180, 0x1A066190, 0x1B06A1A0, 0x1C06E1B0, 0x1D0721C0, 0x1E0761D0, 0x1F07A1E0, 0x2007E1F0, 
        0x21082200, 0x22086210, 0x2308A220, 0x2408E230, 0x25092240, 0x26096250, 0x2709A260, 0x2809E270, 
        0x290A2280, 0x2A0A6290, 0x2B0AA2A0, 0x2C0AE2B0, 0x2D0B22C0, 0x2E0B62D0, 0x2F0BA2E0, 0x300BE2F0, 
        0x310C2300, 0x320C6310, 0x330CA320, 0x340CE330, 0x350D2340, 0x360D6350, 0x370DA360, 0x380DE370, 
        0x390E2380, 0x3A0E6390, 0x3B0EA3A0, 0x3C0EE3B0, 0x3D0F23C0, 0x3E0F63D0, 0x3F0FA3E0, 0x3FFFE3F0,}},
    {
     .bv = 1500,
     .packed_lut = {
        0x01002000, 0x02006010, 0x0300A020, 0x0400E030, 0x05012040, 0x06016050, 0x0701A060, 0x0801E070, 
        0x09022080, 0x0A026090, 0x0B02A0A0, 0x0C02E0B0, 0x0D0320C0, 0x0E0360D0, 0x0F03A0E0, 0x1003E0F0, 
        0x11042100, 0x12046110, 0x1304A120, 0x1404E130, 0x15052140, 0x16056150, 0x1705A160, 0x1805E170, 
        0x19062180, 0x1A066190, 0x1B06A1A0, 0x1C06E1B0, 0x1D0721C0, 0x1E0761D0, 0x1F07A1E0, 0x2007E1F0, 
        0x21082200, 0x22086210, 0x2308A220, 0x2408E230, 0x25092240, 0x26096250, 0x2709A260, 0x2809E270, 
        0x290A2280, 0x2A0A6290, 0x2B0AA2A0, 0x2C0AE2B0, 0x2D0B22C0, 0x2E0B62D0, 0x2F0BA2E0, 0x300BE2F0, 
        0x310C2300, 0x320C6310, 0x330CA320, 0x340CE330, 0x350D2340, 0x360D6350, 0x370DA360, 0x380DE370, 
        0x390E2380, 0x3A0E6390, 0x3B0EA3A0, 0x3C0EE3B0, 0x3D0F23C0, 0x3E0F63D0, 0x3F0FA3E0, 0x3FFFE3F0, }},
    {// 线性曲线，BV=500
     .bv = 2000,
     .packed_lut = {
        0x01002000, 0x02006010, 0x0300A020, 0x0400E030, 0x05012040, 0x06016050, 0x0701A060, 0x0801E070, 
        0x09022080, 0x0A026090, 0x0B02A0A0, 0x0C02E0B0, 0x0D0320C0, 0x0E0360D0, 0x0F03A0E0, 0x1003E0F0, 
        0x11042100, 0x12046110, 0x1304A120, 0x1404E130, 0x15052140, 0x16056150, 0x1705A160, 0x1805E170, 
        0x19062180, 0x1A066190, 0x1B06A1A0, 0x1C06E1B0, 0x1D0721C0, 0x1E0761D0, 0x1F07A1E0, 0x2007E1F0, 
        0x21082200, 0x22086210, 0x2308A220, 0x2408E230, 0x25092240, 0x26096250, 0x2709A260, 0x2809E270, 
        0x290A2280, 0x2A0A6290, 0x2B0AA2A0, 0x2C0AE2B0, 0x2D0B22C0, 0x2E0B62D0, 0x2F0BA2E0, 0x300BE2F0, 
        0x310C2300, 0x320C6310, 0x330CA320, 0x340CE330, 0x350D2340, 0x360D6350, 0x370DA360, 0x380DE370, 
        0x390E2380, 0x3A0E6390, 0x3B0EA3A0, 0x3C0EE3B0, 0x3D0F23C0, 0x3E0F63D0, 0x3F0FA3E0, 0x3FFFE3F0,}}
};

static const _Sensor_WDR sc2336p_wdr_init = {
    .wdr_bv           = {791, 1599, 3534,  7000, 10000, 14000, 28000, 56000},
    .max_ns_slope     = {1.0,  1.0,  1.0,   1.0,  1.25,   1.5,   2.0,   2.5},
    .max_shadow_slope = {1.0,  1.0,  1.0,   1.0,   1.0,  1.25,   1.5,   1.5},
};


/**
 * @brief SC2336P AE/AGC寄存器配置函数（严格匹配手册表2-3/2-4/2-5）
 * @param p_cfg 曝光增益配置入参
 * @note 1. 优先模拟粗增益，模拟满32倍后开启数字增益
 * @note 2. DIG FINE GAIN用于平滑增益跳变，消除AGC震荡
 * @note 3. DIG_GAIN档位：0x00(1~2x) / 0x01(2~4x) / 0x03(固定4x)
 */
static void sc2336p_ae_adjust(struct isp_exposure_opt *p_cfg)
{
    uint8_t  cmd_idx = 0;
    uint8_t *p_buf = p_cfg->data.addr;

    uint32_t total_gain_int = p_cfg->analog_gain; // 总增益定点值 256=1x
    uint32_t exposure_line  = p_cfg->exposure_line;

    const uint8_t gain_segment[] = {1, 2, 4, 8, 16, 32};
    const uint8_t gain_value[]   = {0x00, 0x08, 0x09, 0x0B, 0x0F, 0x1F};
    uint8_t ana_idx = 5; // 默认最高32x模拟档位
    uint16_t fine_val = 0;
    uint8_t dig_gain = 0;

    // 选择模拟粗增益档位
    for (uint8_t i = 0; i < 6; i++) {
        if (total_gain_int >= (gain_segment[i] * 256)) {
            ana_idx = i;
        } else {
            break;
        }
    }
	
    // <64x：dig_gain=0x00（数字 1x 基底，精细 1~1.969x）
    // 64x~128x：dig_gain=0x01（数字 2x 基底，精细 1~1.969x）
    // ≥128x：dig_gain=0x03（固定 4 倍数字增益）
	uint8_t shift_bit = ana_idx;
    if (total_gain_int >= (128U * 256U)) {
        dig_gain = 0x03;
		shift_bit += 2;
    } else if (total_gain_int >= (64U * 256U)) {
        dig_gain = 0x01;
		shift_bit += 1;
    } else {
        dig_gain = 0x00;
    }

    // 精细增益计算 DIG FINE GAIN
	uint32_t base_gain = total_gain_int >> shift_bit;
	
    if (base_gain > 256U) {
        fine_val = (uint16_t)(((base_gain - 256U) >> 3) << 2);
        if (fine_val > 0x7F) {
            fine_val = 0x7F;
        }
    } else {
        fine_val = 0;
    }

    // 手册要求的配置: 0x3e03 Bit[3:0] = 0x0b 
    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x03;
    p_buf[cmd_idx++] = 0x0b;

    // 曝光行数寄存器
    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x02;
    p_buf[cmd_idx++] = (exposure_line << 4) & 0xf0;

    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x01;
    p_buf[cmd_idx++] = (exposure_line >> 4) & 0xff;

    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x00;
    p_buf[cmd_idx++] = (exposure_line >> 12) & 0x0f;

    // ANA GAIN 模拟粗增益
    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x09;
    p_buf[cmd_idx++] = gain_value[ana_idx];

    // DIG GAIN 数字粗增益
    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x06;
    p_buf[cmd_idx++] = dig_gain;

    // DIG FINE GAIN 数字细增益
    p_buf[cmd_idx++] = 0x3e;
    p_buf[cmd_idx++] = 0x07;
    p_buf[cmd_idx++] = 0x80 + fine_val;

    p_cfg->data.size = cmd_idx;
    p_cfg->cmd_len   = 2 + 1;
}

// static void sc2336p_fps_opt(struct isp_sensor_opt *p_opt)
// {
//     uint8  *addr        = (uint8 *)p_opt->data.addr;
//     uint8  index        = 0;
//     addr[index++]       = 0x32;
//     addr[index++]       = 0x0e;
//     addr[index++]       = p_opt->curr_length >> 8;
//     addr[index++]       = 0x32;
//     addr[index++]       = 0x0f;
//     addr[index++]       = p_opt->curr_length & 0xff;
//     p_opt->data.size    = index;
//     p_opt->cmd_len      = 2+1;
// }


static void sc2336p_img_opt(struct isp_sensor_opt *p_opt)
{
    uint8  *addr = (uint8 *)p_opt->data.addr;
    uint8  index = 0;
    uint8  reg_value = 0x00;
    // 0x3221 ([2:1]:mirror_mode 2'b00: mirror off 2'b11: mirror on) ([6:5]:flip_ctrl 2'b00:flip off 2'b11:flip on)
    addr[index++] = 0x32; 
    addr[index++] = 0x21;
    if (p_opt->reverse_en) {
        reg_value |= (0x03 << 5); //set bit[6:5] = 0b11
    }
    if (p_opt->mirror_en) {  // 
        reg_value |= (0x03 << 1); // set bit[2:1] = 0b11
    }
    addr[index++] = reg_value;
    p_opt->data.size = index;
    p_opt->cmd_len   = 2 + 1;
}

static const _Sensor_ISP_Init sc2336p_isp_init = 
{
    .type         = ISP_INPUT_DAT_SRC_MIPI1,
#if SC2336P_1920X1024_12FPS
    .pixel_h      = 1024,
    .pixel_w      = 1920,
#elif SC2336P_1920X1080_25FPS
    .pixel_h      = 1080,
    .pixel_w      = 1920,
#endif
    .mirror       = 1,
    .reverse      = 1,
    .bayer_patten = ISP_BAYER_FORMAT_BGGR,
    .input_format = ISP_INPUT_DAT_FORMAT_RAW10,
    .adjust_func  = (isp_ae_func     )sc2336p_ae_adjust,
    .p_blc        = (_Sensor_BLC    *)&sc2336p_blc_init,
    .p_ccm        = (_Sensor_CCM    *)&sc2336p_ccm_init,
    .p_awb        = (_Sensor_AWB    *)&sc2336p_awb_init,
    .p_ae         = (_Sensor_AE     *)&sc2336p_ae_init,
    .p_dpc        = (_Sensor_DPC    *)&sc2336p_dpc_init,
	.p_csc        = (_Sensor_CSC    *)&sc2336p_csc_init,
	.p_gic        = (_Sensor_GIC    *)&sc2336p_gic_init,
    .p_csupp      = (_Sensor_CSUPP  *)&sc2336p_csupp_init,
    .p_sharp      = (_Sensor_SHARP  *)&sc2336p_sharp_init,
    .p_yuvnr      = (_Sensor_YUVNR  *)&sc2336p_yuvnr_init,    
    .p_colenh     = (_Sensor_COLENH *)&sc2336p_colenh_init,
    .p_bv2nr      = (_Sensor_BV2NR  *)sc2336p_bv2nr_init,    
    .p_lsc        = (_Sensor_LSC    *)&sc2336p_lsc_init,
    .p_lhs        = (_Sensor_LHS    *)sc2336p_lhs_map,
    .p_ygamma     = (_Sensor_YGAMMA *)sc2336p_ygamma_tbl,
    .p_wdr        = (_Sensor_WDR    *)&sc2336p_wdr_init,
	// .fps_opt      = (sensor_fps_opt  )sc2336p_fps_opt,
    .img_opt      = (sensor_img_opt  )sc2336p_img_opt,
};

SENSOR_OP_SECTION const _Sensor_Adpt_ sc2336p_cmd_csi1 = 
{	
#if SC2336P_1920X1024_12FPS
	.pixelw = 1920,
	.pixelh = 1024,
#elif SC2336P_1920X1080_25FPS
   	.pixelw = 1920,
	.pixelh = 1080, 
#endif
	.init = (unsigned char *)sc2336p_1920x1024_slave_1line,
    .mipi_lane_num = 1,
    .vts_reg = {0x320e,0x320f},
    .vts_reg_num = 2,
    .sensor_isp  = (_Sensor_ISP_Init *)&sc2336p_isp_init,
};

const _Sensor_Ident_ sc2336p_init_csi1 =
{
	0x3a,0x60,0x61,0x02,0x01,0x3108
};



#endif

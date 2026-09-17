


#if 1

#include "sys_config.h"
#include "typesdef.h"
#include "lib/video/dvp/cmos_sensor/csi.h"
#include "tx_platform.h"
#include "list.h"
#include "dev.h"
#include "hal/isp.h"

#if DEV_SENSOR_SC1346
/* lens & sensor config information:
- sensor      : gc1346
- fstop       : TBD
- mclk        : 24MHz
- max FPS     : 25fps
- frame length: 1500
- usage       : TDB
- interface   : DVP
*/

SENSOR_INIT_SECTION const unsigned char sc1346InitTable[CMOS_INIT_LEN]=
{
#if 0

#else

    #if 0

    //222 ,0x,
    0x01,0x03,0x01,
    0x01,0x00,0x00,
    0x36,0xe9,0x80,
    0x37,0xf9,0x80,
    0x30,0x1f,0x05,
    0x31,0x06,0x05,
    0x32,0x00,0x00,
    0x32,0x01,0x00,
    0x32,0x02,0x00,
    0x32,0x03,0x00,
    0x32,0x04,0x05,
    0x32,0x05,0x07,
    0x32,0x06,0x02,
    0x32,0x07,0xdb,
    0x32,0x08,0x05,
    0x32,0x09,0x00,
    0x32,0x0a,0x02,
    0x32,0x0b,0xd0,
    0x32,0x0c,0x07,
    0x32,0x0d,0x08,
    0x32,0x10,0x00,
    0x32,0x11,0x04,
    0x32,0x12,0x00,
    0x32,0x13,0x04,
    0x32,0x50,0x00,
    0x33,0x01,0x06,
    0x33,0x06,0x50,
    0x33,0x08,0x0a,
    0x33,0x0a,0x00,
    0x33,0x0b,0xda,
    0x33,0x0e,0x0a,
    0x33,0x1e,0x61,
    0x33,0x1f,0xa1,
    0x33,0x64,0x1f,
    0x33,0x90,0x09,
    0x33,0x91,0x0f,
    0x33,0x92,0x1f,
    0x33,0x93,0x30,
    0x33,0x94,0x30,
    0x33,0x95,0x30,
    0x33,0xad,0x10,
    0x33,0xb3,0x40,
    0x33,0xf9,0x50,
    0x33,0xfb,0x80,
    0x33,0xfc,0x09,
    0x33,0xfd,0x0f,
    0x34,0x9f,0x03,
    0x34,0xa6,0x09,
    0x34,0xa7,0x0f,
    0x34,0xa8,0x40,
    0x34,0xa9,0x30,
    0x34,0xaa,0x00,
    0x34,0xab,0xe8,
    0x34,0xac,0x01,
    0x34,0xad,0x0c,
    0x36,0x30,0xe2,
    0x36,0x32,0x76,
    0x36,0x33,0x33,
    0x36,0x39,0xf4,
    0x36,0x41,0x00,
    0x36,0x70,0x09,
    0x36,0x74,0xe2,
    0x36,0x75,0xea,
    0x36,0x76,0xea,
    0x36,0x7c,0x09,
    0x36,0x7d,0x0f,
    0x36,0x90,0x22,
    0x36,0x91,0x22,
    0x36,0x92,0x22,
    0x36,0x98,0x88,
    0x36,0x99,0x90,
    0x36,0x9a,0xa1,
    0x36,0x9b,0xc3,
    0x36,0x9c,0x09,
    0x36,0x9d,0x0f,
    0x36,0xa2,0x09,
    0x36,0xa3,0x0b,
    0x36,0xa4,0x0f,
    0x36,0xd0,0x01,
    0x36,0xea,0x1b,
    0x36,0xeb,0x0d,
    0x36,0xec,0x15,
    0x36,0xed,0x28,
    0x37,0x0f,0x01,
    0x37,0x22,0x41,
    0x37,0x24,0x41,
    0x37,0x25,0xc1,
    0x37,0x28,0x00,
    0x37,0xb0,0x41,
    0x37,0xb1,0x41,
    0x37,0xb2,0x47,
    0x37,0xb3,0x09,
    0x37,0xb4,0x0f,
    0x37,0xfa,0x09,
    0x37,0xfb,0x33,
    0x37,0xfc,0x11,
    0x37,0xfd,0x37,
    0x39,0x03,0x40,
    0x39,0x04,0x04,
    0x39,0x05,0x8d,
    0x39,0x07,0x00,
    0x39,0x08,0x41,
    0x39,0x1f,0x41,
    0x39,0x33,0x80,
    0x39,0x34,0x02,
    0x39,0x35,0x02,
    0x39,0x36,0x00,
    0x39,0x37,0x74,
    0x39,0x38,0x75,
    0x39,0x39,0x0f,
    0x39,0x3a,0xf0,
    0x39,0x3b,0x0f,
    0x39,0x3c,0xfb,
    0x3e,0x01,0x2e,
    0x3e,0x02,0xa0,
    0x45,0x09,0x20,
    0x45,0x0d,0x28,
    0x48,0x00,0x64,
    0x48,0x19,0x06,
    0x48,0x1b,0x03,
    0x48,0x1d,0x0b,
    0x48,0x1f,0x03,
    0x48,0x21,0x08,
    0x48,0x23,0x03,
    0x48,0x25,0x03,
    0x48,0x27,0x03,
    0x48,0x29,0x05,
    0x57,0x80,0x66,
    0x57,0x8d,0x40,
    0x36,0xe9,0x28,
    0x37,0xf9,0x20,
    0x01,0x00,0x01,

    #else

    //111
    0x01, 0x03, 0x01,
    0x01, 0x00, 0x00,
    0x36, 0xe9, 0x80,
    0x37, 0xf9, 0x80,
    0x30, 0x1f, 0x03,
    0x31, 0x06, 0x05,

    // 0x32, 0x0e, 0x03,//800
    // 0x32, 0x0f, 0x84,

    0x32, 0x0e, 0x07,//1944
    0x32, 0x0f, 0x98,


    0x32, 0x21, 0x66,
    0x32, 0x50, 0x40,
    0x33, 0x01, 0x06,
    0x33, 0x06, 0x50,
    0x33, 0x08, 0x0a,
    0x33, 0x0a, 0x00,
    0x33, 0x0b, 0xda,
    0x33, 0x0e, 0x0a,
    0x33, 0x1e, 0x61,
    0x33, 0x1f, 0xa1,
    0x33, 0x64, 0x1f,
    0x33, 0x90, 0x09,
    0x33, 0x91, 0x0f,
    0x33, 0x92, 0x1f,
    0x33, 0x93, 0x30,
    0x33, 0x94, 0x30,
    0x33, 0x95, 0x30,
    0x33, 0xad, 0x10,
    0x33, 0xb3, 0x40,
    0x33, 0xf9, 0x50,
    0x33, 0xfb, 0x70,
    0x33, 0xfc, 0x09,
    0x33, 0xfd, 0x0f,
    0x34, 0x9f, 0x03,
    0x34, 0xa6, 0x09,
    0x34, 0xa7, 0x0f,
    0x34, 0xa8, 0x40,
    0x34, 0xa9, 0x30,
    0x34, 0xaa, 0x00,
    0x34, 0xab, 0xe8,
    0x34, 0xac, 0x00,
    0x34, 0xad, 0xfc,
    0x36, 0x30, 0xe2,
    0x36, 0x32, 0x76,
    0x36, 0x33, 0x33,
    0x36, 0x39, 0xf4,
    0x36, 0x70, 0x09,
    0x36, 0x74, 0xe2,
    0x36, 0x75, 0xea,
    0x36, 0x76, 0xea,
    0x36, 0x7c, 0x09,
    0x36, 0x7d, 0x0f,
    0x36, 0x90, 0x22,
    0x36, 0x91, 0x22,
    0x36, 0x92, 0x22,
    0x36, 0x98, 0x88,
    0x36, 0x99, 0x90,
    0x36, 0x9a, 0xa1,
    0x36, 0x9b, 0xc3,
    0x36, 0x9c, 0x09,
    0x36, 0x9d, 0x0f,
    0x36, 0xa2, 0x09,
    0x36, 0xa3, 0x0b,
    0x36, 0xa4, 0x0f,
    0x36, 0xd0, 0x01,
    0x36, 0xea, 0x1b,
    0x36, 0xeb, 0x0d,
    0x36, 0xec, 0x15,
    0x36, 0xed, 0x28,
    0x37, 0x0f, 0x01,
    0x37, 0x22, 0x41,
    0x37, 0x24, 0x41,
    0x37, 0x25, 0xc1,
    0x37, 0x28, 0x00,
    0x37, 0xb0, 0x41,
    0x37, 0xb1, 0x41,
    0x37, 0xb2, 0x47,
    0x37, 0xb3, 0x09,
    0x37, 0xb4, 0x0f,
    0x37, 0xfa, 0x09,
    0x37, 0xfb, 0x33,
    0x37, 0xfc, 0x11,
    0x37, 0xfd, 0x37,
    0x39, 0x03, 0x40,
    0x39, 0x04, 0x04,
    0x39, 0x05, 0x8d,
    0x39, 0x07, 0x00,
    0x39, 0x08, 0x41,
    0x39, 0x33, 0x80,
    0x39, 0x34, 0x0a,
    0x39, 0x37, 0x79,
    0x39, 0x39, 0x00,
    0x39, 0x3a, 0x00,
    0x3e, 0x01, 0x2e,
    0x3e, 0x02, 0xa0,
    0x44, 0x0e, 0x02,
    0x45, 0x09, 0x20,
    0x45, 0x0d, 0x28,
    0x48, 0x00, 0x44,
    0x48, 0x19, 0x06,
    0x48, 0x1b, 0x03,
    0x48, 0x1d, 0x0b,
    0x48, 0x1f, 0x03,
    0x48, 0x21, 0x08,
    0x48, 0x23, 0x03,
    0x48, 0x25, 0x03,
    0x48, 0x27, 0x03,
    0x48, 0x29, 0x05,
    0x36, 0xe9, 0x28,
    0x37, 0xf9, 0x20,
    0x01, 0x00, 0x01,
    0xff, 0xff, 0xff,
    #endif
#endif
};

const _Sensor_CCM sc1346_ccm_init =
{
    // 5500k, gamma2p2
    // 0x18b,  0xf95,  0xff2,
    // 0xf79,  0x183,  0xf61,
    // 0xfe7,  0xfdf,  0x197,
    // 0xfff,  0xffd,  0xffe,


    // 304,-71,26,
    // -33,325,-140,
    // -15,2,370,

435,
-48,
-25,
-152,
335,
-166,
-27,
-31,
448,
};

const _Sensor_BLC sc1346_blc_init =
{
    260, 260, 260, 260,
};

const _Sensor_AWB sc1346_awb_init =
{
//    r,  gr,  gb,   b
    .default_gain = {482, 256, 256, 508},
    .awb_min_gain = {256, 256, 256, 256},
    .awb_max_gain = {500, 256, 256, 738},

    .coarse_constraint = {
        .coarse_min_bg = 100,
        .coarse_lb_bg  = 140,
        .coarse_rt_bg  = 150,
        .coarse_max_bg = 200,
        .coarse_min_rg = 110,
        .coarse_lb_rg  = 160,
        .coarse_rt_rg  = 180,
        .coarse_max_rg = 255,
    },

   .constraint = {
    .section_num = 5,
    .color_temp = { 7500, 6500, 5000, 4000, 2856, 0, 0, 0 },
    .sec_line_slope = { 0.06470, 0.06074, 0.10263, 0.18188, 0.58356, 0, 0, 0 },
    .sec_line_offset = { 175.00192, 159.37488, 130.87244, 102.30433, -15.34502, 0, 0, 0 },
    .sec_line_sqrtk2add1 = { 0.99791, 0.99816, 0.99478, 0.98386, 0.86369, 0, 0, 0 },
    .center_line_slope = { -3.00000, -0.95455, -0.50000, -0.52273, 0.00000, 0, 0 },
    .center_line_offset = { 640.00000, 318.86365, 237.50000, 241.97726, 0.00000, 0, 0},
    .lower_line_slope = { -2.31478, -1.32273, -0.67459, -0.40942, 0.00000, 0, 0},
    .lower_line_offset = { 492.99161, 353.66870, 252.35878, 205.89957, 0.00000, 0, 0},
    .upper_line_slope = { -1.73609, -0.82671, -0.44091, -0.45581, 0.00000, 0, 0},
    .upper_line_offset = { 478.25418, 316.86832, 239.65529, 242.94041, 0.00000, 0, 0},
    .corner_limit = { 133.63817, 183.64816, 222.80952, 114.67744, 168.39987, 185.89722, 248.50296, 129.67108 },
},
};

// const _Sensor_AE sc1346_ae_init =
// {
//     .max_frame_length      = 880,
//     .curr_fps              = (uint32)(25.5*256),
//     .min_frame_vb          = 8,
//     .max_analog_gain       = 32<<8,
//     .min_analog_gain       =  1<<8,
//     .default_exposure_line = 872,
//     .max_exposure_line     = 872,
//     .min_exposure_line     = 10,
//     .row_time_us           = 26,
//     .expo_frame_interval   = 2,
//     .to_day_bv             = 1528,
//     .to_night_bv           = 369,
//     .dark_scene_target_lut = {50, 60},
//     .dark_scene_bv_lut     = {47, 800},
//     .hs_scene_limit_lut    = {60, 80},
//     .hs_scene_bv_lut       = {800, 3022},
//     .lowlight_lsb_bv_lut   = {47, 94, 195, 381, 781, 1636, 3225, 1e30},
//     .lowlight_lsb_gain_lut = {16, 16,  16,  16,  16,   16,   16,   16},  // u7.4

// };

const _Sensor_AE sc1346_ae_init =
{
    // ??????????? 0x320e/0x320f ?????0x0384 = 900
    .max_frame_length      = 1944,

    // ???? 25.5 fps?????25.5 * 256?
    // ?????????????????????
    .curr_fps              = (uint32)(12.5*256),

    // ??????????????
    .min_frame_vb          = 6,

    // ?????? 32 ??????32 << 8?
    .max_analog_gain       = 126<<8,

    // ?????? 1 ?
    .min_analog_gain       = 1<<8,

    // ?????? = ???????? 746 ?
    .default_exposure_line = 746,

    // ?????? = ?? - 6 = 900 - 6 = 894????????
    .max_exposure_line     = 1938,//1002,

    // ????????????
    .min_exposure_line     = 2,//10,

    // ?????????????
    // ??? = 1e6 / (?? × ??) = 1,000,000 / (25.5 × 900) ? 43.57 us
    // ?? 26 us ? 900 ?/25.5fps ???????
    .row_time_us           = 41,   // ?????????????

    // ????????? 2 ?????????
    .expo_frame_interval   = 2,

    // ?? BV???????? LUT ?????????
    .to_day_bv             = 1528,
    .to_night_bv           = 369,
    .dark_scene_target_lut = {34, 55},
    .dark_scene_bv_lut     = {47, 2000},
    .hs_scene_limit_lut    = {55,65},
    .hs_scene_bv_lut       = {2000, 20000},
    .lowlight_lsb_bv_lut   = {47, 94, 195, 381, 781, 1636, 3225, 1e30},
    .lowlight_lsb_gain_lut = {16, 16,  16,  16,  16,   16,   16,   16},  // u7.4
};

const _Sensor_CSUPP sc1346_csupp_init = {
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

const _Sensor_GAMMA_BV sc1346_gamma_map =
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

const _Sensor_CSC sc1346_csc_init =
{
    .rgb2yuv_gamut         = ISP_YUV_GAMUT_BT709,
    .rgb2yuv_range         = ISP_YUV_RANGE_NARROW,
    .yuv2rgb_in_gamut      = ISP_YUV_GAMUT_BT709,
    .yuv2rgb_in_range      = ISP_YUV_RANGE_NARROW,
    .yuv2rgb_out_gamut     = ISP_YUV_GAMUT_BT709,
    .yuv2rgb_out_range     = ISP_YUV_RANGE_NARROW,
    .y_gamma_alpha         = 0xff,
    .rgb_gamma_alpha       = 0xff,
    .gamma_alpha_map       = (void *)&sc1346_gamma_map,
};

const _Sensor_SHARP sc1346_sharp_init = {
    // .filt_alpha      = 128,
    // .shrink_thr      = 0,
    // .filt_clip_hi    = 127,
    // .filt_clip_lo    = 127,
    // .sp_thr2 		 = 64,
    // .sp_thr1 	 	 = 64,
    // .enha_clip_hi 	 = 64,
    // .enha_clip_lo	 = 127,

    // .e1 = 5, .e2 = 10, .e3 = 15,
    // .k0 = 0, .k1 = 128, .k2 = 128, .k3 = 128,
    // .y1 = 0,              // y1 = k0*e1
    // .y2 = 20,              // y2 = k1*e2 + (y1 - k1*e1) = k1 * (e2 - e1) + y1
    // .y3 = 40,              // y3 = k2*e3 + (y2 - k2*e2) = k2 * (e3 - e2) + y2

    // .filt_w11 = 7,  .filt_w12 = 9,  .filt_w13 = 10,
    // .filt_w21 = 9,  .filt_w22 = 12, .filt_w23 = 13,
    // .filt_w31 = 10, .filt_w32 = 13, .filt_w33 = 16,
    // .filt_type       = 1,
    // .filt_sbit       = 8,
    // .lpf_scale		 = 1,

    // .strength_lut    = { 64, 128},
    // .strength_bv_lut = {369,1528},


    // .filt_alpha      = 128,
    // .shrink_thr      = 4,
    // .filt_clip_hi    = 127,
    // .filt_clip_lo    = 127,
    // .sp_thr2 		 = 127,
    // .sp_thr1 	 	 = 127,
    // .enha_clip_hi 	 = 64,
    // .enha_clip_lo	 = 127,
    // .e1				 = 5,
	// .e2				 = 10,
	// .e3				 = 15,
    // .k0				 = 127,
	// .k1				 = 127,
	// .k2				 = 127,
	// .k3				 = 127,
    // .y1				 = 19,
	// .y2				 = 38,
	// .y3				 = 57,
    // .filt_w11		 = 0,
	// .filt_w12        = 0,
	// .filt_w13        = 0,
    // .filt_w21        = 0,
	// .filt_w22        = 28,
	// .filt_w23        = 28,
    // .filt_w31        = 0,
	// .filt_w32        = 28,
	// .filt_w33        = 32,
    // .filt_type       = 1,
    // .filt_sbit       = 8,
    // .lpf_scale		 = 3,
    // .strength_lut    = {64, 255},
    // .strength_bv_lut = {347, 7000},


    //        .filt_alpha      = 128,
    // .shrink_thr      = 8,
    // .filt_clip_hi    = 127,
    // .filt_clip_lo    = 127,
    // .sp_thr2 		 = 127,
    // .sp_thr1 	 	 = 127,
    // .enha_clip_hi 	 = 64,
    // .enha_clip_lo	 = 64,
    // .e1				 = 5,
	// .e2				 = 10,
	// .e3				 = 15,
    // .k0				 = 127,
	// .k1				 = 127,
	// .k2				 = 127,
	// .k3				 = 127,
    // .y1				 = 19,
	// .y2				 = 38,
	// .y3				 = 57,
    // .filt_w11		 = 10,
	// .filt_w12        = 10,
	// .filt_w13        = 10,
    // .filt_w21        = 10,
	// .filt_w22        = 10,
	// .filt_w23        = 10,
    // .filt_w31        = 10,
	// .filt_w32        = 10,
	// .filt_w33        = 16,
    // .filt_type       = 1,
    // .filt_sbit       = 8,
    // .lpf_scale		 = 1,
    // .strength_lut    = { 64, 128},
    // .strength_bv_lut = {369,1528},

    .filt_alpha = 128,
    .shrink_thr = 4,
    .filt_clip_hi = 127,
    .filt_clip_lo = 127,
    .sp_thr2 = 127,
    .sp_thr1 = 127,
    .enha_clip_hi = 64,
    .enha_clip_lo = 127,
    .e1 = 5,
    .e2 = 10,
    .e3 = 15,
    .k0 = 127,
    .k1 = 127,
    .k2 = 127,
    .k3 = 127,
    .y1 = 19,
    .y2 = 38,
    .y3 = 57,
    .filt_w11 = 0,
    .filt_w12 = 0,
    .filt_w13 = 0,
    .filt_w21 = 0,
    .filt_w22 = 28,
    .filt_w23 = 28,
    .filt_w31 = 0,
    .filt_w32 = 28,
    .filt_w33 = 32,
    .filt_type = 1,
    .filt_sbit = 8,
    .lpf_scale = 3,
    .strength_lut    = { 64, 120},
    .strength_bv_lut = {369,7000},


};



const _Sensor_YUVNR sc1346_yuvnr_init = {
    // 20,20,20,20,20,20,20,20,
    // 205,255,0,
        80,80,80,80,80,80,80,80,
    205,255,0,
};

const _Sensor_COLENH_BV sc1346_ce_map[BV2COLENH_ARRAY_NUM] = {
    {.bv =   29491, .hue = 0, .luma = 58, .contrast = 60, .saturation = 70},
    {.bv =    3534, .hue = 0, .luma = 58, .contrast = 60, .saturation = 70},
    {.bv =    4531, .hue = 0, .luma = 50, .contrast = 60, .saturation = 65},
    {.bv =     400, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =     222, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =     115, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =      57, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
    {.bv =      34, .hue = 0, .luma = 50, .contrast = 60, .saturation = 50},
};

const _Sensor_COLENH sc1346_colenh_init = {
    .yuv_range  = 0,
    .luma       = 58, // range: 0 ~ 100
    .contrast   = 53, // range: 0 ~ 100
    .saturation = 90, // range: 0 ~ 100
    .hue        = 0, // range: -180 ~ 180
    .ce_in_ofs_y   = 138,//128,
    .ce_in_ofs_cb  = 128,
    .ce_in_ofs_cr  = 128, // range: -128 ~ 128
    .ce_out_ofs_y  = 128,
    .ce_out_ofs_cb = 128,
    .ce_out_ofs_cr = 128, // range: -128 ~ 128
    .adj_by_bv_en  = 1,
    .bv2colenh_map = (void *)sc1346_ce_map,
};

const _Sensor_BV2NR sc1346_bv2nr_init[BV2RAWNR_ARRAY_NUM] = {
    //           bv, bnr_range_weight_index, bnr_invksigma, bnr_intensity_threshold, yuvnr_idx, csupp_idx. h264_3dnr_lev h264_3dnr_en
    {        10000,                     8 ,          511,                      63,         1,         0,              1,             1},    // 646lux
    {         5993,                     8 ,          400,                      63,         1,         0,              2,             1},    // 240lux
    {         3022,                     16,           250,                      63,         1,         1,             2,             1},    // 20lux
    {         1528,                     16,           271,                      63,         1,         1,             2,             1},    // 10lux
    {          754,                     16,           165,                      63,         2,         1,             2,             1},    // 5p03lux
    {          369,                     16,           135,                      63,         2,         1,             2,             1},    // 2p5lux
    {          184,                     20,           101,                      63,         3,         1,             2,             1},    // 1p25lux
    {           90,                     24,            62,                      63,         4,         2,             2,             1},    // 0p62lux
    {           46,                     26,            50,                      63,         4,         2,             2,             1},    // 0p31lux
    {           32,                     28,            40,                      63,         5,         2,             2,             1},    // 0p1lux
    {           24,                     31,            25,                      63,         5,         2,             2,             1},    // 0p01lux
};
const uint32 sc1346_lsc_tbl[] = {
0x0005415C, 0x0004B53E, 0x00046922, 0x00043D11, 0x0004350C, 0x00048D15, 0x00051932, 0x0005C959, 0x0000018D, 0x00052959, 0x0004A938, 0x0004591D, 0x0004250E, 0x00042D09, 0x00047911, 0x0005012D,
0x0005B954, 0x00000186, 0x00051556, 0x00049133, 0x0004451A, 0x0004210B, 0x00041D03, 0x0004650D, 0x0004E125, 0x00059950, 0x0000017F, 0x0005014F, 0x00048131, 0x00043116, 0x00040D06, 0x00040D03,
0x00045107, 0x0004CD20, 0x00058548, 0x00000178, 0x0004F14A, 0x0004712B, 0x00042110, 0x00040502, 0x00041102, 0x00044106, 0x0004B91E, 0x00057145, 0x00000173, 0x0004E548, 0x00045D26, 0x0004150D,
0x00040101, 0x00041102, 0x00043907, 0x0004B11C, 0x0005553F, 0x00000170, 0x0004E144, 0x00045124, 0x00040D0B, 0x00040104, 0x00041103, 0x00043907, 0x0004A919, 0x0005413D, 0x0000016E, 0x0004D943,
0x00045123, 0x00040908, 0x00040902, 0x00041102, 0x00043D08, 0x0004A518, 0x0005453B, 0x0000016E, 0x0004D144, 0x00045923, 0x00040D09, 0x00040503, 0x00041503, 0x00043909, 0x00049918, 0x0005453A,
0x0000016E, 0x0004D946, 0x00044D24, 0x00040D08, 0x00040501, 0x00041502, 0x00043D0A, 0x00049917, 0x00053D39, 0x0000016D, 0x0004DD47, 0x00045125, 0x00040D08, 0x00040501, 0x00041903, 0x00043909,
0x00049917, 0x00053D3A, 0x00000169, 0x0004F14A, 0x00045D27, 0x00040D0B, 0x00040501, 0x00041503, 0x00043507, 0x00049D18, 0x0005313A, 0x00000169, 0x0004F94D, 0x00045D2A, 0x0004190D, 0x00040D02,
0x00041904, 0x00043D08, 0x0004A11A, 0x0005453C, 0x0000016D, 0x00051D56, 0x00047D31, 0x00042D12, 0x00042107, 0x00042907, 0x0004550E, 0x0004BD20, 0x00056142, 0x0000016F, 0x0005295E, 0x00049937,
0x00044519, 0x0004210D, 0x00043109, 0x00045D10, 0x0004CD23, 0x00056D44, 0x00000172, 0x00054164, 0x0004AD3D, 0x0004551F, 0x00043910, 0x0004410E, 0x00046D16, 0x0004DD28, 0x00057D49, 0x00000175,
0x00056D6D, 0x0004D146, 0x00047127, 0x00045516, 0x00045D15, 0x0004991D, 0x0004F530, 0x00059551, 0x0000017B,

0x0005114F, 0x0004A535, 0x00046520, 0x00045116, 0x00046114, 0x0004B11E, 0x00052D3B, 0x0005D15E, 0x00000188, 0x0004F54A, 0x00048D2F, 0x0004511A, 0x0004350F, 0x0004450D, 0x00049518, 0x00051133,
0x0005B958, 0x00000182, 0x0004DD45, 0x00047929, 0x00043914, 0x0004210A, 0x00042D08, 0x00047512, 0x0004ED2B, 0x00059952, 0x0000017B, 0x0004CD3F, 0x00046925, 0x00042510, 0x00041105, 0x00042105,
0x0004610D, 0x0004D526, 0x00058D4D, 0x00000176, 0x0004BD3C, 0x0004511F, 0x0004190C, 0x00040D03, 0x00041D05, 0x0004590B, 0x0004CD23, 0x00057D47, 0x00000171, 0x0004B13D, 0x0004451D, 0x00040D09,
0x00040901, 0x00041D04, 0x0004550B, 0x0004BD21, 0x00055943, 0x0000016D, 0x0004B138, 0x00043D1C, 0x00040906, 0x00040902, 0x00042105, 0x00044D0C, 0x0004B91F, 0x00054D40, 0x0000016C, 0x0004AD38,
0x0004391B, 0x00040505, 0x00040D02, 0x00042104, 0x00044D0C, 0x0004B51E, 0x0005453E, 0x0000016B, 0x0004A938, 0x0004411B, 0x00040504, 0x00040902, 0x00042105, 0x0004490C, 0x0004A91C, 0x0005493C,
0x00000168, 0x0004AD39, 0x0004391C, 0x00040105, 0x00040D01, 0x00042104, 0x0004450D, 0x0004A11A, 0x0005413B, 0x00000167, 0x0004B53B, 0x00043D1C, 0x00040506, 0x00040901, 0x00042105, 0x0004410C,
0x00049D19, 0x0005353B, 0x00000165, 0x0004C13E, 0x00043D1E, 0x00040507, 0x00040900, 0x00041D04, 0x00043D0A, 0x00049D1A, 0x00052D3A, 0x00000163, 0x0004C540, 0x00044120, 0x00040906, 0x00040900,
0x00041D05, 0x0004410A, 0x0004A11A, 0x0005353A, 0x00000164, 0x0004CD41, 0x00044D22, 0x00040D08, 0x00040901, 0x00041D04, 0x00044109, 0x0004A11B, 0x00052D3A, 0x0000015F, 0x0004DD48, 0x00045D26,
0x00041D0D, 0x00041103, 0x00041904, 0x00044D0B, 0x0004AD1E, 0x0005313C, 0x00000160, 0x0004F14B, 0x0004752B, 0x00042D12, 0x00041D07, 0x00042907, 0x0004590E, 0x0004BD22, 0x00054140, 0x00000164,
0x00050951, 0x00048D31, 0x00044518, 0x00042D0C, 0x00043D0C, 0x00047114, 0x0004CD26, 0x00055D44, 0x00000169,

0x0005114E, 0x0004A535, 0x00046520, 0x00045115, 0x00045D13, 0x0004AD1E, 0x00052D39, 0x0005C95D, 0x00000186, 0x0004F549, 0x0004892E, 0x00044D19, 0x0004350E, 0x0004450C, 0x00049117, 0x00050D32,
0x0005B156, 0x00000180, 0x0004D943, 0x00047529, 0x00043915, 0x00041D09, 0x00042908, 0x00047111, 0x0004E929, 0x00059551, 0x00000179, 0x0004C53E, 0x00046124, 0x0004210F, 0x00041105, 0x00041D05,
0x00045D0C, 0x0004D125, 0x0005854B, 0x00000174, 0x0004B53A, 0x00044D1F, 0x0004150B, 0x00040902, 0x00041D04, 0x0004590B, 0x0004C923, 0x00057946, 0x00000170, 0x0004AD3C, 0x0004451D, 0x00040908,
0x00040501, 0x00041D04, 0x0004510B, 0x0004B920, 0x00055541, 0x0000016C, 0x0004A538, 0x00043D1B, 0x00040506, 0x00040902, 0x00042104, 0x00044D0B, 0x0004B51E, 0x0005453E, 0x0000016A, 0x0004A936,
0x0004351A, 0x00040505, 0x00040D02, 0x00042104, 0x0004450C, 0x0004AD1D, 0x0005413C, 0x00000169, 0x0004A537, 0x00043D1A, 0x00040104, 0x00040901, 0x00042104, 0x0004490C, 0x0004A91B, 0x00054139,
0x00000167, 0x0004A537, 0x0004351B, 0x00040105, 0x00040901, 0x00042105, 0x0004450D, 0x00049D1A, 0x00053D3A, 0x00000165, 0x0004AD3A, 0x0004391B, 0x00040505, 0x00040901, 0x00042104, 0x0004410B,
0x00049D18, 0x00052D39, 0x00000163, 0x0004B93D, 0x00043D1C, 0x00040507, 0x00040900, 0x00041D04, 0x00043D09, 0x00049919, 0x00052939, 0x00000161, 0x0004C13F, 0x00044120, 0x00040906, 0x00040D00,
0x00041D04, 0x0004410A, 0x00049D19, 0x00053139, 0x00000162, 0x0004C940, 0x00044922, 0x00041109, 0x00040D01, 0x00041D03, 0x00044109, 0x00049D1A, 0x00052939, 0x0000015D, 0x0004D948, 0x00046126,
0x00041D0D, 0x00041103, 0x00041D04, 0x00044D0B, 0x0004A91D, 0x0005293B, 0x0000015F, 0x0004ED4A, 0x0004752B, 0x00042D13, 0x00041D08, 0x00042908, 0x00045D0F, 0x0004B922, 0x00053D3F, 0x00000162,
0x00050951, 0x00048D31, 0x00044519, 0x0004310D, 0x00043D0C, 0x00047114, 0x0004CD27, 0x00055943, 0x00000168,

0x0004A92D, 0x00044D23, 0x0004250F, 0x00042507, 0x00043909, 0x00049115, 0x00050933, 0x00059D55, 0x00000183, 0x00048926, 0x0004391A, 0x00041109, 0x00040503, 0x00042D05, 0x00047911, 0x0004F92D,
0x00059D50, 0x0000017D, 0x00048925, 0x00044118, 0x00041108, 0x00040D01, 0x00041D04, 0x00046D10, 0x0004E529, 0x00059952, 0x0000017C, 0x00048D23, 0x0004411A, 0x00040909, 0x00040D00, 0x00042506,
0x00046D11, 0x0004E52A, 0x00059D50, 0x0000017D, 0x00048123, 0x00043D18, 0x00040106, 0x00040500, 0x00043106, 0x00047D13, 0x0004ED2C, 0x00059D50, 0x00000179, 0x00047D27, 0x00042D13, 0x0003FD04,
0x000404FF, 0x00043508, 0x00047913, 0x0004E52B, 0x0005794B, 0x00000175, 0x00047D23, 0x00042514, 0x0003F902, 0x00040900, 0x00043508, 0x00047113, 0x0004D929, 0x00056D48, 0x00000177, 0x00048122,
0x00042512, 0x0003F901, 0x00041103, 0x00043508, 0x00046912, 0x0004D926, 0x00056149, 0x00000174, 0x00047921, 0x00042912, 0x0003F4FF, 0x00040902, 0x00043107, 0x00046514, 0x0004C924, 0x00056944,
0x00000170, 0x00047923, 0x00042113, 0x0003FD01, 0x00041501, 0x00043907, 0x00046113, 0x0004BD20, 0x00056144, 0x0000016D, 0x00047D23, 0x00042514, 0x00040103, 0x00041101, 0x00043109, 0x00045112,
0x0004B11E, 0x00054541, 0x0000016C, 0x00048124, 0x00042516, 0x0003F902, 0x00040CFF, 0x00042D07, 0x0004490E, 0x0004AD1D, 0x00053940, 0x00000164, 0x00048927, 0x00042516, 0x0003F501, 0x00040900,
0x00042D07, 0x00044D0D, 0x0004AD1B, 0x0005393F, 0x00000167, 0x0004AD30, 0x00043D1E, 0x00040D06, 0x00042102, 0x0004390B, 0x00045D11, 0x0004B520, 0x00054540, 0x00000163, 0x0004BD35, 0x00045522,
0x00041D0A, 0x00042105, 0x0004350A, 0x00046110, 0x0004BD24, 0x00053942, 0x00000161, 0x0004C93A, 0x00046927, 0x00042D10, 0x00042507, 0x0004310C, 0x00046513, 0x0004CD27, 0x00054D44, 0x00000166,
0x0004E545, 0x00047D2E, 0x00044918, 0x0004350F, 0x00044D0D, 0x00047916, 0x0004DD2B, 0x00055D49, 0x0000016A,
};
const _Sensor_LSC          sc1346_lsc_init = {
    .p_lsc_tbl = (uint32 *)sc1346_lsc_tbl,
};

const _Sensor_LHS sc1346_lhs_map[] = {
    // region defination: lower -> center -> upper(direction: anticlockwise)
    // region_lower, region_center, region_upper, hue adjust value, saturation adjust value
    //   (9 bits)      (9 bits)       (9 bits)          (9 bits)           (8 bits)
    {            24,            52,           80,                0,                       0},  // magenta,          range: 28
    {            80,           109,          138,                0,                       0},  // red,              range: 29
    {           140,           171,          202,               +0,                     +00},  // yellow,           range: 31
    {           204,           232,          260,              + 0,                       0},  // green,            range: 28
    {           261,           289,          317,                0,                       0},  // cyan,             range: 28
    {           320,           351,           22,                0,                       0},  // blue,             range: 31
    {           109,           132,          156,                0,                       0},  // skin enhance,     range:
    {           160,           203,          247,               +00,                      0},  // green enhance(plants),    range:
    {           296,           318,          340,                0,                       00}   // blue enhance,     range:
};

const _Sensor_YGAMMA sc1346_ygamma_tbl[NUM_CURVES] = {
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
     .packed_lut = { //1655 李康权曲线 sc1346 夜视曲线
    0x00801000, 0x01003008, 0x01905410, 0x02107419, 0x02A09821, 0x0340BC2A, 0x03D0E034, 0x0481083D,
    0x05213448, 0x05E16052, 0x06A1905E, 0x0771C46A, 0x0851F877, 0x09423485, 0x0A427094, 0x0B42B0A4,
    0x0C52F0B4, 0x0D6334C5, 0x0E937CD6, 0x0FC3C8E9, 0x10F414FC, 0x1234650F, 0x1384B923, 0x14D50D38,
    0x1635614D, 0x1785B563, 0x18E60D78, 0x1A56698E, 0x1BC6C5A5, 0x1D4721BC, 0x1EC781D4, 0x2057E5EC,
    0x21D84605, 0x2368AA1D, 0x24E90A36, 0x26696A4E, 0x27D9CA66, 0x294A267D, 0x2AAA7E94, 0x2BFAD2AA,
    0x2D2B26BF, 0x2E5B72D2, 0x2F7BBAE5, 0x307BFEF7, 0x318C4307, 0x327C8318, 0x337CBF27, 0x345CFB37,
    0x353D3345, 0x361D6B53, 0x36FDA361, 0x37CDD76F, 0x389E0F7C, 0x396E4389, 0x3A3E7396, 0x3AFEA7A3,
    0x3BCEDBAF, 0x3C9F0FBC, 0x3D6F3FC9, 0x3E1F73D6, 0x3EAF9BE1, 0x3F2FBBEA, 0x3F9FDBF2, 0x3FFFF3F9, }},
    {
//      .bv = 1000,
//      .packed_lut = {
// 0x00500C00, 0x00A02005, 0x0100340A, 0x01604C10, 0x01C06416, 0x0230801C, 0x02B09C23, 0x0340C02B,
// 0x03E0E434, 0x0491103E, 0x05613C49, 0x06417456, 0x0731AC64, 0x0841F073, 0x09523084, 0x0A727895,
// 0x0B92C0A7, 0x0CC30CB9, 0x0DF358CC, 0x0F33A4DF, 0x1073F4F3, 0x11B44507, 0x1304991B, 0x1454E930,
// 0x15A53D45, 0x16F5915A, 0x1845E56F, 0x19963984, 0x1AE68D99, 0x1C26E1AE, 0x1D7735C2, 0x1EC785D7,
// 0x2007D9EC, 0x21482A00, 0x22787A14, 0x23B8C627, 0x24D9123B, 0x26095A4D, 0x2719A260, 0x2839EA71,
// 0x294A3283, 0x2A5A7694, 0x2B6ABAA5, 0x2C7AFEB6, 0x2D8B42C7, 0x2E8B82D8, 0x2F9BC2E8, 0x309C06F9,
// 0x319C4709, 0x329C8719, 0x339CC729, 0x348D0739, 0x358D4348, 0x368D8358, 0x377DBF68, 0x386DFF77,
// 0x396E3B86, 0x3A5E7796, 0x3B4EB7A5, 0x3C3EF3B4, 0x3D3F2FC3, 0x3E2F6BD3, 0x3F1FA7E2, 0x3FFFE3F1, }},
//     {
//      .bv = 1500,
//      .packed_lut = {
// 0x00500C00, 0x00A02005, 0x0100340A, 0x01604C10, 0x01C06416, 0x0230801C, 0x02B09C23, 0x0340C02B,
// 0x03E0E434, 0x0491103E, 0x05613C49, 0x06417456, 0x0731AC64, 0x0841F073, 0x09523084, 0x0A727895,
// 0x0B92C0A7, 0x0CC30CB9, 0x0DF358CC, 0x0F33A4DF, 0x1073F4F3, 0x11B44507, 0x1304991B, 0x1454E930,
// 0x15A53D45, 0x16F5915A, 0x1845E56F, 0x19963984, 0x1AE68D99, 0x1C26E1AE, 0x1D7735C2, 0x1EC785D7,
// 0x2007D9EC, 0x21482A00, 0x22787A14, 0x23B8C627, 0x24D9123B, 0x26095A4D, 0x2719A260, 0x2839EA71,
// 0x294A3283, 0x2A5A7694, 0x2B6ABAA5, 0x2C7AFEB6, 0x2D8B42C7, 0x2E8B82D8, 0x2F9BC2E8, 0x309C06F9,
// 0x319C4709, 0x329C8719, 0x339CC729, 0x348D0739, 0x358D4348, 0x368D8358, 0x377DBF68, 0x386DFF77,
// 0x396E3B86, 0x3A5E7796, 0x3B4EB7A5, 0x3C3EF3B4, 0x3D3F2FC3, 0x3E2F6BD3, 0x3F1FA7E2, 0x3FFFE3F1,  }},
//     {// ?????BV=500
//      .bv = 2000,
//      .packed_lut = {
// 0x00500C00, 0x00A02005, 0x0100340A, 0x01604C10, 0x01C06416, 0x0230801C, 0x02B09C23, 0x0340C02B,
// 0x03E0E434, 0x0491103E, 0x05613C49, 0x06417456, 0x0731AC64, 0x0841F073, 0x09523084, 0x0A727895,
// 0x0B92C0A7, 0x0CC30CB9, 0x0DF358CC, 0x0F33A4DF, 0x1073F4F3, 0x11B44507, 0x1304991B, 0x1454E930,
// 0x15A53D45, 0x16F5915A, 0x1845E56F, 0x19963984, 0x1AE68D99, 0x1C26E1AE, 0x1D7735C2, 0x1EC785D7,
// 0x2007D9EC, 0x21482A00, 0x22787A14, 0x23B8C627, 0x24D9123B, 0x26095A4D, 0x2719A260, 0x2839EA71,
// 0x294A3283, 0x2A5A7694, 0x2B6ABAA5, 0x2C7AFEB6, 0x2D8B42C7, 0x2E8B82D8, 0x2F9BC2E8, 0x309C06F9,
// 0x319C4709, 0x329C8719, 0x339CC729, 0x348D0739, 0x358D4348, 0x368D8358, 0x377DBF68, 0x386DFF77,
// 0x396E3B86, 0x3A5E7796, 0x3B4EB7A5, 0x3C3EF3B4, 0x3D3F2FC3, 0x3E2F6BD3, 0x3F1FA7E2, 0x3FFFE3F1,  }}
     .bv = 1000,
     .packed_lut = {
0x00500C00, 0x00B02005, 0x0110380B, 0x01805011, 0x01F06C18, 0x02808C1F, 0x0310B028, 0x03D0DC31,
0x04A10C3D, 0x05B1484A, 0x06D1905B, 0x0821E06D, 0x09A23882, 0x0B32989A, 0x0CD300B3, 0x0E8368CD,
0x1043D8E8, 0x12044904, 0x13D4B920, 0x15A52D3D, 0x1775A15A, 0x19561977, 0x1B569595, 0x1D7719B5,
0x1F97A1D7, 0x219825F9, 0x2368A219, 0x25091236, 0x26596A50, 0x2779BA65, 0x288A0277, 0x298A4288,
0x2A6A7E98, 0x2B3AB6A6, 0x2C1AEAB3, 0x2CEB1EC1, 0x2DBB52CE, 0x2E9B8ADB, 0x2F6BBEE9, 0x303BF6F6,
0x310C2B03, 0x31DC5F10, 0x329C8F1D, 0x336CC329, 0x341CF336, 0x34DD1F41, 0x358D4B4D, 0x363D7758,
0x36DDA363, 0x378DCF6D, 0x382DF778, 0x38CE1F82, 0x396E478C, 0x3A0E6F96, 0x3AAE97A0, 0x3B4EBFAA,
0x3BEEE7B4, 0x3C7F0BBE, 0x3D1F33C7, 0x3DAF57D1, 0x3E3F7FDA, 0x3EDFA3E3, 0x3F6FCBED, 0x3FFFEFF6, }},
    {
     .bv = 1500,
     .packed_lut = {
0x00500C00, 0x00B02005, 0x0110380B, 0x01805011, 0x01F06C18, 0x02808C1F, 0x0310B028, 0x03D0DC31,
0x04A10C3D, 0x05B1484A, 0x06D1905B, 0x0821E06D, 0x09A23882, 0x0B32989A, 0x0CD300B3, 0x0E8368CD,
0x1043D8E8, 0x12044904, 0x13D4B920, 0x15A52D3D, 0x1775A15A, 0x19561977, 0x1B569595, 0x1D7719B5,
0x1F97A1D7, 0x219825F9, 0x2368A219, 0x25091236, 0x26596A50, 0x2779BA65, 0x288A0277, 0x298A4288,
0x2A6A7E98, 0x2B3AB6A6, 0x2C1AEAB3, 0x2CEB1EC1, 0x2DBB52CE, 0x2E9B8ADB, 0x2F6BBEE9, 0x303BF6F6,
0x310C2B03, 0x31DC5F10, 0x329C8F1D, 0x336CC329, 0x341CF336, 0x34DD1F41, 0x358D4B4D, 0x363D7758,
0x36DDA363, 0x378DCF6D, 0x382DF778, 0x38CE1F82, 0x396E478C, 0x3A0E6F96, 0x3AAE97A0, 0x3B4EBFAA,
0x3BEEE7B4, 0x3C7F0BBE, 0x3D1F33C7, 0x3DAF57D1, 0x3E3F7FDA, 0x3EDFA3E3, 0x3F6FCBED, 0x3FFFEFF6, }},
    {// ?????BV=500
     .bv = 2000,
     .packed_lut = {
0x00500C00, 0x00B02005, 0x0110380B, 0x01805011, 0x01F06C18, 0x02808C1F, 0x0310B028, 0x03D0DC31,
0x04A10C3D, 0x05B1484A, 0x06D1905B, 0x0821E06D, 0x09A23882, 0x0B32989A, 0x0CD300B3, 0x0E8368CD,
0x1043D8E8, 0x12044904, 0x13D4B920, 0x15A52D3D, 0x1775A15A, 0x19561977, 0x1B569595, 0x1D7719B5,
0x1F97A1D7, 0x219825F9, 0x2368A219, 0x25091236, 0x26596A50, 0x2779BA65, 0x288A0277, 0x298A4288,
0x2A6A7E98, 0x2B3AB6A6, 0x2C1AEAB3, 0x2CEB1EC1, 0x2DBB52CE, 0x2E9B8ADB, 0x2F6BBEE9, 0x303BF6F6,
0x310C2B03, 0x31DC5F10, 0x329C8F1D, 0x336CC329, 0x341CF336, 0x34DD1F41, 0x358D4B4D, 0x363D7758,
0x36DDA363, 0x378DCF6D, 0x382DF778, 0x38CE1F82, 0x396E478C, 0x3A0E6F96, 0x3AAE97A0, 0x3B4EBFAA,
0x3BEEE7B4, 0x3C7F0BBE, 0x3D1F33C7, 0x3DAF57D1, 0x3E3F7FDA, 0x3EDFA3E3, 0x3F6FCBED, 0x3FFFEFF6, }}
};

// void SC1346_ae_adjust(struct isp_exposure_opt *p_cfg)
// {
//     uint8  i;
//     uint8  analog_gain        = p_cfg->analog_gain>>8;
//     uint8  gain_segment[]     = {1, 2, 4, 8, 16, 32, 64};
//     uint8  gain_value[]       = {0, 0x08, 0x09, 0x0b, 0x0f, 0x1f};
//     uint8  sensor_gain_part1  = 0;
//     uint16 sensor_gain_part2  = 0;
//     uint32 exposure_line      = p_cfg->exposure_line;
//     uint8  *addr              = (uint8 *)p_cfg->data.addr;

//     for(i = 0; i < (sizeof(gain_segment) / sizeof(gain_segment[0])); i++){
//         if(analog_gain < gain_segment[i+1]){
//             sensor_gain_part1 = i;
//             break;
//         }
//     }
//     sensor_gain_part2 = (((p_cfg->analog_gain >> sensor_gain_part1) - 256) >> 3) << 2;

//     i = 0;
//     addr[i++] = 0x3e;
//     addr[i++] = 0x02;
//     addr[i++] = (exposure_line << 4) & 0xf0;
//     addr[i++] = 0x3e;
//     addr[i++] = 0x01;
//     addr[i++] = (exposure_line >> 4) & 0xff;
//     addr[i++] = 0x3e;
//     addr[i++] = 0x00;
//     addr[i++] = (exposure_line >> 12) & 0x0f;

//     addr[i++] = 0x3e;
//     addr[i++] = 0x09;
//     addr[i++] = gain_value[sensor_gain_part1];
//     addr[i++] = 0x3e;
//     addr[i++] = 0x06;
//     addr[i++] = 0;
//     addr[i++] = 0x3e;
//     addr[i++] = 0x07;
//     addr[i++] = 0x80 + sensor_gain_part2;
//     p_cfg->data.size = i;
//     p_cfg->cmd_len   = 3;
// }


volatile uint32 sc1346_analog_gain = 0;
// void SC1346_ae_adjust(struct isp_exposure_opt *p_cfg)
// {
//     uint8  i;
//     uint8  analog_gain        = p_cfg->analog_gain>>8;          // ??????????
//     uint8  gain_segment[]     = {1, 2, 4, 8, 16, 32, 128};      // ?????????
//     uint8  gain_value[]       = {0, 0x08, 0x09, 0x0b, 0x0f, 0x1f}; // ???????????2-4
//     uint8  sensor_gain_part1  = 0;
//     uint16 sensor_gain_part2  = 0;
//     uint32 exposure_line      = p_cfg->exposure_line;
//     uint8  *addr              = (uint8 *)p_cfg->data.addr;

//     /* ?????????????????? */
//     for(i = 0; i < (sizeof(gain_segment) / sizeof(gain_segment[0])); i++){
//         if(analog_gain < gain_segment[i+1]){
//             sensor_gain_part1 = i;
//             break;
//         }
//     }
//     sc1346_analog_gain = p_cfg->analog_gain;
//     /* ??????????????????????????? */
//     sensor_gain_part2 = (((p_cfg->analog_gain >> sensor_gain_part1) - 256) >> 3) << 2;

//     i = 0;
//     /* ???????????????2-2??? 0x3e00/0x3e01/0x3e02? */
//     addr[i++] = 0x3e;
//     addr[i++] = 0x02;
//     addr[i++] = (exposure_line << 4) & 0xf0;      // ?4??? 0x3e02[7:4]

//     addr[i++] = 0x3e;
//     addr[i++] = 0x01;
//     addr[i++] = (exposure_line >> 4) & 0xff;      // ??8??? 0x3e01[7:0]

//     addr[i++] = 0x3e;
//     addr[i++] = 0x00;
//     addr[i++] = (exposure_line >> 12) & 0x0f;     // ?4??? 0x3e00[3:0]

//     /* ???????????? 0x3e09? */
//     addr[i++] = 0x3e;
//     addr[i++] = 0x09;
//     addr[i++] = gain_value[sensor_gain_part1];

//     /* ???????????? 0x3e06 ? 0x3e07? */
//     addr[i++] = 0x3e;
//     addr[i++] = 0x06;
//     addr[i++] = 0;                                 // DIG GAIN ??? 0???????
//     addr[i++] = 0x3e;
//     addr[i++] = 0x07;
//     addr[i++] = 0x80 + sensor_gain_part2;          // DIG FINE GAIN ?? 0x80(1?) + ???

//     p_cfg->data.size = i;
//     p_cfg->cmd_len   = 3;
// }


void SC1346_ae_adjust(struct isp_exposure_opt *p_cfg)
{

    uint8_t  i;
    uint32_t  total_gain_int     = p_cfg->analog_gain;
    uint8_t  gain_segment[]     = {   1, 	2,    4,    8,   16,   32};
    uint8_t  gain_value[]       = {0x00, 0x08, 0x09, 0x0B, 0x0F, 0x1F};
    uint8_t  ana_idx            = 5;
    uint16_t fine_val           = 0;
    uint8_t  dig_gain           = 0;
    uint32_t exposure_line      = p_cfg->exposure_line;
    uint8_t  *addr              = (uint8_t *)p_cfg->data.addr;

    for (i = 0; i < 6; i++) {
        if (total_gain_int >= (gain_segment[i]* 256)) {
            ana_idx = i;
        } else {
            break;
        }
    }

    uint32_t ana_base = gain_segment[ana_idx] * 256;
    (total_gain_int >= 16384)?(dig_gain=1):(dig_gain=0);

	fine_val = (((p_cfg->analog_gain >> (ana_idx + dig_gain)) - 256) >> 3) << 2;

    i = 0;
    addr[i++] = 0x3e;
    addr[i++] = 0x02;
    addr[i++] = (exposure_line << 4) & 0xf0;
    addr[i++] = 0x3e;
    addr[i++] = 0x01;
    addr[i++] = (exposure_line >> 4) & 0xff;
    addr[i++] = 0x3e;
    addr[i++] = 0x00;
    addr[i++] = (exposure_line >> 12) & 0x0f;


    //os_printf("======> exposure_line :%x  %x %x %x \n",exposure_line,((exposure_line << 4) & 0xf0),((exposure_line >> 4) & 0xff),((exposure_line >> 12) & 0x0f));

    addr[i++] = 0x3e;
    addr[i++] = 0x09;
    addr[i++] = gain_value[ana_idx];
    addr[i++] = 0x3e;
    addr[i++] = 0x06;
    addr[i++] = dig_gain;
    addr[i++] = 0x3e;
    addr[i++] = 0x07;
    addr[i++] = 0x80 + fine_val;

    p_cfg->data.size = i;
    p_cfg->cmd_len   = 3;

}

void sc1346_fps_opt(struct isp_sensor_opt *p_opt)
{
    uint8  *addr        = (uint8 *)p_opt->data.addr;
    uint8  index        = 0;
    addr[index++]       = 0x32;
    addr[index++]       = 0x0e;
    addr[index++]       = p_opt->curr_length >> 8;
    addr[index++]       = 0x32;
    addr[index++]       = 0x0f;
    addr[index++]       = p_opt->curr_length & 0xff;
    p_opt->data.size    = index;
    p_opt->cmd_len      = 2+1;
}

// void sc1346_img_opt(struct isp_sensor_opt *p_opt)
// {
//     uint8  *addr = (uint8 *)p_opt->data.addr;
//     uint8  index = 0;
//     addr[index++] = 0x32;
//     addr[index++] = 0x21;
//     addr[index++] = p_opt->reverse_en*2 + p_opt->mirror_en;

//     p_opt->data.size = index;
//     p_opt->cmd_len   = 2+1;
// }

void sc1346_img_opt(struct isp_sensor_opt *p_opt)
{
    uint8  *addr = (uint8 *)p_opt->data.addr;
    uint8  index = 0;
    uint8  reg_value = 0x00;
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

const _Sensor_DPC sc1346_dpc_init =
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


const _Sensor_ISP_Init sc1346_isp_init =
{
    .type         = ISP_INPUT_DAT_SRC_MIPI0,
    .pixel_h      = 720,
    .pixel_w      = 1280,
    .bayer_patten = ISP_BAYER_FORMAT_BGGR,
    .input_format = ISP_INPUT_DAT_FORMAT_RAW10,
    .adjust_func  = (isp_ae_func     )SC1346_ae_adjust,
    .p_blc        = (_Sensor_BLC    *)&sc1346_blc_init,
    .p_ccm        = (_Sensor_CCM    *)&sc1346_ccm_init,
    .p_awb        = (_Sensor_AWB    *)&sc1346_awb_init,
    .p_ae         = (_Sensor_AE     *)&sc1346_ae_init,
    .p_dpc        = (_Sensor_DPC    *)&sc1346_dpc_init,
    .p_csc        = (_Sensor_CSC    *)&sc1346_csc_init,
    .p_csupp      = (_Sensor_CSUPP  *)&sc1346_csupp_init,
    .p_sharp      = (_Sensor_SHARP  *)&sc1346_sharp_init,
    .p_yuvnr      = (_Sensor_YUVNR  *)&sc1346_yuvnr_init,
    .p_colenh     = (_Sensor_COLENH *)&sc1346_colenh_init,
    .p_bv2nr      = (_Sensor_BV2NR  *)sc1346_bv2nr_init,
    .p_lsc        = (_Sensor_LSC    *)&sc1346_lsc_init,
    .p_lhs        = (_Sensor_LHS    *)sc1346_lhs_map,
    .p_ygamma     = (_Sensor_YGAMMA *)sc1346_ygamma_tbl,
    .fps_opt      = (sensor_fps_opt  )sc1346_fps_opt,
    .img_opt      = (sensor_img_opt  )sc1346_img_opt,
};

SENSOR_OP_SECTION const _Sensor_Adpt_ sc1346_cmd=
{
	.typ = 1, //YUV
	.pixelw = 1280,
	.pixelh= 720,
	.hsyn = 1,
	.vsyn = 1,
	.rduline = 0,//
	.rawwide = 1,//10bit
	.colrarray = 2,//0:_RGRG_ 1:_GRGR_,2:_BGBG_,3:_GBGB_
	.init = (uint8 *)sc1346InitTable,
    .init_len = sizeof(sc1346InitTable),
	.rotate_adapt = {0},
    .mipi_lane_num = 1,

	.hvb_adapt = {0x80,0x0a,0x80,0x0a},
	.mclk = 24000000,
	.p_fun_adapt = {NULL,NULL,NULL},
	.sensor_isp = (_Sensor_ISP_Init *)&sc1346_isp_init,
};

const _Sensor_Ident_ sc1346_init=
{
	0x4d,0x60,0x61,0x02,0x01,0x3108
};

#endif

#else


#endif

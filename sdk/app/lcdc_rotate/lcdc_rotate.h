#ifndef __LCDC_ROTATE_H
#define __LCDC_ROTATE_H

#include "sys_config.h"
#include "typesdef.h"
#include "hal/lcdc.h"
#include "lib/multimedia/msi.h"

struct lcdc_rotate_config {
    const char *name;
    uint16_t    output_w;
    uint16_t    output_h;
    uint16_t    capture_w;
    uint16_t    capture_h;
    uint8_t     rotate_mode;
    uint8_t     input_stype;
    uint8_t     output_stype;
};

#define LCDC_ROTATE_LINE_NUM        32
#define LCDC_ROTATE_CAPTURE_BUF_NUM 1

#define LCDC_ROTATE_DCLK            100000000
#define LCDC_ROTATE_LANE_NUM        2
#define LCDC_ROTATE_VSA             2
#define LCDC_ROTATE_VBP             2
#define LCDC_ROTATE_VFP             10
#define LCDC_ROTATE_HSA             2
#define LCDC_ROTATE_HBP             8
#define LCDC_ROTATE_HFP             96

struct msi *lcdc_rotate_msi_init(const struct lcdc_rotate_config *config);

#endif

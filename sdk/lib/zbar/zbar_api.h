#ifndef ZBAR_API_H
#define ZBAR_API_H
#include "zbar.h"
#include "zbar_alloc.h"
//二维码解析结果回调
typedef void (*zbar_stream_result_cb)(zbar_symbol_type_t type, const char *data,void *userdata);
void zbar_stream_decode_yuv(const uint8_t *yuv, uint32_t w, uint32_t h, zbar_stream_result_cb cb,void *userdata);
#endif
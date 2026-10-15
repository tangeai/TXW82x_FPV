/*Copyright (C) 2008-2009  Timothy B. Terriberry (tterribe@xiph.org)
  You can redistribute this library and/or modify it under the terms of the
   GNU Lesser General Public License as published by the Free Software
   Foundation; either version 2.1 of the License, or (at your option) any later
   version.*/
#if !defined(_qrcode_binarize_H)
# define _qrcode_binarize_H (1)

void qr_image_cross_masking_median_filter(unsigned char *_img,
 int _width,int _height);

void qr_wiener_filter(unsigned char *_img,int _width,int _height);

/*Binarizes a grayscale image.*/
unsigned char *qr_binarize(const unsigned char *_img,int _width,int _height);

/*Binarizes a grayscale image into caller-provided buffers.
  If _mask is non-NULL and _mask_size >= w*h, it is used; otherwise w*h
  bytes are allocated internally and returned.
  If _col_sums is non-NULL and _col_sums_size >= w, it is used; otherwise
  w unsigned ints are allocated internally as a scratch buffer (and freed
  before returning if allocated internally).
  Callers on bare-metal targets should pass both to avoid any allocation
  during decoding.
  On return, callers must free the returned mask if and only if the _mask
  argument passed was NULL (or too small, in which case internal
  allocation was used).*/
unsigned char *qr_binarize_ex(const unsigned char *_img,
                              unsigned char *_mask,
                              unsigned _mask_size,
                              unsigned *_col_sums,
                              unsigned _col_sums_size,
                              int _width,int _height);

#endif

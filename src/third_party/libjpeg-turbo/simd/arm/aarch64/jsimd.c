/*
 * jsimd_arm64.c
 *
 * Copyright 2009 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright (C) 2011, Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2009-2011, 2013-2014, 2016, 2018, 2020, D. R. Commander.
 * Copyright (C) 2015-2016, 2018, Matthieu Darbois.
 * Copyright (C) 2020, Arm Limited.
 *
 * Based on the x86 SIMD extension for IJG JPEG library,
 * Copyright (C) 1999-2006, MIYASAKA Masaru.
 * For conditions of distribution and use, see copyright notice in jsimdext.inc
 *
 * This file contains the interface between the "normal" portions
 * of the library and the SIMD implementations when running on a
 * 64-bit Arm architecture.
 */

#define JPEG_INTERNALS
#include "../../../jinclude.h"
#include "../../../jpeglib.h"
#include "../../../jsimd.h"
#include "../../../jdct.h"
#include "../../../jsimddct.h"
#include "../../jsimd.h"
#include "jconfigint.h"

#ifdef STARBOARD
#include "starboard/client_porting/poem/strings_poem.h"
#include "starboard/client_porting/poem/string_poem.h"
#include "starboard/configuration.h"
#include "starboard/client_porting/poem/stdio_poem.h"
#include "starboard/character.h"
#else
#include "starboard/client_porting/poem/stdio_poem.h"
#endif

#define JSIMD_FASTLD3  1
#define JSIMD_FASTST3  2
#define JSIMD_FASTTBL  4

static unsigned int simd_support = ~0;
static unsigned int simd_huffman = 1;
static unsigned int simd_features = JSIMD_FASTLD3 | JSIMD_FASTST3 |
                                    JSIMD_FASTTBL;

#if defined(__linux__) || defined(ANDROID) || defined(__ANDROID__)
#if !defined(STARBOARD)

#define SOMEWHAT_SANE_PROC_CPUINFO_SIZE_LIMIT  (1024 * 1024)

LOCAL(int)
check_cpuinfo(char *buffer, const char *field, char *value)
{
  char *p;

  if (*value == 0)
    return 0;
  if (strncmp(buffer, field, strlen(field)) != 0)
    return 0;
  buffer += strlen(field);
  while (isspace(*buffer))
    buffer++;

  /* Check if 'value' is present in the buffer as a separate word */
  while ((p = strstr(buffer, value))) {
    if (p > buffer && !isspace(*(p - 1))) {
      buffer++;
      continue;
    }
    p += strlen(value);
    if (*p != 0 && !isspace(*p)) {
      buffer++;
      continue;
    }
    return 1;
  }
  return 0;
}

LOCAL(int)
parse_proc_cpuinfo(int bufsize)
{
  char *buffer = (char *)malloc(bufsize);
  FILE *fd;

  if (!buffer)
    return 0;

  fd = fopen("/proc/cpuinfo", "r");
  if (fd) {
    while (fgets(buffer, bufsize, fd)) {
      if (!strchr(buffer, '\n') && !feof(fd)) {
        /* "impossible" happened - insufficient size of the buffer! */
        fclose(fd);
        free(buffer);
        return 0;
      }
      if (check_cpuinfo(buffer, "CPU part", "0xd03") ||
          check_cpuinfo(buffer, "CPU part", "0xd07"))
        /* The Cortex-A53 has a slow tbl implementation.  We can gain a few
           percent speedup by disabling the use of that instruction.  The
           speedup on Cortex-A57 is more subtle but still measurable. */
        simd_features &= ~JSIMD_FASTTBL;
      else if (check_cpuinfo(buffer, "CPU part", "0x0a1"))
        /* The SIMD version of Huffman encoding is slower than the C version on
           Cavium ThunderX.  Also, ld3 and st3 are abyssmally slow on that
           CPU. */
        simd_huffman = simd_features = 0;
    }
    fclose(fd);
  }
  free(buffer);
  return 1;
}

#endif
#endif

/*
 * Check what SIMD accelerations are supported.
 *
 * FIXME: This code is racy under a multi-threaded environment.
 */

/*
 * Armv8 architectures support Neon extensions by default.
 * It is no longer optional as it was with Armv7.
 */


LOCAL(void)
init_simd(void)
{
#ifndef NO_GETENV
  char *env = NULL;
#endif
#if defined(__linux__) || defined(ANDROID) || defined(__ANDROID__)
#if !defined(STARBOARD)
  int bufsize = 1024; /* an initial guess for the line buffer size limit */
#endif
#endif

  if (simd_support != ~0U)
    return;

  simd_support = 0;

  simd_support |= JSIMD_NEON;
#if defined(__linux__) || defined(ANDROID) || defined(__ANDROID__)
#if !defined(STARBOARD)
  while (!parse_proc_cpuinfo(bufsize)) {
    bufsize *= 2;
    if (bufsize > SOMEWHAT_SANE_PROC_CPUINFO_SIZE_LIMIT)
      break;
  }
#endif
#endif

#ifndef NO_GETENV
  /* Force different settings through environment variables */
  env = getenv("JSIMD_FORCENEON");
  if ((env != NULL) && (strcmp(env, "1") == 0))
    simd_support = JSIMD_NEON;
  env = getenv("JSIMD_FORCENONE");
  if ((env != NULL) && (strcmp(env, "1") == 0))
    simd_support = 0;
  env = getenv("JSIMD_NOHUFFENC");
  if ((env != NULL) && (strcmp(env, "1") == 0))
    simd_huffman = 0;
  env = getenv("JSIMD_FASTLD3");
  if ((env != NULL) && (strcmp(env, "1") == 0))
    simd_features |= JSIMD_FASTLD3;
  if ((env != NULL) && (strcmp(env, "0") == 0))
    simd_features &= ~JSIMD_FASTLD3;
  env = getenv("JSIMD_FASTST3");
  if ((env != NULL) && (strcmp(env, "1") == 0))
    simd_features |= JSIMD_FASTST3;
  if ((env != NULL) && (strcmp(env, "0") == 0))
    simd_features &= ~JSIMD_FASTST3;
#endif
}

GLOBAL(int)
jsimd_can_rgb_ycc(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if ((RGB_PIXELSIZE != 3) && (RGB_PIXELSIZE != 4))
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_rgb_gray(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if ((RGB_PIXELSIZE != 3) && (RGB_PIXELSIZE != 4))
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_ycc_rgb(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if ((RGB_PIXELSIZE != 3) && (RGB_PIXELSIZE != 4))
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_ycc_rgb565(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_rgb_ycc_convert(j_compress_ptr cinfo, JSAMPARRAY input_buf,
                      JSAMPIMAGE output_buf, JDIMENSION output_row,
                      int num_rows)
{
  void (*neonfct) (JDIMENSION, JSAMPARRAY, JSAMPIMAGE, JDIMENSION, int);

  switch (cinfo->in_color_space) {
  case JCS_EXT_RGB:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTLD3)
#endif
      neonfct = jsimd_extrgb_ycc_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_extrgb_ycc_convert_neon_slowld3;
#endif
    break;
  case JCS_EXT_RGBX:
  case JCS_EXT_RGBA:
    neonfct = jsimd_extrgbx_ycc_convert_neon;
    break;
  case JCS_EXT_BGR:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTLD3)
#endif
      neonfct = jsimd_extbgr_ycc_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_extbgr_ycc_convert_neon_slowld3;
#endif
    break;
  case JCS_EXT_BGRX:
  case JCS_EXT_BGRA:
    neonfct = jsimd_extbgrx_ycc_convert_neon;
    break;
  case JCS_EXT_XBGR:
  case JCS_EXT_ABGR:
    neonfct = jsimd_extxbgr_ycc_convert_neon;
    break;
  case JCS_EXT_XRGB:
  case JCS_EXT_ARGB:
    neonfct = jsimd_extxrgb_ycc_convert_neon;
    break;
  default:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTLD3)
#endif
      neonfct = jsimd_extrgb_ycc_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_extrgb_ycc_convert_neon_slowld3;
#endif
    break;
  }

  neonfct(cinfo->image_width, input_buf, output_buf, output_row, num_rows);
}

GLOBAL(void)
jsimd_rgb_gray_convert(j_compress_ptr cinfo, JSAMPARRAY input_buf,
                       JSAMPIMAGE output_buf, JDIMENSION output_row,
                       int num_rows)
{
  void (*neonfct) (JDIMENSION, JSAMPARRAY, JSAMPIMAGE, JDIMENSION, int);

  switch (cinfo->in_color_space) {
  case JCS_EXT_RGB:
    neonfct = jsimd_extrgb_gray_convert_neon;
    break;
  case JCS_EXT_RGBX:
  case JCS_EXT_RGBA:
    neonfct = jsimd_extrgbx_gray_convert_neon;
    break;
  case JCS_EXT_BGR:
    neonfct = jsimd_extbgr_gray_convert_neon;
    break;
  case JCS_EXT_BGRX:
  case JCS_EXT_BGRA:
    neonfct = jsimd_extbgrx_gray_convert_neon;
    break;
  case JCS_EXT_XBGR:
  case JCS_EXT_ABGR:
    neonfct = jsimd_extxbgr_gray_convert_neon;
    break;
  case JCS_EXT_XRGB:
  case JCS_EXT_ARGB:
    neonfct = jsimd_extxrgb_gray_convert_neon;
    break;
  default:
    neonfct = jsimd_extrgb_gray_convert_neon;
    break;
  }

  neonfct(cinfo->image_width, input_buf, output_buf, output_row, num_rows);
}

GLOBAL(void)
jsimd_ycc_rgb_convert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
                      JDIMENSION input_row, JSAMPARRAY output_buf,
                      int num_rows)
{
  void (*neonfct) (JDIMENSION, JSAMPIMAGE, JDIMENSION, JSAMPARRAY, int);

  switch (cinfo->out_color_space) {
  case JCS_EXT_RGB:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTST3)
#endif
      neonfct = jsimd_ycc_extrgb_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_ycc_extrgb_convert_neon_slowst3;
#endif
    break;
  case JCS_EXT_RGBX:
  case JCS_EXT_RGBA:
    neonfct = jsimd_ycc_extrgbx_convert_neon;
    break;
  case JCS_EXT_BGR:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTST3)
#endif
      neonfct = jsimd_ycc_extbgr_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_ycc_extbgr_convert_neon_slowst3;
#endif
    break;
  case JCS_EXT_BGRX:
  case JCS_EXT_BGRA:
    neonfct = jsimd_ycc_extbgrx_convert_neon;
    break;
  case JCS_EXT_XBGR:
  case JCS_EXT_ABGR:
    neonfct = jsimd_ycc_extxbgr_convert_neon;
    break;
  case JCS_EXT_XRGB:
  case JCS_EXT_ARGB:
    neonfct = jsimd_ycc_extxrgb_convert_neon;
    break;
  default:
#ifndef NEON_INTRINSICS
    if (simd_features & JSIMD_FASTST3)
#endif
      neonfct = jsimd_ycc_extrgb_convert_neon;
#ifndef NEON_INTRINSICS
    else
      neonfct = jsimd_ycc_extrgb_convert_neon_slowst3;
#endif
    break;
  }

  neonfct(cinfo->output_width, input_buf, input_row, output_buf, num_rows);
}

GLOBAL(void)
jsimd_ycc_rgb565_convert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
                         JDIMENSION input_row, JSAMPARRAY output_buf,
                         int num_rows)
{
  jsimd_ycc_rgb565_convert_neon(cinfo->output_width, input_buf, input_row,
                                output_buf, num_rows);
}

GLOBAL(int)
jsimd_can_h2v2_downsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_h2v1_downsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_h2v2_downsample(j_compress_ptr cinfo, jpeg_component_info *compptr,
                      JSAMPARRAY input_data, JSAMPARRAY output_data)
{
  jsimd_h2v2_downsample_neon(cinfo->image_width, cinfo->max_v_samp_factor,
                             compptr->v_samp_factor, compptr->width_in_blocks,
                             input_data, output_data);
}

GLOBAL(void)
jsimd_h2v1_downsample(j_compress_ptr cinfo, jpeg_component_info *compptr,
                      JSAMPARRAY input_data, JSAMPARRAY output_data)
{
  jsimd_h2v1_downsample_neon(cinfo->image_width, cinfo->max_v_samp_factor,
                             compptr->v_samp_factor, compptr->width_in_blocks,
                             input_data, output_data);
}

GLOBAL(int)
jsimd_can_h2v2_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_h2v1_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_h2v2_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                    JSAMPARRAY input_data, JSAMPARRAY *output_data_ptr)
{
  jsimd_h2v2_upsample_neon(cinfo->max_v_samp_factor, cinfo->output_width,
                           input_data, output_data_ptr);
}

GLOBAL(void)
jsimd_h2v1_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                    JSAMPARRAY input_data, JSAMPARRAY *output_data_ptr)
{
  jsimd_h2v1_upsample_neon(cinfo->max_v_samp_factor, cinfo->output_width,
                           input_data, output_data_ptr);
}

GLOBAL(int)
jsimd_can_h2v2_fancy_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_h2v1_fancy_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_h1v2_fancy_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_h2v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                          JSAMPARRAY input_data, JSAMPARRAY *output_data_ptr)
{
  jsimd_h2v2_fancy_upsample_neon(cinfo->max_v_samp_factor,
                                 compptr->downsampled_width, input_data,
                                 output_data_ptr);
}

GLOBAL(void)
jsimd_h2v1_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                          JSAMPARRAY input_data, JSAMPARRAY *output_data_ptr)
{
  jsimd_h2v1_fancy_upsample_neon(cinfo->max_v_samp_factor,
                                 compptr->downsampled_width, input_data,
                                 output_data_ptr);
}

GLOBAL(void)
jsimd_h1v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                          JSAMPARRAY input_data, JSAMPARRAY *output_data_ptr)
{
  jsimd_h1v2_fancy_upsample_neon(cinfo->max_v_samp_factor,
                                 compptr->downsampled_width, input_data,
                                 output_data_ptr);
}

GLOBAL(int)
jsimd_can_h2v2_merged_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_h2v1_merged_upsample(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_h2v2_merged_upsample(j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
                           JDIMENSION in_row_group_ctr, JSAMPARRAY output_buf)
{
  void (*neonfct) (JDIMENSION, JSAMPIMAGE, JDIMENSION, JSAMPARRAY);

  switch (cinfo->out_color_space) {
    case JCS_EXT_RGB:
      neonfct = jsimd_h2v2_extrgb_merged_upsample_neon;
      break;
    case JCS_EXT_RGBX:
    case JCS_EXT_RGBA:
      neonfct = jsimd_h2v2_extrgbx_merged_upsample_neon;
      break;
    case JCS_EXT_BGR:
      neonfct = jsimd_h2v2_extbgr_merged_upsample_neon;
      break;
    case JCS_EXT_BGRX:
    case JCS_EXT_BGRA:
      neonfct = jsimd_h2v2_extbgrx_merged_upsample_neon;
      break;
    case JCS_EXT_XBGR:
    case JCS_EXT_ABGR:
      neonfct = jsimd_h2v2_extxbgr_merged_upsample_neon;
      break;
    case JCS_EXT_XRGB:
    case JCS_EXT_ARGB:
      neonfct = jsimd_h2v2_extxrgb_merged_upsample_neon;
      break;
    default:
      neonfct = jsimd_h2v2_extrgb_merged_upsample_neon;
      break;
  }

  neonfct(cinfo->output_width, input_buf, in_row_group_ctr, output_buf);
}

GLOBAL(void)
jsimd_h2v1_merged_upsample(j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
                           JDIMENSION in_row_group_ctr, JSAMPARRAY output_buf)
{
  void (*neonfct) (JDIMENSION, JSAMPIMAGE, JDIMENSION, JSAMPARRAY);

  switch (cinfo->out_color_space) {
    case JCS_EXT_RGB:
      neonfct = jsimd_h2v1_extrgb_merged_upsample_neon;
      break;
    case JCS_EXT_RGBX:
    case JCS_EXT_RGBA:
      neonfct = jsimd_h2v1_extrgbx_merged_upsample_neon;
      break;
    case JCS_EXT_BGR:
      neonfct = jsimd_h2v1_extbgr_merged_upsample_neon;
      break;
    case JCS_EXT_BGRX:
    case JCS_EXT_BGRA:
      neonfct = jsimd_h2v1_extbgrx_merged_upsample_neon;
      break;
    case JCS_EXT_XBGR:
    case JCS_EXT_ABGR:
      neonfct = jsimd_h2v1_extxbgr_merged_upsample_neon;
      break;
    case JCS_EXT_XRGB:
    case JCS_EXT_ARGB:
      neonfct = jsimd_h2v1_extxrgb_merged_upsample_neon;
      break;
    default:
      neonfct = jsimd_h2v1_extrgb_merged_upsample_neon;
      break;
  }

  neonfct(cinfo->output_width, input_buf, in_row_group_ctr, output_buf);
}

GLOBAL(int)
jsimd_can_convsamp(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (sizeof(DCTELEM) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_convsamp_float(void)
{
  return 0;
}

GLOBAL(void)
jsimd_convsamp(JSAMPARRAY sample_data, JDIMENSION start_col,
               DCTELEM *workspace)
{
  jsimd_convsamp_neon(sample_data, start_col, workspace);
}

GLOBAL(void)
jsimd_convsamp_float(JSAMPARRAY sample_data, JDIMENSION start_col,
                     FAST_FLOAT *workspace)
{
}

GLOBAL(int)
jsimd_can_fdct_islow(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(DCTELEM) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_fdct_ifast(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(DCTELEM) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_fdct_float(void)
{
  return 0;
}

GLOBAL(void)
jsimd_fdct_islow(DCTELEM *data)
{
  jsimd_fdct_islow_neon(data);
}

GLOBAL(void)
jsimd_fdct_ifast(DCTELEM *data)
{
  jsimd_fdct_ifast_neon(data);
}

GLOBAL(void)
jsimd_fdct_float(FAST_FLOAT *data)
{
}

GLOBAL(int)
jsimd_can_quantize(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (sizeof(DCTELEM) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_quantize_float(void)
{
  return 0;
}

GLOBAL(void)
jsimd_quantize(JCOEFPTR coef_block, DCTELEM *divisors, DCTELEM *workspace)
{
  jsimd_quantize_neon(coef_block, divisors, workspace);
}

GLOBAL(void)
jsimd_quantize_float(JCOEFPTR coef_block, FAST_FLOAT *divisors,
                     FAST_FLOAT *workspace)
{
}

GLOBAL(int)
jsimd_can_idct_2x2(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (sizeof(ISLOW_MULT_TYPE) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_idct_4x4(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (sizeof(ISLOW_MULT_TYPE) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_idct_2x2(j_decompress_ptr cinfo, jpeg_component_info *compptr,
               JCOEFPTR coef_block, JSAMPARRAY output_buf,
               JDIMENSION output_col)
{
  jsimd_idct_2x2_neon(compptr->dct_table, coef_block, output_buf, output_col);
}

GLOBAL(void)
jsimd_idct_4x4(j_decompress_ptr cinfo, jpeg_component_info *compptr,
               JCOEFPTR coef_block, JSAMPARRAY output_buf,
               JDIMENSION output_col)
{
  jsimd_idct_4x4_neon(compptr->dct_table, coef_block, output_buf, output_col);
}

GLOBAL(int)
jsimd_can_idct_islow(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (sizeof(ISLOW_MULT_TYPE) != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_idct_ifast(void)
{
  init_simd();

  /* The code is optimised for these values only */
  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (BITS_IN_JSAMPLE != 8)
    return 0;
  if (sizeof(JDIMENSION) != 4)
    return 0;
  if (sizeof(IFAST_MULT_TYPE) != 2)
    return 0;
  if (IFAST_SCALE_BITS != 2)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_can_idct_float(void)
{
  return 0;
}

GLOBAL(void)
jsimd_idct_islow(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                 JCOEFPTR coef_block, JSAMPARRAY output_buf,
                 JDIMENSION output_col)
{
  jsimd_idct_islow_neon(compptr->dct_table, coef_block, output_buf,
                        output_col);
}

GLOBAL(void)
jsimd_idct_ifast(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                 JCOEFPTR coef_block, JSAMPARRAY output_buf,
                 JDIMENSION output_col)
{
  jsimd_idct_ifast_neon(compptr->dct_table, coef_block, output_buf,
                        output_col);
}

GLOBAL(void)
jsimd_idct_float(j_decompress_ptr cinfo, jpeg_component_info *compptr,
                 JCOEFPTR coef_block, JSAMPARRAY output_buf,
                 JDIMENSION output_col)
{
}

GLOBAL(int)
jsimd_can_huff_encode_one_block(void)
{
  init_simd();

  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;

  if (simd_support & JSIMD_NEON && simd_huffman)
    return 1;

  return 0;
}

GLOBAL(JOCTET *)
jsimd_huff_encode_one_block(void *state, JOCTET *buffer, JCOEFPTR block,
                            int last_dc_val, c_derived_tbl *dctbl,
                            c_derived_tbl *actbl)
{
#ifndef NEON_INTRINSICS
  if (simd_features & JSIMD_FASTTBL)
#endif
    return jsimd_huff_encode_one_block_neon(state, buffer, block, last_dc_val,
                                            dctbl, actbl);
#ifndef NEON_INTRINSICS
  else
    return jsimd_huff_encode_one_block_neon_slowtbl(state, buffer, block,
                                                    last_dc_val, dctbl, actbl);
#endif
}

GLOBAL(int)
jsimd_can_encode_mcu_AC_first_prepare(void)
{
  init_simd();

  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (SIZEOF_SIZE_T != 8)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(void)
jsimd_encode_mcu_AC_first_prepare(const JCOEF *block,
                                  const int *jpeg_natural_order_start, int Sl,
                                  int Al, JCOEF *values, size_t *zerobits)
{
  jsimd_encode_mcu_AC_first_prepare_neon(block, jpeg_natural_order_start,
                                         Sl, Al, values, zerobits);
}

GLOBAL(int)
jsimd_can_encode_mcu_AC_refine_prepare(void)
{
  init_simd();

  if (DCTSIZE != 8)
    return 0;
  if (sizeof(JCOEF) != 2)
    return 0;
  if (SIZEOF_SIZE_T != 8)
    return 0;

  if (simd_support & JSIMD_NEON)
    return 1;

  return 0;
}

GLOBAL(int)
jsimd_encode_mcu_AC_refine_prepare(const JCOEF *block,
                                   const int *jpeg_natural_order_start, int Sl,
                                   int Al, JCOEF *absvalues, size_t *bits)
{
  return jsimd_encode_mcu_AC_refine_prepare_neon(block,
                                                 jpeg_natural_order_start,
                                                 Sl, Al, absvalues, bits);
}

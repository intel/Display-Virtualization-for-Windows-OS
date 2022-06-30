/*===========================================================================
; edid_parser.h
;----------------------------------------------------------------------------
; * Copyright © 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;--------------------------------------------------------------------------*/

#ifndef __EDID_PARSER_H__
#define __EDID_PARSER_H__

#include "edidshared.h"

// Generic EDID Macros
#define MASK											0xFF
#define EDID_MASK(a)									(MASK >> a)			// Mask to retrieve values from Input data
#define EDID_SIZE										256					// Size of Input EDID Hex Dump
#define EDID_HEADER_SIZE								8					// Size of Valid EDID Header
#define OUTPUT_MODELIST_SIZE							32
#define EDID_FIRST_BLOCK_END							127					// End Index of EDID First 128 Bytes
#define EDID_SECOND_BLOCK_START							128					// Start Index of EDID Second 128 Bytes

// Timing Bitmaps
#define TIMING_BITMAP_MODELIST_SIZE						17					// Size of Timing Bitmap Modelist
#define TIMING_BITMAP_START								35					// Start index of Timing Bitmap
#define TIMING_BITMAP_END								37					// End index of Timing Bitmap

// Standard Modes
#define STANDARD_MODE_START								38					// Start index of Standard Modes
#define STANDARD_MODE_END								53					// End index of Standard Modes
#define ASPECT_RATIO_16_10								0x0					// Aspect Ratio 16:10
#define ASPECT_RATIO_4_3								0x1					// Aspect Ratio 4:3
#define ASPECT_RATIO_5_4								0x2					// Aspect Ratio 5:4
#define ASPECT_RATIO_16_9								0x3					// Aspect Ratio 16:9

// CEA Video Blocks
#define CEA_MODELIST_SIZE								154					// Size of CEA_Modelist
#define CEA_VIDEO_BLOCK_IDENTIFIER						0x2					// Video Data Block Identifier i.e. Video Tag
#define CEA_DATA_BLOCKS_END_INDEX						130					// Data block end position index
#define CEA_DATA_FIRST_BLOCK_INDEX						132					// CEA_Data first block start index
#define CEA_MODELIST_FIRST_BLOCK						127					// End Index of CEA Modelist first half
#define CEA_MODELIST_SECOND_BLOCK						193					// Start Index of the CEA Modelist second half

// DTD Additional Standard Timings
#define DTD_START										54					// DTD Data Blocks Starting Index
#define DTD_END											126					// DTD Data Blocks Ending Index
#define DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE	44					// Size of Additional Standard Timing Modelist
#define DTD_ADDITIONAL_STANDARD_HEADER_SIZE				5					// Header Size of additional standard display modes
#define DTD_ADDITIONAL_STANDARD_START_BYTE				6					// DTD Data Block data byte start position
#define DTD_ADDITIONAL_STANDARD_TOTAL_BYTES				5					// DTD Data Block total number of resolution bytes

// Header Data
const unsigned char g_edid_header[EDID_HEADER_SIZE] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
const unsigned char g_additional_standard_header[DTD_ADDITIONAL_STANDARD_HEADER_SIZE] = { 0x00, 0x00, 0x00, 0xf7, 0x00 };

// APIs
static inline int validate_edid_header(unsigned char*);
static inline int validate_edid_checksum(unsigned char*);
static inline void get_timing_bitmaps_modes(unsigned char*, struct output_modelist*);
static inline void get_standard_modes(unsigned char*, struct output_modelist*);
static inline void get_additional_standard_display_modes(unsigned char*, struct output_modelist*);
static inline void get_cea_modes(unsigned char*, struct output_modelist*);

// Timing Bitmap from EDID 1.4 Spec
struct edid_qemu_modes timing_bitmap_modelist[TIMING_BITMAP_MODELIST_SIZE] = {
					{800, 600, 60},
					{800, 600, 56},
					{640, 480, 75},
					{640, 480, 72},
					{640, 480, 67}, // Apple Macintosh II
					{640, 480, 60}, // VGA
					{720, 400, 88}, // XGA
					{720, 400, 70}, // VGA
					{1280, 1024, 75},
					{1024, 768, 75},
					{1024, 768, 70},
					{1024, 768, 60},
					{1024, 768, 87}, // interlaced 1024x768i
					{832, 624, 75}, // Apple Macintosh II
					{800, 600, 75},
					{800, 600, 72},
					{1152, 870, 75} // Apple Macintosh II
};

// Additional Standard Timings from EDID 1.4 Spec
struct edid_qemu_modes additional_standard_timing_modelist[DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE] = {
					{1152, 864, 85},
					{1024, 768, 85},
					{800, 600, 85},
					{848, 480, 60},
					{640, 480, 85},
					{720, 400, 85},
					{640, 400, 85},
					{640, 350, 85},
					{1280, 1024, 85},
					{1280, 1024, 60},
					{1280, 960, 85},
					{1280, 960, 60},
					{1280, 768, 85},
					{1280, 768, 75},
					{1280, 768, 60},
					{1280, 768, 60}, // CVT-RB
					{1440, 1050, 75},
					{1440, 1050, 60},
					{1440, 1050, 60}, // CVT-RB
					{1440, 900, 85},
					{1440, 900, 75},
					{1440, 900, 60}, // CVT-RB
					{1280, 768, 60},
					{1360, 768, 60}, // CVT-RB
					{1600, 1200, 70},
					{1600, 1200, 65},
					{1600, 1200, 60},
					{1680, 1050, 85},
					{1680, 1050, 75},
					{1680, 1050, 60},
					{1680, 1050, 60}, // CVT-RB
					{1440, 1050, 85},
					{1920, 1200, 60},
					{1920, 1200, 60}, // CVT-RB
					{1856, 1392, 75},
					{1856, 1392, 60},
					{1792, 1344, 75},
					{1792, 1344, 60},
					{1600, 1200, 85},
					{1600, 1200, 70},
					{1920, 1440, 75},
					{1920, 1440, 60},
					{1920, 1200, 85},
					{1920, 1200, 75},
};

// Resolutions from EIA/CEA-861
struct edid_qemu_modes cea_modelist[CEA_MODELIST_SIZE] = {
					{640, 480, 59.94},
					{720, 480, 59.94},
					{720, 480, 59.94},
					{1280, 720, 60},
					{1920, 540, 60},
					{1440, 240, 59.94},
					{1440, 240, 59.94},
					{1440, 240, 59.826},
					{1440, 240, 59.826},
					{2880, 240, 59.94},
					{2880, 240, 59.94},
					{2880, 240, 60},
					{2880, 240, 60},
					{1440, 480, 59.94},
					{1440, 480, 59.94},
					{1920, 1080, 60},
					{720, 576, 50},
					{720, 576, 50},
					{1280, 720, 50},
					{1920, 540, 50},
					{1440, 288, 50},
					{1440, 288, 50},
					{1440, 288, 50},
					{1440, 288, 50},
					{2880, 288, 50},
					{2880, 288, 50},
					{2880, 288, 50},
					{2880, 288, 50},
					{1440, 576, 50},
					{1440, 576, 50},
					{1920, 1080, 50},
					{1920, 1080, 23.98},
					{1920, 1080, 25},
					{1920, 1080, 29.97},
					{2880, 240, 59.94},
					{2880, 240, 59.94},
					{2880, 576, 50},
					{2880, 576, 50},
					{1920, 540, 50},
					{1920, 540, 100},
					{1280, 720, 100},
					{720, 576, 100},
					{720, 576, 100},
					{1440, 576, 100},
					{1440, 576, 100},
					{1920, 540, 119.88},
					{1280, 720, 119.88},
					{720, 576, 119.88},
					{720, 576, 119.88},
					{1440, 576, 119.88},
					{1440, 576, 119.88},
					{720, 576, 200},
					{720, 576, 200},
					{1440, 288, 200},
					{1440, 288, 200},
					{720, 480, 239.76},
					{720, 480, 239.76},
					{1440, 240, 239.76},
					{1440, 240, 239.76},
					{1280, 720, 23.98},
					{1280, 720, 25},
					{1280, 720, 29.97},
					{1920, 1080, 119.88},
					{1920, 1080, 100},
					{1280, 720, 23.98},
					{1280, 720, 25},
					{1280, 720, 29.97},
					{1280, 720, 50},
					{1650, 750, 60},
					{1280, 720, 100},
					{1280, 720, 119.88},
					{1920, 1080, 23.98},
					{1920, 1080, 25},
					{1920, 1080, 29.97},
					{1920, 1080, 50},
					{1920, 1080, 60},
					{1920, 1080, 100},
					{1920, 1080, 119.88},
					{1680, 720, 23.98},
					{1680, 720, 25},
					{1680, 720, 29.97},
					{1680, 720, 50},
					{1680, 720, 60},
					{1680, 720, 100},
					{1680, 720, 119.88},
					{2560, 1080, 23.98},
					{2560, 1080, 25},
					{2560, 1080, 29.97},
					{2560, 1080, 50},
					{2560, 1080, 60},
					{2560, 1080, 100},
					{2560, 1080, 119.88},
					{3840, 2160, 23.98},
					{3840, 2160, 25},
					{3840, 2160, 29.97},
					{3840, 2160, 50},
					{3840, 2160, 60},
					{4096, 2160, 23.98},
					{4096, 2160, 25},
					{4096, 2160, 29.97},
					{4096, 2160, 50},
					{4096, 2160, 60},
					{3840, 2160, 23.98},
					{3840, 2160, 25},
					{3840, 2160, 29.97},
					{3840, 2160, 50},
					{3840, 2160, 60},
					{1280, 720, 47.96},
					{1280, 720, 47.96},
					{1680, 720, 47.96},
					{1920, 1080, 47.96},
					{1920, 1080, 47.96},
					{2560, 1080, 47.96},
					{3840, 2160, 47.96},
					{4096, 2160, 47.96},
					{3840, 2160, 47.96},
					{3840, 2160, 100},
					{3840, 2160, 119.88},
					{3840, 2160, 100},
					{3840, 2160, 119.88},
					{5120, 2160, 23.98},
					{5120, 2160, 25},
					{5120, 2160, 29.97},
					{5120, 2160, 47.96},
					{5120, 2160, 50},
					{5120, 2160, 60},
					{5120, 2160, 100},
					{5120, 2160, 119.88},
					{7680, 4320, 23.98},
					{7680, 4320, 25},
					{7680, 4320, 29.97},
					{7680, 4320, 47.96},
					{7680, 4320, 50},
					{7680, 4320, 60},
					{7680, 4320, 100},
					{7680, 4320, 119.88},
					{7680, 4320, 23.98},
					{7680, 4320, 25},
					{7680, 4320, 29.97},
					{7680, 4320, 47.96},
					{7680, 4320, 50},
					{7680, 4320, 60},
					{7680, 4320, 100},
					{7680, 4320, 119.88},
					{10240, 4320, 23.98},
					{10240, 4320, 25},
					{10240, 4320, 29.97},
					{10240, 4320, 47.96},
					{10240, 4320, 50},
					{10240, 4320, 60},
					{10240, 4320, 100},
					{10240, 4320, 119.88},
					{4096, 2160, 100},
					{4096, 2160, 119.88}
};

#endif //__EDID_PARSER_H__

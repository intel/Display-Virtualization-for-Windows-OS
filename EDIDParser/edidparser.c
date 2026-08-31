/*===========================================================================
; edid_parser.c
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;--------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edidparser.h"

static inline int add_mode(struct output_modelist *l, unsigned w, unsigned h, double r)
{
	if (!l)
		return -1;
	/* Ensure we don't overflow the output array */
	if (l->modelist_size >= OUTPUT_MODELIST_SIZE)
		return -1;
	l->modelist[l->modelist_size].width = w;
	l->modelist[l->modelist_size].height = h;
	l->modelist[l->modelist_size].refresh_rate = r;
	l->modelist_size++;
	return 0;
}

/*******************************************************************************
 *
 * Description
 *
 * parse_edid_data - First checks the validity of the hex_input. If valid, then
 * parses all the resolution modelist from it.
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * int - 0 = SUCCESS, -1 = ERROR
 *
 ******************************************************************************/
int parse_edid_data(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	int func_result = -1;
	int index = 0;

	func_result = validate_edid_header(edid_data);
	if (func_result != 0) {
		return func_result;
	}
	func_result = -1;
	func_result = validate_edid_checksum(edid_data);
	if (func_result != 0) {
		return func_result;
	}

	get_timing_bitmaps_modes(edid_data, kmd_modelist);
	get_standard_modes(edid_data, kmd_modelist);
	get_cea_modes(edid_data, kmd_modelist);
	get_detailed_timing_descriptor_modes(edid_data, kmd_modelist);
	get_additional_standard_display_modes(edid_data, kmd_modelist);
	return 0;
}

/*******************************************************************************
 *
 * Description
 *
 * validate_edid_header - Ensures that the header of the input is valid. If
 * invalid, then the parser exits.
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 *
 * Return val
 * int - 0 = SUCCESS, -1/1 = ERROR
 *
 ******************************************************************************/
int validate_edid_header(unsigned char *edid_data) { return memcmp(edid_data, g_edid_header, EDID_HEADER_SIZE); }

/*******************************************************************************
 *
 * Description
 *
 * validate_edid_checksum - Ensures that the checksum of the input is valid. If
 * invalid, then the parser exits.
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 *
 * Return val
 * int - 0 = SUCCESS, -1 = ERROR
 *
 ******************************************************************************/
int validate_edid_checksum(unsigned char *edid_data)
{
	int index = 0;
	int chksum = 0;

	for (index = 0; index <= EDID_FIRST_BLOCK_END; index++) {
		chksum += edid_data[index];
	}
	chksum %= EDID_SIZE;
	if (chksum != 0) {
		return -1;
	}
	for (index = EDID_SECOND_BLOCK_START; index < EDID_SIZE; index++) {
		chksum += edid_data[index];
	}
	chksum %= EDID_SIZE;
	if (chksum != 0) {
		return -1;
	}
	return 0;
}

/*******************************************************************************
 *
 * Description
 *
 * get_cea_modes - parses the VIC codes which contain the variety of
 * resolutions ranging from small basic resolutions to higher ones.
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * void
 *
 ******************************************************************************/
void get_cea_modes(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	int i = 0;
	int vic_number = 0;
	int length_of_current_block = 0;
	int cea_block_end_index = 0;
	int start_index_of_block = 0;
	int video_block_start_index = 0;
	int video_block_length = 0;
	int video_block_end = 0;
	int cea_end = 0;

	/* BLOCK_LENGTH */
	length_of_current_block = edid_data[CEA_DATA_FIRST_BLOCK_INDEX] & EDID_MASK(0x3);
	cea_block_end_index = edid_data[CEA_DATA_BLOCKS_END_INDEX];
	start_index_of_block = CEA_DATA_FIRST_BLOCK_INDEX;

	/* Guard against malformed EDID: clamp loop bound to stay within the 256-byte array */
	cea_end = CEA_DATA_FIRST_BLOCK_INDEX + cea_block_end_index;
	if (cea_end > (EDID_SIZE - 1))
		cea_end = EDID_SIZE - 1;

	while (start_index_of_block < cea_end) {
		/* VIDEO_BLOCK_TAG */
		if (((edid_data[start_index_of_block] & (~EDID_MASK(0x3))) >> 5) == CEA_VIDEO_BLOCK_IDENTIFIER) {

			video_block_start_index = start_index_of_block;
			/* VIDEO_BLOCK_LENGTH */
			video_block_length = edid_data[start_index_of_block] & EDID_MASK(0x3);
			video_block_end = video_block_start_index + video_block_length;

			/* Guard against malformed EDID: clamp inner loop bound within the 256-byte array */
			if (video_block_end >= EDID_SIZE)
				video_block_end = EDID_SIZE - 1;

			for (i = video_block_start_index + 1; i <= video_block_end; i++) {
				/* for VICs 1-64, only 7-bits are used. */
				vic_number = edid_data[i] & EDID_MASK(0x1);
				if (vic_number > 64) {
					vic_number = edid_data[i];
				}

				/* Reject zero VIC which would index -1 */
				if (vic_number == 0)
					continue;

				/* VIC Number from 1 to 127 */
				if (vic_number >= 1 && vic_number <= CEA_MODELIST_FIRST_BLOCK) {
					int idx = vic_number - 1;
					if (idx >= 0 && idx < CEA_MODELIST_SIZE) {
						add_mode(kmd_modelist, cea_modelist[idx].width, cea_modelist[idx].height,
									   cea_modelist[idx].refresh_rate);
					}
				}
				/* VIC Number from 193 to 219 (mapped as vic_number - 65) */
				else if (vic_number >= CEA_MODELIST_SECOND_BLOCK) {
					int idx = vic_number - 65;
					if (idx >= 0 && idx < CEA_MODELIST_SIZE) {
						add_mode(kmd_modelist, cea_modelist[idx].width, cea_modelist[idx].height,
									   cea_modelist[idx].refresh_rate);
					}
				}
			}
			break;
		}
		/* BLOCK_LENGTH */
		length_of_current_block = edid_data[start_index_of_block] & EDID_MASK(0x3);
		start_index_of_block = start_index_of_block + length_of_current_block + 1;
	}
}

/******************************************************************************
 *
 * Description
 *
 * get_standard_modes - parses the standard display resolutions which are
 * larger than timing_bitmap resolutions.
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * void
 *
 ******************************************************************************/
void get_standard_modes(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	int index = 0;
	int width = -1;
	int height = -1;
	int aspect_ratio = -1;
	double refresh_rate = -1;

	for (index = STANDARD_MODE_START; index <= STANDARD_MODE_END; index += 2) {
		if ((edid_data[index] == 0x1) && (edid_data[index + 1] == 0x1)) {
			continue;
		}
		width = (edid_data[index] + 31) * 8;
		aspect_ratio = edid_data[index + 1];
		aspect_ratio >>= 6;

		switch (aspect_ratio) {
		case ASPECT_RATIO_16_10:
			height = width * 10 / 16;
			break;
		case ASPECT_RATIO_4_3:
			height = width * 3 / 4;
			break;
		case ASPECT_RATIO_5_4:
			height = width * 4 / 5;
			break;
		case ASPECT_RATIO_16_9:
			height = width * 9 / 16;
			break;
		default:
			continue;
		}
		/* To calculate REFRESH_RATE */
		refresh_rate = (double)(edid_data[index + 1] & EDID_MASK(0x2)) + 60;

		add_mode(kmd_modelist, (unsigned)width, (unsigned)height, refresh_rate);
	}
}

/*******************************************************************************
 *
 * Description
 *
 * get_timing_bitmaps_modes - parses the basic resolutions of the display device
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * void
 *
 ******************************************************************************/
void get_timing_bitmaps_modes(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	int i = 0;
	int bit_index = 0;
	unsigned char tb_byte = 0;
	int tb_lookup = 0;

	for (i = TIMING_BITMAP_START; i <= TIMING_BITMAP_END; i++) {
		if (tb_lookup > (TIMING_BITMAP_MODELIST_SIZE - 1)) {
			break;
		}
		tb_byte = edid_data[i];
		if (i < TIMING_BITMAP_END) {
			/* Traverse from the 1st bit to the 8th bit of timing_bitmap byte */
			for (bit_index = 0x0; bit_index <= 0x7; bit_index++) {
				if ((tb_byte & 0x1) == 1) {
					add_mode(kmd_modelist, timing_bitmap_modelist[tb_lookup].width,
								   timing_bitmap_modelist[tb_lookup].height,
								   timing_bitmap_modelist[tb_lookup].refresh_rate);
				}
				if ((tb_lookup >= 0) && (tb_lookup <= (TIMING_BITMAP_MODELIST_SIZE - 1))) {
					tb_lookup += 1;
				}
				if (tb_lookup > (TIMING_BITMAP_MODELIST_SIZE - 1)) {
					break;
				}
				tb_byte >>= 1;
			}
		} else {
			if (((tb_byte >>= 7) & 0x1) == 1) {
				add_mode(kmd_modelist, timing_bitmap_modelist[tb_lookup].width,
							   timing_bitmap_modelist[tb_lookup].height,
							   timing_bitmap_modelist[tb_lookup].refresh_rate);
			}
		}
	}
}

/*******************************************************************************
 *
 * Description
 *
 * get_additional_standard_display_modes - parses the additional standard
 * display resolutions which are not part of standard_display_modes
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * void
 *
 ******************************************************************************/
void get_additional_standard_display_modes(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	unsigned char *ptr_to_input_data = NULL;
	int start_index = 0;
	int i = 0;
	int index = 0;
	unsigned char asd_byte = 0;
	int bit = 0;
	int asd_lookup = 0;
	int end_index = -1;
	ptr_to_input_data = edid_data;

	for (i = DTD_START; i < DTD_END; i++) {
		if (memcmp(ptr_to_input_data + i, g_additional_standard_header, DTD_ADDITIONAL_STANDARD_HEADER_SIZE) == 0) {
			start_index = i + DTD_ADDITIONAL_STANDARD_START_BYTE;
			end_index = start_index + DTD_ADDITIONAL_STANDARD_TOTAL_BYTES;
			for (index = start_index; index <= end_index; index++) {
				if (asd_lookup > (DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE - 1)) {
					break;
				}
				asd_byte = edid_data[index];
				/* Retrieving modes from the first 5 bytes */
				if (index <= (start_index + 4)) {
					/* Traverse from the 1st bit to the 8th bit of the additional standard mode byte */
					for (bit = 0x0; bit <= 0x7; bit++) {
						if ((asd_byte & 0x1) == 1) {
							add_mode(kmd_modelist, additional_standard_timing_modelist[asd_lookup].width,
										   additional_standard_timing_modelist[asd_lookup].height,
										   additional_standard_timing_modelist[asd_lookup].refresh_rate);
						}
						if ((asd_lookup >= 0) && (asd_lookup <= (DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE - 1))) {
							asd_lookup += 1;
						}
						if (asd_lookup > (DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE - 1)) {
							break;
						}
						asd_byte >>= 1;
					}
				}
				/* Retrieving modes of the last byte */
				else {
					asd_byte >>= 4;
					/* Traverse from the 5th bit to the 8th bit of additional standard mode byte */
					for (bit = 0x4; bit <= 0x7; bit++) {
						if (asd_lookup > (DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE - 1)) {
							break;
						}
						if ((asd_byte & 0x1) == 1) {
							add_mode(kmd_modelist, additional_standard_timing_modelist[asd_lookup].width,
										   additional_standard_timing_modelist[asd_lookup].height,
										   additional_standard_timing_modelist[asd_lookup].refresh_rate);
						}
						if ((asd_lookup >= 0) && (asd_lookup <= (DTD_ADDITIONAL_STANDARD_TIMING_MODELIST_SIZE - 1))) {
							asd_lookup += 1;
						}
						asd_byte >>= 1;
					}
				}
			}
			break;
		}
	}
}

/*******************************************************************************
 *
 * Description
 *
 * get_detailed_timing_descriptor_modes - parses the detailed timing
 * descriptors if present in EDID
 *
 * Parameters
 * unsigned char *edid_data - input edid 256 bytes array
 * struct output_modelist* modelist - output modelist structure containing
 * all the supported modes (width, height & refresh_rate)
 *
 * Return val
 * void
 *
 ******************************************************************************/
static inline void get_detailed_timing_descriptor_modes(unsigned char *edid_data, struct output_modelist *kmd_modelist)
{
	unsigned char *ptr_to_input_data = NULL;
	unsigned int dtd_h_active = 0, dtd_v_active = 0, dtd_pixel_clk = 0, dtd_h_blank = 0, dtd_v_blank = 0,
				 dtd_h_total = 0, dtd_v_total = 0;
	double dtd_refresh_rate = 0;
	int i = DTD_START;

	ptr_to_input_data = edid_data;

	while (i < (DTD_END - DTD_DISPLAY_DESCRIPTOR_HEADER_SIZE)) {
		if (memcmp(ptr_to_input_data + i, g_dtd_display_header, DTD_DISPLAY_DESCRIPTOR_HEADER_SIZE) != 0) {
			dtd_pixel_clk =
				(edid_data[i + BYTE_POSITION(0)] + (edid_data[i + BYTE_POSITION(1)] << SHIFT_INDEX(8))) * CLK_UNIT;
			dtd_h_active = ((edid_data[i + BYTE_POSITION(4)] >> SHIFT_INDEX(4)) << SHIFT_INDEX(8)) +
						   edid_data[i + BYTE_POSITION(2)];
			dtd_v_active = ((edid_data[i + BYTE_POSITION(7)] >> SHIFT_INDEX(4)) << SHIFT_INDEX(8)) +
						   edid_data[i + BYTE_POSITION(5)];
			dtd_h_blank = ((edid_data[i + BYTE_POSITION(4)] & SHIFT_INDEX(15)) << SHIFT_INDEX(8)) +
						  edid_data[i + BYTE_POSITION(3)];
			dtd_v_blank = ((edid_data[i + BYTE_POSITION(7)] & SHIFT_INDEX(15)) << SHIFT_INDEX(8)) +
						  edid_data[i + BYTE_POSITION(6)];
			dtd_h_total = dtd_h_active + dtd_h_blank;
			dtd_v_total = dtd_v_active + dtd_v_blank;

			/* Guard against malformed DTD: skip descriptor if totals are zero to prevent divide-by-zero */
			if (dtd_h_total != 0 && dtd_v_total != 0) {
				dtd_refresh_rate = dtd_pixel_clk / (dtd_h_total * dtd_v_total);
				add_mode(kmd_modelist, dtd_h_active, dtd_v_active, dtd_refresh_rate);
			}
		}
		i = (i + DTD_STANDARD_DESC_SIZE);
	}
}

/*===========================================================================
; DVServeredidparser.cpp
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file defines the DVServer edid parser functionality
;--------------------------------------------------------------------------*/
#include <windows.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Driver.h"
#include "DVServercommon.h"
#include "DVServeredidparser.h"
#include "DVServererror.h"

using namespace Microsoft::IndirectDisp;

static bool validate_edidblock(BYTE *edid);
static void dtd_description(BYTE *edid, DISPLAY_INFO *disp_info, int *count);

const UINT64 MHZ = 1000000;

extern struct IndirectSampleMonitor s_SampleMonitors[];
/*---------------------------------------------------------------------------
; Internal API to get VM monitor mode info from all sources
; Use the host OS populated mode info if it's avaiable in the registry
; Use the hard-coded mode info if host OS mode info not avaiable^M
---------------------------------------------------------------------------*/
void idd_get_monitor_modes(void)
{
	BYTE edid[BUFSIZ] = { 0 };
	DISPLAY_INFO disp_info = { 0 };
	int count, status;
	ULONG buff_size = BUFSIZ;

	WriteToLog("idd_get_monitor_modes: Start\n");
	status = idd_read_registry_binary(L"PanelEDID", edid, &buff_size);

	if (status == IDD_SUCCESS &&
		buff_size >= EDID_SIZE &&
		idd_parse_edid(edid, &disp_info, &count) == IDD_SUCCESS) {
		WriteToLog("Update mode info with valid EDID.\n");
		idd_update_modes(edid, &disp_info);
	} else {
		WriteToLog("No valid EDID found.\n");
	}

	WriteToLog("idd_get_monitor_modes: Exit.\n");
}

/*---------------------------------------------------------------------------
; Internal API to parse the EDID data
---------------------------------------------------------------------------*/
int idd_parse_edid(BYTE *edid, DISPLAY_INFO *disp_info, int *count)
{
	WriteToLog("idd_parse_edid: Function gets called.\n");

	/* For now, IddCx only needs DTD info to be parsed with a valid edid */
	if (validate_edidblock(edid) == false) {
		return IDD_FAILURE;
	} else {
		dtd_description(edid, disp_info, count);
	}

	return IDD_SUCCESS;
}

/*---------------------------------------------------------------------------
; Internal API to update IDD mode info for IddCtx
---------------------------------------------------------------------------*/
void idd_update_modes(BYTE *edid, DISPLAY_INFO* disp_info)
{
	if (!edid || !disp_info) {
		return;
	}

	/*
	 * Assme valid edid and display info from the caller
	 * Update the IDD monitor mode info to be sent back to IddCx
	 */
	WriteToLog("idd_update_modes: Update IddCx monitor mode info. \n");
	
	memcpy_s(s_SampleMonitors[0].pEdidBlock, EDID_SIZE, edid, EDID_SIZE);

	s_SampleMonitors[0].pModeList[0].Width = disp_info->h_active;
	s_SampleMonitors[0].pModeList[0].Height = disp_info->v_active;
	s_SampleMonitors[0].pModeList[0].VSync = (DWORD)disp_info->pixel_clock;
}

/*---------------------------------------------------------------------------
; Helper function to validate EDID data integrity
---------------------------------------------------------------------------*/
static bool validate_edidblock(BYTE *edid)
{
	BYTE edid_len = 128;
	BYTE check_sum = 0, i;

	for (i = 0; i < edid_len; i++)
		check_sum += *(edid + i);

	if (check_sum != 0) {
		WriteToLog("validate_edidblock: Checksum wrong. \n");
		return false;
	}

	BYTE base_edidheader[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	if (memcmp(edid, base_edidheader, sizeof(base_edidheader)) != 0) {
		WriteToLog("validate_edidblock: EDID header wrong. \n");
		return false;
	}

	return true;
}

/*---------------------------------------------------------------------------
; Helper function to get DTD info from the EDID data block
---------------------------------------------------------------------------*/
static void dtd_description(BYTE *edid, DISPLAY_INFO *disp_info, int *count)
{
	BYTE *temp = edid + DTD_BLOCK_SART;
	BYTE loop_count;
	int h_active, v_active;
	int h_blanking, h_sync_offset, h_sync_width;
	int v_blanking, v_sync_offset, v_sync_width;
	float pixel_clock;

	for (loop_count = 0; loop_count < DTD_BLOCK_COUNT; loop_count++) {
		/* Refer to EDID 1.4 spec for following definition details */
		if (temp[0] != 0 && temp[1] != 0) {
			/* Valid DTD block found */
			WriteToLog("dtd_description: Valid DTD found.\n");

			h_active        = ((temp[4] & 0xF0) << 4) | temp[2];
			v_active        = ((temp[7] & 0xF0) << 4) | temp[5];
			h_blanking      = ((temp[4] & 0x0F) << 8) | temp[3];
			v_blanking      = ((temp[7] & 0x0F) << 8) | temp[6];
			h_sync_offset   = ((temp[11] & 0xC0) << 2) | temp[8];
			v_sync_offset   = ((temp[11] & 0x0C) << 2) | ((temp[10] & 0xF0) >> 4);
			h_sync_width    = ((temp[11] & 0x30) << 4) | temp[9];
			v_sync_width    = ((temp[11] & 0x03) << 4) | (temp[10] & 0x0F);
			pixel_clock     = (float)(((temp[1] << 8) | temp[0]) * 10000);

			disp_info->h_active     = h_active;
			disp_info->v_active     = v_active;
			disp_info->h_total      = h_active + h_sync_offset + h_sync_width;
			disp_info->v_total      = v_active + v_sync_offset + v_sync_width;
			disp_info->pixel_clock  = pixel_clock;

			memcpy_s(disp_info->edid, EDID_SIZE, edid, EDID_SIZE);
			(*count)++;
		}
		temp += DTD_BLOCK_SIZE;
	}
}

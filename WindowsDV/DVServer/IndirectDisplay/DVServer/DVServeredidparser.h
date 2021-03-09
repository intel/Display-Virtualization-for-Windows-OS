/*===========================================================================
; DVServeredidparser.h
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file declares the IOTG IDD edid parser function releted definitions
;--------------------------------------------------------------------------*/
#ifndef __DVSERVER_EDID_PARSER_H__
#define __DVSERVER_EDID_PARSER_H__

struct DISPLAY_INFO {
	int h_active;
	int v_active;
	int h_total;
	int v_total;
	float pixel_clock;
	BYTE edid[128];
};

#define DTD_BLOCK_SART      0x36
#define DTD_BLOCK_COUNT     0x04
#define DTD_BLOCK_SIZE      0x12
#define EDID_SIZE           0x80

#define EDID_REG_NAME       "PanelEDID"
#define EDID_NUM            "NumOfEDID"

void idd_get_monitor_modes(void);
int idd_parse_edid(BYTE *edid, DISPLAY_INFO *disp_info, int *count);
void idd_update_modes(BYTE *edid, DISPLAY_INFO* disp_info);

#endif /* __DVSERVER_EDID_PARSER_H__ */

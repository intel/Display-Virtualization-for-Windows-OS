/*===========================================================================
; edidshared.h
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;--------------------------------------------------------------------------*/

#ifndef __EDID_SHARED_H__
#define __EDID_SHARED_H__

#define OUTPUT_MODELIST_SIZE							32

struct edid_qemu_modes {
	unsigned int width;
	unsigned int height;
	double refresh_rate;
};

struct output_modelist {
	struct edid_qemu_modes modelist[OUTPUT_MODELIST_SIZE];
	unsigned int modelist_size;
};

int parse_edid_data(unsigned char*, struct output_modelist*);

#endif //__EDID_SHARED_H__

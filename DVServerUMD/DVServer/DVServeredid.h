/*===========================================================================
; DVServeredid.h
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file declares the IOTG IDD edid parser function releted definitions
;--------------------------------------------------------------------------*/
#ifndef __DVSERVER_EDID_H__
#define __DVSERVER_EDID_H__

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <winioctl.h>
#include <SetupAPI.h>
#include <initguid.h>
#include "Driver.h"
#include "DVServercommon.h"


#define WIDTH_UPPER_CAP     3840 //3840x2160
#define WIDTH_LOWER_CAP     1024 //1024x768
#define REFRESH_RATE_59     59   //59Hz
#define REFRESH_RATE_60     60   //60Hz
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define VGPU_BPP            32
#define BYTE_ALIGN_16       16

int get_total_screens(HANDLE devHandle);
int get_edid_data(HANDLE devHandle, void* m, DWORD id);
int is_blacklist(unsigned int width, unsigned int height);
int is_samestride(unsigned int width);

#endif /* __DVSERVER_EDID_H__ */

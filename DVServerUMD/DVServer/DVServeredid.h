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

int get_total_screens(HANDLE devHandle);
int get_edid_data(HANDLE devHandle, void *m, DWORD id, BOOL d_edid);
int is_blacklist(unsigned int width, unsigned int height);

#endif /* __DVSERVER_EDID_H__ */

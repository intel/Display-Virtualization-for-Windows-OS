/*===========================================================================
; DVServercommon.h
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file declares the common variables used & related functionalities
;--------------------------------------------------------------------------*/
#ifndef __DVSERVER_COMMON_H__
#define __DVSERVER_COMMON_H__

#include "Driver.h"

/* DVServerUMD Error Codes */
#define DVSERVERUMD_SUCCESS        0
#define DVSERVERUMD_FAILURE        -1
#define MODE_LIST_MAX_SIZE         32
#define MAX_MONITOR_SUPPORTED      4
#define HOTPLUG_EVENT              L"Global\\HOTPLUG_EVENT"
#define DVE_EVENT                  L"Global\\DVE_EVENT"
#define HOTPLUG_TERMINATE_EVENT    L"Global\\HOTPLUG_TERMINATE_EVENT"
#define DISP_INFO                  L"Global\\DISP_INFO"
#define PRIMARY_IDD_INDEX          0

//MTL Device ID Lists
#define DEV_ID_7D40 0x7D40
#define DEV_ID_7D45 0x7D45
#define DEV_ID_7D55 0x7D55
#define DEV_ID_7D60 0x7D60
#define DEV_ID_7D67 0x7D67
#define DEV_ID_7DD5 0x7DD5

constexpr auto DEVICE_ID_REGEX_PATTERN = R"(DEV_([0-9A-Fa-f]{4}))";
constexpr int MAX_ENUM_ATTEMPTS = 100;  // Prevent infinite loops

#endif /* __DVSERVER_COMMON_H__ */

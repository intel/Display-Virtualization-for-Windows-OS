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
#define MAX_REG_NAME_LENGTH        32
#define EDID_SIZE                  256
#define MODE_LIST_MAX_SIZE         32
#define MAX_MONITOR_SUPPORTED      4
#define HOTPLUG_EVENT              L"Global\\HOTPLUG_EVENT"
#define DVE_EVENT                  L"Global\\DVE_EVENT"
#define HOTPLUG_TERMINATE_EVENT    L"Global\\HOTPLUG_TERMINATE_EVENT"
#define DISP_INFO                  L"Global\\DISP_INFO"
#define PRIMARY_IDD_INDEX          0

static NTSTATUS open_dvserver_registry(WDFKEY* key);
static void close_dvserver_registry(WDFKEY key);
int write_dvserver_registry_binary(PCWSTR name, BYTE* buffer, ULONG size);
int read_dvserver_registry_binary(PCWSTR name, BYTE* buffer, ULONG* size);

#endif /* __DVSERVER_COMMON_H__ */

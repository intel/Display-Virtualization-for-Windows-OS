/*===========================================================================
; DVServercommon.h
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file declares the common variables used & related functionalities
;--------------------------------------------------------------------------*/
#ifndef __DVSERVER_COMMON_H__
#define __DVSERVER_COMMON_H__

#include "Driver.h"

/* IDD debug output support */
#define WriteToLog(x)  OutputDebugStringA(x)

/* IDD registry access interface */
#define MAX_REG_NAME_LENGTH     32

int idd_read_registry_dword(PCWSTR name, ULONG *value);
int idd_write_registry_dword(PCWSTR name, ULONG value);
int idd_read_registry_binary(PCWSTR name, BYTE *buffer, ULONG *size);
int idd_write_registry_binary(PCWSTR name, BYTE *buffer, ULONG size);
int idd_read_registry_string(PCWSTR name, WCHAR *buffer, ULONG size);
int idd_write_registry_string(PCWSTR name, WCHAR *buffer, ULONG size);

#endif /* __DVSERVER_COMMON_H__ */

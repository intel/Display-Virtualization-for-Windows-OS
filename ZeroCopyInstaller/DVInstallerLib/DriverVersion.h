/*==============================================================================
; DriverVersion.h
;-------------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
; This file declares variables used in driver version retrieval functionality
;-----------------------------------------------------------------------------*/
#pragma once
#include <windows.h>
#include <SetupAPI.h>
#include <iostream>
#include <initguid.h>

#define EXPORT extern "C" __declspec(dllexport)
#define DVSERVERKMD_DESC L"DVServerKMD Device"
#define DRIVERINSTALLER_REGPATH L"SYSTEM\\CurrentControlSet\\Control\\Class"

DEFINE_GUID(ClassGuid, 0x4d36e97d, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

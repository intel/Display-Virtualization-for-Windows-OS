/*==============================================================================
; DVInstaller.h
;-------------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
; This file declares Variables and API's used in GUI installer functionality
;-----------------------------------------------------------------------------*/
#pragma once

#include "resource.h"

#include <windows.h>
#include <Newdev.h>
#include <SetupAPI.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <tchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <regstr.h>
#include <infstr.h>
#include <string.h>
#include <malloc.h>
#include <newdev.h>
#include <objbase.h>
#include <strsafe.h>
#include <iostream>
#include <psapi.h>
#include <string.h>
#include <Shellapi.h>
#include <Pathcch.h>
#include <tlhelp32.h>
#include <vector>
#include <wincrypt.h>
#include <fstream>

#define TEXT(quote) __TEXT(quote)

#ifdef _UNICODE
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesW"
#define SETUPUNINSTALLOEMINF "SetupUninstallOEMInfW"
#define DIINSTALLDRIVER "DiInstallDriverW"
#define DIUNINSTALLDRIVER "DiUninstallDriverW"
#else
#define UPDATEDRIVERFORPLUGANDPLAYDEVICES "UpdateDriverForPlugAndPlayDevicesA"
#define SETUPUNINSTALLOEMINF "SetupUninstallOEMInfA"
#define DINSTALLDRIVER "DiInstallDriverA"
#define DIUNINSTALLDRIVER "DiUninstallDriverA"
#endif

enum InstallModes {
	INSTALL = 1,
	UNINSTALL,
	UPDATE,
	UNINSTALLANDUPDATE,
	MAXMODES
};

#define EXIT_OK      (0)
#define EXIT_FAIL  (1)
#define EXIT_REBOOT    (2)
#define EXIT_USAGE   (3)
#define EXIT_UMD_INF_REMOVAL_FAILED   (4)
#define EXIT_KMD_INF_REMOVAL_FAILED   (5)

#define DLL_LOADED_MAX_RETRY 10
#define DV_HWID TEXT("PCI\\VEN_1AF4&DEV_1050&CC_030000")
#define DVSERVERKMD_INF TEXT("DVServerKMD.inf")
#define DVSERVERKMD_DESC L"DVServerKMD Device"
#define DVSERVERUMD_DESC L"DVServerUMD Device"
#define DVINSTALLER_RUNDLL_EXE L"rundll32.exe"
#define DVINSTALLER_RUNDLL_ARGUMENT L"DVEnabler.dll,dvenabler_init"
#define DVINSTALLER_DLL_NAME L"DVEnabler.dll"
#define DVINSTALLER_REG_RUN_PATH TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run")
#define DVINSTALLER_REG_DLL_KEYNAME TEXT("dvServerDll")
#define DVINSTALLER_REG_EXE_KEYNAME TEXT("dvServerExe")
#define DVINSTALLER_WIN_APP TEXT("DV")
#define DVINSTALLER_DVSERVERDLL_NAME "dvserver.dll"
#define DVINSTALLER_DLL_FUNCTION L"dvenabler_init"

DEFINE_GUID(ClassGuid, 0x4d36e97d, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

int dvPostInstall(void);
int dvInstall(InstallModes code, BOOL dvInstallUmd);
int dvGetInstalledOemInfFile(WCHAR*);
int dvUninstall(WCHAR*);
int dvGetInfPath(TCHAR* infFile, TCHAR* infPath);
int dvLoadUnloadNewdevLib(HMODULE* newdevMod, BOOL isLoad);
void dvKillDll(std::wstring moduleName);
int dvUpdateCertifcates(void);
BOOL dvDeviceScan(void);
BOOL  dvGetInstalledOemInfFileName(const wchar_t* deviceDesc, WCHAR* infFileName);
BOOL dvUninstallKmdAndUmd(BOOL unInstallUmd);

typedef BOOL(WINAPI* UpdateDriverForPlugAndPlayDevicesProto)(_In_opt_ HWND hwndParent,
	_In_ LPCTSTR HardwareId,
	_In_ LPCTSTR FullInfPath,
	_In_ DWORD InstallFlags,
	_Out_opt_ PBOOL bRebootRequired
	);
typedef BOOL(WINAPI* DINSTALLDRIVERProto)(_In_opt_ HWND hwndParent,
	_In_ LPCTSTR FullInfPath,
	_In_ DWORD InstallFlags,
	_Out_opt_ PBOOL bRebootRequired
	);

typedef BOOL(WINAPI* DIUNINSTALLDRIVERProto)(_In_opt_ HWND hwndParent,
	_In_ LPCTSTR InfFileName,
	_In_ DWORD Flags,
	PBOOL  NeedReboot
	);

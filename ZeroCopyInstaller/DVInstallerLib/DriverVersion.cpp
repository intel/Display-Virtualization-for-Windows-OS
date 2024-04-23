/*===========================================================================
; DriverVersion.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   Implements the Driver version retrieval functionality
;--------------------------------------------------------------------------*/
#include "DriverVersion.h"

EXPORT int cdecl GetStringfromRegister(int* major, int* minor, int* patch, int* build) {
	const wchar_t* partialDriverkeyPath = DRIVERINSTALLER_REGPATH;
	wchar_t filterDeviceDesc[] = DVSERVERKMD_DESC;
	int result = 0;
	DWORD index = 0, size = 0;
	SP_DEVINFO_DATA devInfo;
	wchar_t deviceName[MAX_PATH];
	wchar_t driverVersion[MAX_PATH];
	wchar_t versionBuffer[MAX_PATH];
	wchar_t regkey[MAX_PATH];
	HKEY hkey;
	DWORD versionBufferSize = sizeof(versionBuffer);
	HDEVINFO devInfoSet;

	devInfoSet = SetupDiGetClassDevs(&ClassGuid, nullptr, nullptr, DIGCF_PRESENT);
	if (devInfoSet == INVALID_HANDLE_VALUE) {
		return result;
	}
	// Get the device info for this device
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	for (index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfo); index++) {

		// Build a list of driver info items that we will retrieve below
		if (!SetupDiGetDeviceRegistryProperty(devInfoSet, &devInfo, SPDRP_DEVICEDESC, NULL,
			(PBYTE)deviceName, sizeof(deviceName), &size)) {
			continue;
		}
		// compare any driver matches with "DVServerKMD Device"
		if (wcsstr(deviceName, filterDeviceDesc) == NULL) {
			continue;
		}
		// retrieve Driver key matching with "DVServerKMD Device"
		if (!SetupDiGetDeviceRegistryProperty(devInfoSet, &devInfo, SPDRP_DRIVER, nullptr,
			reinterpret_cast<PBYTE>(driverVersion), sizeof(driverVersion), &size)) {
			continue;
		}
		//get the Registry path of the DVServerKMD Device file
		_snwprintf_s(regkey, MAX_PATH, _TRUNCATE, L"%s\\%s", partialDriverkeyPath, driverVersion);

		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, regkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
			continue;
		}
		if (RegQueryValueEx(hkey, L"DriverVersion", NULL, NULL, (LPBYTE)versionBuffer, &versionBufferSize) == ERROR_SUCCESS) {
			result = 1;
			if (!swscanf_s(versionBuffer, L"%d.%d.%d.%d", &(*major), &(*minor), &(*patch), &(*build))) {
				result = 0;
			}
		}
		RegCloseKey(hkey);
		break;
	}
	SetupDiDestroyDeviceInfoList(devInfoSet);
	return result;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

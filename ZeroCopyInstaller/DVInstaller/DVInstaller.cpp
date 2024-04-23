/*===========================================================================
; dvinstaller.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file implements SRIOV install,uninstall and update functionality.
;--------------------------------------------------------------------------*/
#include "DVInstaller.h"
#include "Trace.h"
#include "dvinstaller.tmh"

/*******************************************************************************
*
* Description
*
* dvUninstallKmdUmdAndRemoveOemInfFile - uninstalls both KMD and UMD driver
*
* Parameters
*   deviceDesc - Device description of the Parent(KMD) driver.
*   kmdInfFilename - KMD driver's OEM INF file name from driver store.
*   umdInfFilename - UMD driver's OEM INF file name from driver store.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
*******************************************************************************/
int  dvUninstallKmdUmdAndRemoveOemInfFile(const wchar_t* deviceDesc, WCHAR* infFullPathKmd, WCHAR* infFullPathUmd) {

	int result = EXIT_FAIL;
	DWORD index = 0, size = 0;
	wchar_t deviceName[MAX_PATH];
	HDEVINFO devInfoSet;
	SP_DEVINFO_DATA devInfo;
	HMODULE newdevMod = NULL;
	BOOL reboot = TRUE;

	TRACING();
	if (!infFullPathKmd) {
		return result;
	}
	devInfoSet = SetupDiGetClassDevs(NULL, nullptr, nullptr, DIGCF_ALLCLASSES);
	if (devInfoSet == INVALID_HANDLE_VALUE) {
		ERR("failed in SetupDiGetClassDevs,\n");
		return result;
	}
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	for (index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfo); index++) {
		// Build a list of driver info items
		if (!SetupDiGetDeviceRegistryProperty(
			devInfoSet, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)deviceName, sizeof(deviceName), &size)) {
			continue;
		}
		if (wcsstr(deviceName, deviceDesc)) {
			result = EXIT_OK;
			if (DiUninstallDevice(NULL, devInfoSet, &devInfo, 0, &reboot)) {
				DBGPRINT("\nKMD device removed");
				if (SetupUninstallOEMInf(infFullPathKmd, SUOI_FORCEDELETE, NULL)) {
					DBGPRINT("\nKMD device INF removed");
					if (infFullPathUmd) {
						if (!SetupUninstallOEMInf(infFullPathUmd, SUOI_FORCEDELETE, NULL)) {
							ERR("\nUMD device INF remove failed");
							result = EXIT_UMD_INF_REMOVAL_FAILED;
						}
						else {
							DBGPRINT("\nUMD device INF removed");
						}
					}
				}
				else {
					ERR("\nKMD device INF remove failed");
					result = EXIT_KMD_INF_REMOVAL_FAILED;
				}
			}
			else {
				if (GetLastError() == ERROR_INF_IN_USE_BY_DEVICES) {
					ERR("\nDV driver is in use, can not de deleted\n");
				}
				else {
					ERR("\nerror code %x\n", GetLastError());
				}
				ERR("KMD device remove failed\n");
				result = EXIT_FAIL;
			}
		}
	}
	SetupDiDestroyDeviceInfoList(devInfoSet);
	return result;
}

/*******************************************************************************
*
* Description
*
* dvUninstallKmdAndUmd - uninstalls the DV driver from the driver store through OEM INF
*               file.
*
* Parameters
*   unInstallUmd - Specify if child driver INF also to be removed.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
*******************************************************************************/
BOOL dvUninstallKmdAndUmd(BOOL unInstallUmd)
{
	int result = EXIT_FAIL;
	WCHAR infFullPathumd[MAX_PATH];
	WCHAR infFullPathkmd[MAX_PATH];

	TRACING();
	//device scan after uninstall
	if (!dvDeviceScan()) {
		ERR("\nKMD/UMD device scan failed");
	}
	if (unInstallUmd) {
		//Get UMD inf file name
		if (!dvGetInstalledOemInfFileName(DVSERVERUMD_DESC, infFullPathumd)) {
			ERR("\nUMD device INF not found");
			return FALSE;
		}
	}
	//Get KMD inf file name
	if (!dvGetInstalledOemInfFileName(DVSERVERKMD_DESC, infFullPathkmd)) {
		ERR("\nKMD device INF not found");
		return FALSE;
	}
	//Remove kmd and it's child device and later remove the OEM inf file name from the device store
	result = dvUninstallKmdUmdAndRemoveOemInfFile(DVSERVERKMD_DESC, infFullPathkmd, ((unInstallUmd == TRUE) ? infFullPathumd : NULL));
	if (result != EXIT_OK) {

		ERR("\nKMD/UMD device and driver removal failed %d", result);
		return FALSE;
	}

	return TRUE;
}

/*******************************************************************************
*
* Description
*
* dvUninstall - uninstalls the DV driver from the driver store through OEM INF
*               file.
*
* Parameters
*   infFullPath - OEM INF file from drive store with full path.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
*******************************************************************************/
int dvUninstall(WCHAR* infFullPath)
{
	HMODULE newdevMod = NULL;
	BOOL reboot = TRUE;
	int failcode = EXIT_FAIL;
	DIUNINSTALLDRIVERProto suoiFn;

	TRACING();
	if (dvLoadUnloadNewdevLib(&newdevMod, TRUE)) {
		ERR("Failed to load NewdevLib");
		return EXIT_FAIL;
	}

	suoiFn = (DIUNINSTALLDRIVERProto)GetProcAddress(newdevMod, DIUNINSTALLDRIVER);
	if (suoiFn) {
		if (!suoiFn(NULL, infFullPath, 0, &reboot)) {
			if (GetLastError() == ERROR_INF_IN_USE_BY_DEVICES) {
				ERR("\nDV driver is in use, can not de deleted\n");
			}
			else if (GetLastError() == ERROR_NOT_AN_INSTALLED_OEM_INF) {
				ERR("\nDV driver is not installed, can not delete\n");
			}
			else {
				ERR("\nfailed to delete for reason = (%d) \n", GetLastError());
			}
		}
		else {
			DBGPRINT("dv Driver uninstalled Successfully");
			failcode = EXIT_OK;
		}
	}

	if (dvLoadUnloadNewdevLib(&newdevMod, FALSE)) {
		ERR("Failed to unload the NewdevLib");
		return EXIT_FAIL;
	}

	return failcode;
}

/*******************************************************************************
*
* Description
*
* dvInstall - Function installs/updates the driver based on inputs.
*
* Parameters
*   installMode - Driver is installed or updated based on the installMode.
*   dvInstallUmd - Specifies if DVServerUMD or DVServerKMD inf to be choosen
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
******************************************************************************/
int dvInstall(InstallModes installMode, int dvInstallUmd)
{
	HMODULE newdevMod = NULL;
	BOOL reboot = TRUE;
	DWORD flags = 0;
	int failcode = EXIT_FAIL;
	LPCTSTR hwid = DV_HWID;
	TCHAR infFile[MAX_PATH] = DVSERVERKMD_INF;
	TCHAR infPath[MAX_PATH] = TEXT("");
	DINSTALLDRIVERProto installFn;
	UpdateDriverForPlugAndPlayDevicesProto updateFn;

	TRACING();
	DBGPRINT("RequestedMode = %d and dvInstallUmd = %d\n", installMode, dvInstallUmd);
	if (dvInstallUmd) {
		_tcscpy_s(infFile, TEXT("DVServer.inf"));
	}
	if (dvGetInfPath(infFile, infPath)) {
		ERR("Failed to retrieve dv driver INF path");
		return EXIT_FAIL;
	}
	if (dvLoadUnloadNewdevLib(&newdevMod, TRUE)) {
		ERR("Failed to load NewdevLib");
		return EXIT_FAIL;
	}
	//device scan before install
	if (!dvDeviceScan()) {
		goto final;
	}
	switch (installMode) {
	case INSTALL:
		installFn = (DINSTALLDRIVERProto)GetProcAddress(newdevMod, DIINSTALLDRIVER);
		if (!installFn) {
			ERR("Failed to fetch install function address");
			goto final;
		}
		if (!installFn(NULL, infPath, DIIRFLAG_FORCE_INF, &reboot)) {
			ERR("Failed to install dv driver ");
			goto final;
		}
		DBGPRINT("dv driver Installed Successfully!!!");
		break;
	case UPDATE:
		updateFn = (UpdateDriverForPlugAndPlayDevicesProto)GetProcAddress(newdevMod, UPDATEDRIVERFORPLUGANDPLAYDEVICES);
		if (!updateFn) {
			ERR("Failed to fetch Update function address");
			goto final;
		}
		if (!updateFn(NULL, hwid, infPath, INSTALLFLAG_FORCE, &reboot)) {
			ERR("\nfailed updating the drivers Error = %d\n", GetLastError());
			goto final;
		}
		DBGPRINT("Update of dv driver done!!!");
		break;
	default:
		ERR("Invalid case");
		goto final;
		break;
	}
	if ((installMode == INSTALL) || (installMode == UPDATE)) {
		if (!dvDeviceScan()) {
			goto final;
		}
		if (dvInstallUmd) {
			DBGPRINT("UMD driver install done in dvPostInstall");
			failcode = EXIT_OK;
		}
		else {
			if (dvPostInstall()) {
				ERR("failed in dvPostInstall");
			}
			else {
				failcode = EXIT_OK;
			}
		}
	}
	final :
	if (dvLoadUnloadNewdevLib(&newdevMod, FALSE)) {
		ERR("Failed to unload NewdevLib");
		return EXIT_FAIL;
	}
	return failcode;
}

/*==============================================================================
; dvpostinit.cpp
;-------------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file implements changes required by the dvinstaller post install/update
;-----------------------------------------------------------------------------*/
#include "DVInstaller.h"
#include "Trace.h"
#include "dvpostinit.tmh"

/*******************************************************************************
*
* Description
*
* dvGetRegistryPath: Gets the Powershell path from the windows registry.
*
* Parameters
*   none.
*
* Return val
*  Powershell path- On success, Empty string - on Failure
*
******************************************************************************/
static std::wstring dvGetRegistryPath()
{
	HKEY hkey;
	DWORD dwRet;
	WCHAR buffer[MAX_PATH];
	DWORD bufferSize = MAX_PATH;
	LPCTSTR lpSubkey = L"Software\\Microsoft\\PowerShell\\1\\ShellIds\\Microsoft.PowerShell";

	TRACING();
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, lpSubkey, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
		dwRet = RegQueryValueEx(hkey, L"Path", NULL, NULL, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
		if (dwRet == ERROR_SUCCESS) {
			RegCloseKey(hkey);
			return std::wstring(buffer);
		}
		else {
			DBGPRINT("failed to retrieve with error code %d", dwRet);
			RegCloseKey(hkey);
			return L"";
		}
	}
	else {
		DBGPRINT("Failed to open key:Software\\Microsoft\\Windows\\CurrentVersion\\Run");
	}
	return L"";
}

/*******************************************************************************
*
* Description
*
* dvRegisterTaskSchedular: Register commands with the task schedular to
*  start DVenabler on workstation unlock and kill dvenabler on workstation lock
*
*
* Parameters
*   none.
*
* Return val
*  Powershell path- On success, Empty string - on Failure
*
******************************************************************************/
static BOOL dvRegisterTaskSchedular()
{
	WCHAR commandLine[MAX_PATH + 50];
	STARTUPINFO si;
	DWORD lpExitCode = 9999;
	PROCESS_INFORMATION pi;
	BOOL result = FALSE;
	const wchar_t* scrtipContent = LR"($TASK_SESSION_UNLOCK = 8 #TASK_SESSION_STATE_CHANGE_TYPE.TASK_SESSION_UNLOCK (taskschd.h)
	$TASK_SESSION_LOCK = 7  #TASK_SESSION_STATE_CHANGE_TYPE.TASK_SESSION_LOCK (taskschd.h)

	$stateChangeTrigger = Get-CimClass `
		-Namespace ROOT\Microsoft\Windows\TaskScheduler `
		-ClassName MSFT_TaskSessionStateChangeTrigger

	$onUnlockTrigger = New-CimInstance ` -CimClass $stateChangeTrigger `
		-Property @{ StateChange = $TASK_SESSION_UNLOCK } ` -ClientOnly

	$onLockTrigger = New-CimInstance ` -CimClass $stateChangeTrigger `
		-Property @{ StateChange = $TASK_SESSION_LOCK } ` -ClientOnly

	unregister-scheduledtask -TaskName "DVEnabler" -confirm:$false -ErrorAction SilentlyContinue
	unregister-scheduledtask -TaskName "StopDVEnabler" -confirm:$false -ErrorAction SilentlyContinue

	try {
		#Register a task to start the dvenabler.dll as a service during every user logon or user unlock
			$ac = New-ScheduledTaskAction -Execute "rundll32.exe"  -Argument "C:\Windows\System32\DVEnabler.dll,dvenabler_init"
			$tr = New-ScheduledTaskTrigger -AtLogOn
			# Use the SID for INTERACTIVE group
			$interactiveSID = "S-1-5-4"
			$pr = New-ScheduledTaskPrincipal  -Groupid  $interactiveSID -RunLevel Highest
			$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -ExecutionTimeLimit 0 -MultipleInstances Queue
		Register-ScheduledTask -TaskName "DVEnabler" -Trigger @($tr, $onUnlockTrigger) -TaskPath "\Microsoft\Windows\DVEnabler" -Action $ac -Principal $pr -Settings $settings -ErrorAction Stop

		#Register a task to Stop the dvenabler.dll during every user lock
			$ac = New-ScheduledTaskAction -Execute "powershell.exe"  -Argument "-ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -Command `"Stop-ScheduledTask -TaskName '\Microsoft\Windows\DVEnabler\DvEnabler'`""
			$pr = New-ScheduledTaskPrincipal  -Groupid  $interactiveSID -RunLevel Highest
			$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -ExecutionTimeLimit 0 -MultipleInstances Queue
		Register-ScheduledTask -TaskName "StopDVEnabler" -Trigger $onLockTrigger -TaskPath "\Microsoft\Windows\DVEnabler" -Action $ac -Principal $pr -Settings $settings -ErrorAction Stop
	} catch {
		
        exit 0
}
exit 1
)";
	TRACING();
	try {
		std::wstring powershellPath = dvGetRegistryPath();
		if (!powershellPath.empty()) {
			std::wofstream scriptFile(L"tempscript.ps1", std::ios::out | std::ios::binary);
			if (!scriptFile.is_open()) {
				ERR("failed to open temporary file tempscript.ps1");
				return FALSE;
			}
			scriptFile << scrtipContent;
			scriptFile.close();
			DBGPRINT("tempscript.ps1 file created");
			//Construct powershell command to be executed
			swprintf(commandLine, (MAX_PATH + 50), L"%s -WindowStyle Hidden -ExecutionPolicy Bypass -File \"%s\"", powershellPath.c_str(), L"tempscript.ps1");

			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));
			if (!CreateProcess(powershellPath.c_str(), commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
				ERR("failed to create process");
				return FALSE;
			}
			DBGPRINT("Waiting for the process to complete execution of the script");
			// Wait until child process exits.
			WaitForSingleObject(pi.hProcess, INFINITE);
			if (!GetExitCodeProcess(pi.hProcess, &lpExitCode)) {
				ERR("failed to get return value of the script execution");
			}
			else {
				if (!lpExitCode) {
					ERR("failed to run the script");
				}
				else {
					DBGPRINT("Taskschedular registration is successful");
					result = TRUE;
				}
			}
			// Close process and thread handles.
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);

			if (remove("tempscript.ps1") != 0) {
				ERR("Failed to remove tempscript.ps1");
			}
		}
	}
	catch (const std::exception& e) {
		ERR("an exception occurred while fetching the powershell path");
		return FALSE;
	}
	return result;
}

/*******************************************************************************
*
* Description
*
* dvStartDLL: Starts DVEnabler.dll through rundll32.exe.
*
* Parameters
*   none.
*
* Return val
* BOOL - TRUE on success, FALSE on failure
*
******************************************************************************/
static BOOL dvStartDLL()
{
	LPCWSTR argument = DVINSTALLER_RUNDLL_ARGUMENT;
	LPCWSTR command = DVINSTALLER_RUNDLL_EXE;
	wchar_t system32Path[MAX_PATH];
	const wchar_t* path0 = L".\\";
	wchar_t* dllCmdPath = nullptr;
	wchar_t* dllArgPath = nullptr;
	BOOL result = FALSE;

	TRACING();
	if (!GetSystemDirectory(system32Path, MAX_PATH)) {
		ERR("Error in fetching system path");
		return FALSE;
	}

	if (FAILED(PathAllocCombine(system32Path, command, PATHCCH_NONE, &dllCmdPath))) {
		ERR("Failed in the construction of DLL source path ");
		return FALSE;
	}
	if (dllCmdPath == nullptr) {
		return FALSE;
	}
	if (FAILED(PathAllocCombine(system32Path, argument, PATHCCH_NONE, &dllArgPath))) {
		ERR("Failed in the construction of DLL source path ");
		LocalFree(dllCmdPath);
		return FALSE;
	}
	if (dllArgPath == nullptr) {
		LocalFree(dllCmdPath);
		return FALSE;
	}
	HINSTANCE hInstance = ShellExecute(NULL, L"open", dllCmdPath, dllArgPath, NULL, SW_SHOWDEFAULT);
	if ((int)(intptr_t)(hInstance) <= 32) {
		ERR(" Failed to load DVenabler, last error = %ld", GetLastError());
	}
	else {
		result = TRUE;
		DBGPRINT("DVenabler started Successfully by rundll32.exe");
	}

	LocalFree(dllCmdPath);
	LocalFree(dllArgPath);
	return result;
}

/*******************************************************************************
*
* Description
*
* dvCopyDLL: Copies DVEnabler.dll to system32 path for starting during every system boot.
*
* Parameters
*   none.
*
* Return val
* BOOL - TRUE on success, FALSE on failure
*
******************************************************************************/
static BOOL dvCopyDLL()
{
	TCHAR system32Path[MAX_PATH];
	TCHAR dllDestPath[MAX_PATH];
	TCHAR dllSrcPath[MAX_PATH];
	TRACING();

	if (!GetSystemDirectory(system32Path, MAX_PATH)) {
		ERR("Error in fetching system path");
		return FALSE;
	}

	if (FAILED(PathCchCombine(dllDestPath, MAX_PATH, system32Path, DVINSTALLER_DLL_NAME))) {
		ERR("Failed in the construction of Destination system path");
		return FALSE;
	}

	if (FAILED(PathCchCombine(dllSrcPath, MAX_PATH, L".\\", DVINSTALLER_DLL_NAME))) {
		ERR("Failed in the construction of DLL Destination source path ");
		return FALSE;
	}

	if (!CopyFile(dllSrcPath, dllDestPath, FALSE)) {
		ERR("Failed to Copy to the system path with error %d", GetLastError());
		return FALSE;
	}
	//Extra check
	if (GetFileAttributes(dllDestPath) == INVALID_FILE_ATTRIBUTES) {
		ERR("dllDestPath doesn't exist");
		return FALSE;
	}
	return TRUE;
}

/*******************************************************************************
*
* Description
*
* dvIsUmdLoaded: to check if DVenabler is running or not.
*
*
* Parameters
*   none.
*
* Return val
* BOOL - TRUE on success, FALSE on failure
*
******************************************************************************/
static BOOL dvIsUmdLoaded(void)
{
	FILE* pPipe;
	BOOL umdLoaded = FALSE;
	const char* dllName = "dvserver.dll";
	const char* processName = "WUDFhost.exe";
	char command[256], psBuffer[256];
	TRACING();

	snprintf(command, sizeof(command), "tasklist /m /fi \"IMAGENAME eq %s\" | find /i \"dvserver.dll\"", processName);

	if ((pPipe = _popen(command, "rt")) == NULL) {
		ERR("UMD not loaded");
		return umdLoaded;
	}
	/* Read pipe until end of file, or an error occurs. */
	while (fgets(psBuffer, sizeof(psBuffer), pPipe))
	{
		if (strstr(psBuffer, dllName)) {
			umdLoaded = TRUE;
			DBGPRINT("UMD loaded");
			break;
		}
	}
	_pclose(pPipe);
	return umdLoaded;
}

/*******************************************************************************
*
* Description
*
* dvDisplaySwitch: Enables the display path.
*
* Parameters
*   none.
*
******************************************************************************/
static void dvDisplaySwitch()
{
	DISPLAYCONFIG_TARGET_BASE_TYPE baseType;
	unsigned int path_count = NULL, mode_count = NULL;
	char err[256];

	TRACING();
	memset(err, 0, 256);
	/* Initializing the baseType.baseOutputTechnology to default OS value(failcase) */
	baseType.baseOutputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
	/* Step 0: Get the size of buffers w.r.t active paths and modes, required for QueryDisplayConfig */
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
		ERR("GetDisplayConfigBufferSizes failed with %s. Exiting!!!\n", err);
		return;
	}

	try {
		/* Initializing STL vectors for all the paths and its respective modes */
		std::vector<DISPLAYCONFIG_PATH_INFO> path_list(path_count);
		std::vector<DISPLAYCONFIG_MODE_INFO> mode_list(mode_count);

		/* Step 1: Retrieve information about all possible display paths for all display devices */
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, path_list.data(), &mode_count, mode_list.data(), nullptr) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("QueryDisplayConfig failed with %s. Exiting!!!\n", err);
			return;
		}
		for (auto& activepath_loopindex : path_list) {
			baseType.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE;
			baseType.header.size = sizeof(baseType);
			baseType.header.adapterId = activepath_loopindex.sourceInfo.adapterId;
			baseType.header.id = activepath_loopindex.targetInfo.id;
			/* Step 2 : DisplayConfigGetDeviceInfo function retrieves display configuration information about the device */
			if (DisplayConfigGetDeviceInfo(&baseType.header) != ERROR_SUCCESS) {
				DBGPRINT("DisplayConfigGetDeviceInfo failed... Continuing with other active paths!!!\n");
				continue;
			}

			DBGPRINT("baseType.baseOutputTechnology = %d\n", baseType.baseOutputTechnology);
			if (baseType.baseOutputTechnology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {
				/* Step 4: Clear the DISPLAYCONFIG_PATH_INFO.flags for MSFT path*/
				activepath_loopindex.flags = 0;
				DBGPRINT("Clearing Microsoft activepath_loopindex.flags.\n");
			}
			else {
				/* Move the IDD source co-ordinates to (0,0)  if MSBDA monitor is listed as first monitor in the path list*/
				mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x = 0;
				mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y = 0;
				DBGPRINT("x, y  = %dX%x\n", mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x,
					mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y);
			}
		}
		if (SetDisplayConfig(path_count, path_list.data(), mode_count, mode_list.data(), \
			SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("SetDisplayConfig failed with %s\n", err);
			return;
		}
	}
	catch (const std::exception& e) {
		ERR("an exception occurred while changing the display path");
	}
}

/*******************************************************************************
*
* Description
*
* dvPostInstall: Post Install of DV driver changes
*
* Parameters
*   none.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
******************************************************************************/
int dvPostInstall(void)
{
	const char* dllName = DVINSTALLER_DVSERVERDLL_NAME;
	WCHAR infFullPath[MAX_PATH];
	int err, count = DLL_LOADED_MAX_RETRY;

	TRACING();
	do {
		if (dvIsUmdLoaded()) {
			break;
		}
	} while (--count);
	DBGPRINT("umd retry count %d", count);
	if (!count) {
		DBGPRINT("UMD is not loaded,installing UMD driver manually");

		if (dvInstall(INSTALL, TRUE)) {
			ERR("manual installation of UMD failed");
		}
		else {
			DBGPRINT("manual installation of UMD is successful");
		}

		if (!dvIsUmdLoaded()) {
			DBGPRINT("uninstall DVServerKMD as Manual Install of UMD failed to load DVServerUMD");
			if (dvUninstallKmdAndUmd(FALSE)) {
				DBGPRINT("dvUninstallKmdAndUmd succeeded to uninstall\n");
			}
			else {
				ERR("dvUninstallKmdAndUmd Failed to uninstall\n");
			}
			return EXIT_FAIL;
		}
		else {
			DBGPRINT("UMD loaded after Manual install\n");
		}
	}

	try {
		std::wstring dvEnabler = DVINSTALLER_DLL_NAME;
		/* Before removing the existing dvserver.dll, check if the dll is running as service
		if it is running, then kill the service and then remove the dll. */
		dvKillDll(std::move(dvEnabler));
	}
	catch (const std::bad_alloc& e) {
		ERR("an exception occurred while trying to kill DVenabler and DvInstaller task");
	}
	if (dvCopyDLL()) {
		if (dvRegisterTaskSchedular()) {
			if (!dvStartDLL()) {
				ERR("\nFailed to start DVEnabler.dll, Manually enabling the display path ");
				dvDisplaySwitch();
			}
			return EXIT_OK;
		}
	}
	return EXIT_FAIL;
}

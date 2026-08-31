/*===========================================================================
; IntelVirtDisplayEnabler.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file will disable MSFT Display Path (MBDA)
;--------------------------------------------------------------------------*/

#include "pch.h"
#include "Trace.h"
#include "IntelVirtDisplayEnabler.tmh"
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <sddl.h>
#include "Trace_override.h"

int intelvirtdisplayenabler_init()
{
	WPP_INIT_TRACING(NULL);
	TRACING();
	DBGPRINT("IntelVirtDisplayEnabler init dve_event\n");
	DISPLAYCONFIG_TARGET_BASE_TYPE baseType;
	HANDLE hp_event = NULL;
	HANDLE dve_event = NULL;
	char err[256];
	memset(err, 0, 256);
	int status;
	unsigned int path_count = NULL, mode_count = NULL;
	bool found_id_path = FALSE, found_non_id_path = FALSE;
	disp_info dinfo = {0};
	/* Initializing the baseType.baseOutputTechnology to default OS value(failcase) */
	baseType.baseOutputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;

	// Create Security Descriptor for HOTPLUG_EVENT.
	// Grants SYNCHRONIZE | EVENT_MODIFY_STATE to Local System (SY), Local Service (LS),
	// and the interactive user (IU) only. Matches the descriptor used in IntelVirtDisplayUMD.
	PSECURITY_DESCRIPTOR hp_psd = NULL;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			L"D:(A;;0x00100002;;;SY)(A;;0x00100002;;;LS)(A;;0x00100002;;;IU)", SDDL_REVISION_1, &hp_psd, NULL)) {
		ERR("Failed to create security descriptor for HOTPLUG event, error: %d\n", GetLastError());
		WPP_CLEANUP();
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	SECURITY_ATTRIBUTES hp_sa = {0};
	hp_sa.nLength = sizeof(hp_sa);
	hp_sa.lpSecurityDescriptor = hp_psd;
	hp_sa.bInheritHandle = FALSE;

	hp_event = CreateEvent(&hp_sa, FALSE, FALSE, HOTPLUG_EVENT);
	DWORD hp_last_error = GetLastError();
	LocalFree(hp_psd);
	hp_psd = NULL;
	if (NULL == hp_event) {
		if (hp_last_error == ERROR_ACCESS_DENIED) {
			DBGPRINT("HOTPLUG_EVENT already exists, opening by name\n");
			hp_event = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, HOTPLUG_EVENT);
		}
		if (NULL == hp_event) {
			ERR("Cannot create or open HOTPLUG event! GetLastError: %d\n", GetLastError());
			WPP_CLEANUP();
			return INTELVIRTDISPLAYENABLER_FAILURE;
		}
	}

	// Create Security Descriptor for DVE_EVENT.
	// Grants SYNCHRONIZE | EVENT_MODIFY_STATE to Local System (SY), Local Service (LS),
	// and the interactive user (IU) only. Matches the descriptor used in IntelVirtDisplayUMD.
	PSECURITY_DESCRIPTOR dve_psd = NULL;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
			L"D:(A;;0x00100002;;;SY)(A;;0x00100002;;;LS)(A;;0x00100002;;;IU)", SDDL_REVISION_1, &dve_psd, NULL)) {
		ERR("Failed to create security descriptor for DVE event, error: %d\n", GetLastError());
		WPP_CLEANUP();
		CloseHandle(hp_event);
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	SECURITY_ATTRIBUTES dve_sa = {0};
	dve_sa.nLength = sizeof(dve_sa);
	dve_sa.lpSecurityDescriptor = dve_psd;
	dve_sa.bInheritHandle = FALSE;

	dve_event = CreateEvent(&dve_sa, FALSE, FALSE, DVE_EVENT);
	DWORD dve_last_error = GetLastError();
	LocalFree(dve_psd);
	dve_psd = NULL;
	if (NULL == dve_event) {
		if (dve_last_error == ERROR_ACCESS_DENIED) {
			DBGPRINT("DVE_EVENT already exists, opening by name\n");
			dve_event = OpenEvent(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, DVE_EVENT);
		}
		if (NULL == dve_event) {
			ERR("Cannot create or open DVE event! GetLastError: %d\n", GetLastError());
			WPP_CLEANUP();
			CloseHandle(hp_event);
			return INTELVIRTDISPLAYENABLER_FAILURE;
		}
	}

	while (1) {
		if (IsSystemLocked()) {
			DBGPRINT("System is in locked state, so wait untill system gets unlocked");
			continue;
		}

		// Reset the flags before doing QDC
		path_count = NULL, mode_count = NULL;
		found_id_path = FALSE, found_non_id_path = FALSE;

		/* Step 0: Get the size of buffers w.r.t active paths and modes, required for QueryDisplayConfig */
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
						   err, 255, NULL);
			ERR("GetDisplayConfigBufferSizes failed with %s. Exiting!!!\n", err);
			continue;
		}

		/* Initializing STL vectors for all the paths and its respective modes */
		std::vector<DISPLAYCONFIG_PATH_INFO> path_list(path_count);
		std::vector<DISPLAYCONFIG_MODE_INFO> mode_list(mode_count);

		// Get the Display info shared from IntelVirtDisplayUMD
		if (GetDisplayCount(&dinfo) == INTELVIRTDISPLAYENABLER_FAILURE) {
			ERR("shared mem read failed");
			goto end;
		}

		/* Step 1: Retrieve information about all possible display paths for all display devices */
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, path_list.data(), &mode_count, mode_list.data(),
							   nullptr) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
						   err, 255, NULL);
			ERR("QueryDisplayConfig failed with %s. Exiting!!!\n", err);
			continue;
		}

		for (auto &activepath_loopindex : path_list) {
			baseType.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE;
			baseType.header.size = sizeof(baseType);
			baseType.header.adapterId = activepath_loopindex.sourceInfo.adapterId;
			baseType.header.id = activepath_loopindex.targetInfo.id;

			/* Step 2 : DisplayConfigGetDeviceInfo function retrieves display configuration information about the device
			 */
			if (DisplayConfigGetDeviceInfo(&baseType.header) != ERROR_SUCCESS) {
				ERR("DisplayConfigGetDeviceInfo failed... Continuing with other active paths!!!\n");
				continue;
			}

			DBGPRINT("baseType.baseOutputTechnology = %d\n", baseType.baseOutputTechnology);
			if (!(found_non_id_path && found_id_path)) {
				/* Step 3: Check for the "outputTechnology" it should be
				   "DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED" for IDD path ONLY, In case of MSFT display we need
				   to disable the active display path  */
				if (baseType.baseOutputTechnology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {

					/* Step 4: Clear the DISPLAYCONFIG_PATH_INFO.flags for MSFT path*/
					activepath_loopindex.flags = 0;
					DBGPRINT("Clearing Microsoft activepath_loopindex.flags.\n");
					found_non_id_path = true;
				} else {
					/* Move the IDD source co-ordinates to (0,0)  if MSBDA monitor is listed as first monitor in the
					 * path list*/
					if (found_non_id_path && !found_id_path) {
						mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x = 0;
						mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y = 0;
						DBGPRINT("x, y  = %dX%x\n",
								 mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x,
								 mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y);
					}
					found_id_path = true;
				}
			}
		}

		if ((found_non_id_path && (path_count != static_cast<unsigned int>(dinfo.disp_count + 1))) ||
			(!found_non_id_path && (path_count != static_cast<unsigned int>(dinfo.disp_count)))) {
			if (found_non_id_path) {
				DBGPRINT("MSFT display is present. Path count not updated, so loop again");
			} else {
				DBGPRINT("MSFT display is not present. Path count not updated, so loop again");
			}
			DBGPRINT("disp_count = %d, path count = %d", dinfo.disp_count, path_count);
			continue;
		}

		if (found_non_id_path && found_id_path) {
			/* Step 5: SetDisplayConfig modifies the display topology by exclusively enabling/disabling the specified
					   paths in the current session. */
			if (SetDisplayConfig(path_count, path_list.data(), mode_count, mode_list.data(),
								 SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE) != ERROR_SUCCESS) {
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
							   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
				ERR("SetDisplayConfig failed with %s\n", err);
				continue;
			}
		} else {
			DBGPRINT("Skipping SetDisplayConfig as did not find ID and non-ID path. found_non_id_path = %d, "
					 "found_id_path = %d\n",
					 found_non_id_path, found_id_path);
		}

		/*If there is any display config change at the time of reboot / shutdown.
		At this stage, Since the IntelVirtDisplayEnabler is not running, Changed display config will not be saved in windows
		persistence, So at this case MSFT path will be enabled and since the DV enabler starts only after user login The
		login page  will have blank screen after boot, untill we enter the password. To over come this blank out
		issue...In UMD always we will always boot with single display config After login, IntelVirtDisplayEnabler will set the below
		event to enable the HPD path Once this event is set our IntelVirtDisplay UMD driver will enable the Hot plug path and
		get the display status from KMD So this event is Set once after every boot to enable the HPD path in our
		IntelVirtDisplay UMD driver */
		status = SetEvent(hp_event);
		if (status == NULL) {
			ERR(" Set HPevent failed with error [%d]\n ", GetLastError());
			continue;
		}

	end:
		// wait for arraival or departure call from UMD
		WaitForSingleObject(dve_event, INFINITE);
	}

	return 0;
}

int GetDisplayCount(disp_info *pdinfo)
{
	HANDLE hSharedMem = OpenFileMapping(FILE_MAP_READ, FALSE, DISP_INFO);
	if (hSharedMem == NULL) {
		ERR("Failed to open shared memory section (%d)\n", GetLastError());
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	struct disp_info *pSharedMem = (struct disp_info *)MapViewOfFile(hSharedMem, FILE_MAP_READ, 0, 0, 0);
	if (pSharedMem == NULL) {
		ERR("Failed to map view of shared memory section (%d)\n", GetLastError());
		CloseHandle(hSharedMem);
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	// Open the named mutex by name - never read a HANDLE from shared memory,
	// as HANDLEs are per-process and invalid across process boundaries.
	HANDLE hDispMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, DISP_INFO_MUTEX);
	if (hDispMutex == NULL) {
		ERR("Failed to open named mutex for shared memory (%d)\n", GetLastError());
		UnmapViewOfFile(pSharedMem);
		CloseHandle(hSharedMem);
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	WaitForSingleObject(hDispMutex, INFINITE);
	*pdinfo = *pSharedMem;
	ReleaseMutex(hDispMutex);

	CloseHandle(hDispMutex);
	UnmapViewOfFile(pSharedMem);
	CloseHandle(hSharedMem);

	return INTELVIRTDISPLAYENABLER_SUCCESS;
}

/*******************************************************************************
 *
 * Description
 *
 * IsSystemLocked - This function is used to check if the system is in locked
 * or unlocked state.
 *
 * Parameters
 * Null
 *
 * Return val
 * int - 0 = Unlocked, -1 = ERROR, 1 = Locked
 *
 ******************************************************************************/

int IsSystemLocked()
{
	FILE *fp;
	char buffer[128];
	int status = TRUE;

	// Run the PowerShell command to get the system lock status
	fp =
		_popen("powershell.exe -WindowStyle Hidden -Command \"(quser 2>$null) -and (get-process logonui -ea 0)\"", "r");
	if (fp == NULL) {
		ERR("Failed to run PowerShell command.\n");
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	// Read the output of the PowerShell command
	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		// Check if the output is "true" (indicating locked) and act accordingly
		if (strstr(buffer, "True") != NULL) {
			DBGPRINT("System is locked\n");
			status = TRUE;
		} else if (strstr(buffer, "False") != NULL) {
			DBGPRINT("System is unlocked\n");
			status = FALSE;
		} else {
			ERR("Unexpected output\n");
			status = INTELVIRTDISPLAYENABLER_FAILURE;
		}
	}

	// Close the pipe and print any errors
	if (_pclose(fp) != 0) {
		ERR("Error occurred while running PowerShell command.\n");
		return INTELVIRTDISPLAYENABLER_FAILURE;
	}

	return status;
}
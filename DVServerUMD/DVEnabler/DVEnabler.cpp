/*===========================================================================
; DVEnabler.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file will disable MSFT Display Path (MBDA)
;--------------------------------------------------------------------------*/


#include "pch.h"
#include "Trace.h"
#include "DVEnabler.tmh"
#include <Windows.h>


int dvenabler_init()
{
	WPP_INIT_TRACING(NULL);
	TRACING();
	DBGPRINT("DVenabler init dve_event\n");
	DISPLAYCONFIG_TARGET_BASE_TYPE baseType;
	HANDLE hp_event = NULL;
	HANDLE dve_event = NULL;
	char err[256];
	memset(err, 0, 256);
	int status;
	unsigned int path_count = NULL, mode_count = NULL;
	bool found_id_path = FALSE, found_non_id_path = FALSE;
	unsigned int prev_path_count = NULL;
	bool update_prev_path_count = FALSE;
	/* Initializing the baseType.baseOutputTechnology to default OS value(failcase) */
	baseType.baseOutputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;

	//Create Security Descriptor for HOTPLUG_EVENT, To allow the DVServerUMD to access the event
	PSECURITY_DESCRIPTOR hp_psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(hp_psd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(hp_psd, TRUE, NULL, FALSE);

	SECURITY_ATTRIBUTES hp_sa = { 0 };
	hp_sa.nLength = sizeof(hp_sa);
	hp_sa.lpSecurityDescriptor = hp_psd;
	hp_sa.bInheritHandle = FALSE;

	hp_event = CreateEvent(&hp_sa, FALSE, FALSE, HOTPLUG_EVENT);
	if (NULL == hp_event) {
		ERR("Cannot create HOTPULG event!\n");
		return -1;
	}

	//Create Security Descriptor for DVE_EVENT, To allow the DVServerUMD to access the event
	PSECURITY_DESCRIPTOR dve_psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(dve_psd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(dve_psd, TRUE, NULL, FALSE);

	SECURITY_ATTRIBUTES dve_sa = { 0 };
	dve_sa.nLength = sizeof(dve_sa);
	dve_sa.lpSecurityDescriptor = dve_psd;
	dve_sa.bInheritHandle = FALSE;

	dve_event = CreateEvent(&dve_sa, FALSE, FALSE, DVE_EVENT);
	if (NULL == dve_event) {
		ERR("Cannot create DVE event!\n");
		CloseHandle(hp_event);
		return -1;
	}

	while (1)
	{
		/* Step 0: Get the size of buffers w.r.t active paths and modes, required for QueryDisplayConfig */
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("GetDisplayConfigBufferSizes failed with %s. Exiting!!!\n", err);
			continue;
		}

		/* Initializing STL vectors for all the paths and its respective modes */
		std::vector<DISPLAYCONFIG_PATH_INFO> path_list(path_count);
		std::vector<DISPLAYCONFIG_MODE_INFO> mode_list(mode_count);

		/* Step 1: Retrieve information about all possible display paths for all display devices */
		if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, path_list.data(), &mode_count, mode_list.data(), nullptr) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("QueryDisplayConfig failed with %s. Exiting!!!\n", err);
			continue;
		}

		for (auto& activepath_loopindex : path_list) {
			baseType.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE;
			baseType.header.size = sizeof(baseType);
			baseType.header.adapterId = activepath_loopindex.sourceInfo.adapterId;
			baseType.header.id = activepath_loopindex.targetInfo.id;

			/* Step 2 : DisplayConfigGetDeviceInfo function retrieves display configuration information about the device */
			if (DisplayConfigGetDeviceInfo(&baseType.header) != ERROR_SUCCESS) {
				ERR("DisplayConfigGetDeviceInfo failed... Continuing with other active paths!!!\n");
				continue;
			}

			DBGPRINT("baseType.baseOutputTechnology = %d\n", baseType.baseOutputTechnology);
			if (!(found_non_id_path && found_id_path)) {
				/* Step 3: Check for the "outputTechnology" it should be "DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED" for
						   IDD path ONLY, In case of MSFT display we need to disable the active display path  */
				if (baseType.baseOutputTechnology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {

					/* Step 4: Clear the DISPLAYCONFIG_PATH_INFO.flags for MSFT path*/
					activepath_loopindex.flags = 0;
					DBGPRINT("Clearing Microsoft activepath_loopindex.flags.\n");
					found_non_id_path = true;
				}
				else {
					found_id_path = true;
					/* Move the IDD source co-ordinates to (0,0)  if MSBDA monitor is listed as first monitor in the path list*/
					if (found_non_id_path) {
						mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x = 0;
						mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y = 0;
						DBGPRINT("x, y  = %dX%x\n", mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x,
							mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y);
					}
				}
			}
		}

		//If the Path count is same as the previous path count and if the MSDBA monitor is also not found
		//which mean after hotplug the OS not updated the current path count yet, So looping back to get the proper path count
		if ((path_count == prev_path_count) && found_non_id_path == FALSE) {
			DBGPRINT("Path count not updated... Retry QDC to get updated Path count");
			continue;
		}

		//In UMD, dve_event is set once during boot, So DVenabler will loop twice during boot,
		// so dont update the previous path count during the first loop of boot.
		if (update_prev_path_count) {
			prev_path_count = path_count;
		}

		if (found_non_id_path && found_id_path) {
			/* Step 5: SetDisplayConfig modifies the display topology by exclusively enabling/disabling the specified
					   paths in the current session. */
			if (SetDisplayConfig(path_count, path_list.data(), mode_count, mode_list.data(), \
				SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE) != ERROR_SUCCESS) {
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
				ERR("SetDisplayConfig failed with %s\n", err);
				continue;
			}
		}
		else {
			DBGPRINT("Skipping SetDisplayConfig as did not find ID and non-ID path. found_non_id_path = %d, found_id_path = %d\n",
				found_non_id_path, found_id_path);
		}

		/*If there is any display config change at the time of reboot / shutdown.
		At this stage, Since the Dvenabler is not running, Changed display config will not be saved in windows persistence,
		So at this case MSFT path will be enabled and since the DV enabler starts only after user login
		The login page  will have blank screen after boot, untill we enter the password.
		To over come this blank out issue...In UMD always we will always boot with single display config
		After login, Dvenabler will set the below event to enable the HPD path
		Once this event is set our DVserver UMD driver will enable the Hot plug path and get the display status from KMD
		So this event is Set once after every boot to enable the HPD path in our DVServer UMD driver */
		status = SetEvent(hp_event);
		if (status == NULL) {
			ERR(" Set HPevent failed with error [%d]\n ", GetLastError());
			continue;
		}

		//wait for arraival or departure call from UMD
		WaitForSingleObject(dve_event, INFINITE);

		update_prev_path_count = TRUE;
		path_count = NULL, mode_count = NULL;
		found_id_path = FALSE, found_non_id_path = FALSE;

	}
	WPP_CLEANUP();
	CloseHandle(hp_event);
	CloseHandle(dve_event);

	return 0;
}
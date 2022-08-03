/*===========================================================================
; DVEnabler.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file will disable MSFT Display Path (MBDA)
;--------------------------------------------------------------------------*/

#include <Windows.h>
#include <dxgi.h>
#include <vector>
#include <stdio.h>
#include <debug.h>

int main()
{
	set_module_name("DVEnabler");
	TRACING();

	DISPLAYCONFIG_TARGET_BASE_TYPE baseType;
	unsigned int path_count = 0, mode_count = 0;
	bool found_id_path = false, found_non_id_path = false;
	char err[256];
	memset(err, 0, 256);

	/* Step 0: Get the size of buffers w.r.t active paths and modes, required for QueryDisplayConfig */
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
		ERR("GetDisplayConfigBufferSizes failed with %s. Exiting!!!\n", err);
		return -1;
	}

	/* Initializing STL vectors for all the paths and its respective modes */
	std::vector<DISPLAYCONFIG_PATH_INFO> path_list(path_count);
	std::vector<DISPLAYCONFIG_MODE_INFO> mode_list(mode_count);

	/* Step 1: Retrieve information about all possible display paths for all display devices */
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, path_list.data(), &mode_count, mode_list.data(), nullptr) != ERROR_SUCCESS) {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
		ERR("QueryDisplayConfig failed with %s. Exiting!!!\n", err);
		return -1;
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
			} else {
				found_id_path = true;
				/* Move the IDD source co-ordinates to (0,0) */
				mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x = 0;
				mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y = 0;
				DBGPRINT("x, y  = %dX%x\n", mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x,
					mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y);
			}
		}
	}

	if (found_non_id_path && found_id_path) {
		/* Step 5: SetDisplayConfig modifies the display topology by exclusively enabling/disabling the specified 
		           paths in the current session. */
		if (SetDisplayConfig(path_count, path_list.data(), mode_count, mode_list.data(), \
			SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE) != ERROR_SUCCESS) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("SetDisplayConfig failed with %s\n", err);
			return -1;
		}
	} else {
		INFO("Skipping SetDisplayConfig as did not find ID and non-ID path. found_non_id_path = %d, found_id_path = %d\n",
			found_non_id_path, found_id_path);
	}
	
	return 0;
}
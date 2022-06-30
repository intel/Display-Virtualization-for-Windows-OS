/*===========================================================================
; DVEnabler.cpp
;----------------------------------------------------------------------------
; * Copyright © 2021 Intel Corporation
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
	unsigned int mode_index = 0;
	unsigned int path_count = 0, mode_count = 0;
	bool found_id_path = false, found_non_id_path = false;

	/* Step 0: Get the size of buffers w.r.t active paths and modes, required for QueryDisplayConfig */
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
		ERR("GetDisplayConfigBufferSizes failed... Exiting!!!\n");
		return -1;
	}

	/* Initializing STL vectors for all the paths and its respective modes */
	std::vector<DISPLAYCONFIG_PATH_INFO> path_list(path_count);
	std::vector<DISPLAYCONFIG_MODE_INFO> mode_list(mode_count);

	/* Step 1: Retrieve information about all possible display paths for all display devices */
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, path_list.data(), &mode_count, mode_list.data(), nullptr) != ERROR_SUCCESS) {
		ERR("QueryDisplayConfig failed... Exiting!!!\n");
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

		/* Step 3: Check for the "outputTechnology" it should be "DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED" for 
		           IDD path ONLY, In case of MSFT display we need to disable the active display path  */
		if (baseType.baseOutputTechnology != DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED) {
			
			/* Step 4: Clear the DISPLAYCONFIG_PATH_INFO.flags for MSFT path*/
			activepath_loopindex.flags = 0;
			DBGPRINT("Clearing Microsoft activepath_loopindex.flags\n");
			found_non_id_path = true;
		}
		else {
			found_id_path = true;
			/* Move the IDD source co-ordinates to (0,0) */
			mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.x = 0;
			mode_list[activepath_loopindex.sourceInfo.modeInfoIdx].sourceMode.position.y = 0;
		}
		mode_index = mode_index + 2;
	}

	if (found_non_id_path && found_id_path) {
		/* Step 5: SetDisplayConfig modifies the display topology by exclusively enabling/disabling the specified 
		           paths in the current session. */
		if (SetDisplayConfig(path_count, path_list.data(), mode_count, mode_list.data(), \
			SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE) != ERROR_SUCCESS) {
			ERR("SetDisplayConfig failed\n");
		}
	}
	else {
		DBGPRINT("Skipping SetDisplayConfig as did not find ID and non-ID path\n");
	}
	
	return 0;
}
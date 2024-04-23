/*===========================================================================
; dventry.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file is the entry point for SRIOV DV installer implementation code
;--------------------------------------------------------------------------*/

#include "DVInstaller.h"
#include "Trace.h"
#include "dventry.tmh"

/*******************************************************************************
*
* Description
*
* wWinMain - Entry into the dvinstaller, basically control transfer from
*            inno setup process.
*
* Parameters
*
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
******************************************************************************/
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	WCHAR infFullPath[MAX_PATH];
	InstallModes RequestedMode;
	int result = EXIT_OK, argc, unInstallLoop = 10;;
	LPWSTR commandline = GetCommandLine();
	LPWSTR* argv;

	WPP_INIT_TRACING(NULL);
	TRACING();
	DBGPRINT("DV installation begins\n");

	argv = CommandLineToArgvW(commandline, &argc);
	if (argc < 2) {
		ERR("no arguments passed\n");
		WPP_CLEANUP();
		return EXIT_FAIL;
	}

	RequestedMode = (InstallModes)_wtoi(argv[1]);
	switch (RequestedMode) {
	case INSTALL:
	case UPDATE:
		DBGPRINT("RequestedMode is %d\n", RequestedMode);
		if (dvUpdateCertifcates()) {
			ERR("Failed to add certificates\n");
			return EXIT_FAIL;
		}
		if (dvInstall(RequestedMode, FALSE)) {
			ERR("dvInstall: Failed to %d\n", RequestedMode);
		}
		else {
			DBGPRINT("dvInstall: succeeded to %d\n", RequestedMode);
			result = EXIT_FAIL;
		}
		break;
	case UNINSTALL:
		DBGPRINT("RequestedMode is UNINSTALL\n");
		while (unInstallLoop--) {
			if (dvUninstallKmdAndUmd(TRUE))	{
				DBGPRINT("dvUninstallKmdAndUmd succeeded to uninstall in interation =  %d\n", unInstallLoop);
				break;
			}
			else {
				ERR("dvUninstallKmdAndUmd Failed to uninstall in iteration = %d\n", unInstallLoop);
				Sleep(1000);
			}
		}
		break;
	case UNINSTALLANDUPDATE:
		DBGPRINT("RequestedMode is UNINSTALLANDUPDATE\n");

		if (dvUninstallKmdAndUmd(TRUE)) {
			if (dvUpdateCertifcates()) {
				ERR("Failed to add certificates\n");
				return EXIT_FAIL;
			}
			if (dvInstall(INSTALL, FALSE)) {
				ERR("UNINSTALLANDUPDATE Failed to update\n");
			}
			else {
				DBGPRINT("UNINSTALLANDUPDATE  Succeded to update\n");
				result = EXIT_OK;
			}
		}
		else {
			ERR("dvUninstallKmdAndUmd Failed to uninstall before update\n");
		}
		break;
	default:
		break;
	}
	WPP_CLEANUP();
	return result;
}

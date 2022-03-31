/*===========================================================================
; Main.cpp
;----------------------------------------------------------------------------
; * Copyright © 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file declares the IOTG DVserverUMD_Node creation implemenatation
;--------------------------------------------------------------------------*/
#include <iostream>
#include <vector>

#include <windows.h>
#include <swdevice.h>
#include <conio.h>
#include <wrl.h>

/* debug output support */
#define WriteToLog(x)  OutputDebugStringA(x)

VOID WINAPI
CreationCallback(
	_In_ HSWDEVICE hSwDevice,
	_In_ HRESULT hrCreateResult,
	_In_opt_ PVOID pContext,
	_In_opt_ PCWSTR pszDeviceInstanceId
)
{
	UNREFERENCED_PARAMETER(hSwDevice);
	UNREFERENCED_PARAMETER(hrCreateResult);
	UNREFERENCED_PARAMETER(pszDeviceInstanceId);

	HANDLE hEvent = *(HANDLE*)pContext;
	SetEvent(hEvent);
}

int __cdecl main(int argc, wchar_t* argv[])
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);

	HANDLE hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	HSWDEVICE hSwDevice;
	SW_DEVICE_CREATE_INFO createInfo = { 0 };
	PCWSTR description = L"DVServerUMD Node";

	// These match the Pnp id's in the inf file so OS will load the driver when the device is created    
	PCWSTR instanceId = L"DVServer";
	PCWSTR hardwareIds = L"DVServer\0\0";
	PCWSTR compatibleIds = L"DVServer\0\0";

	createInfo.cbSize = sizeof(createInfo);
	createInfo.pszzCompatibleIds = compatibleIds;
	createInfo.pszInstanceId = instanceId;
	createInfo.pszzHardwareIds = hardwareIds;
	createInfo.pszDeviceDescription = description;

	createInfo.CapabilityFlags = SWDeviceCapabilitiesRemovable |
		SWDeviceCapabilitiesSilentInstall |
		SWDeviceCapabilitiesDriverRequired;

	// Create the device
	HRESULT hr = SwDeviceCreate(L"DVServer",
		L"HTREE\\ROOT\\0",
		&createInfo,
		0,
		nullptr,
		CreationCallback,
		&hEvent,
		&hSwDevice);
	if (FAILED(hr))
	{
		WriteToLog("DVServerUMD_Node : SwDeviceCreate failed");
		return 1;
	}

	// Wait for callback to signal that the device has been created
	WriteToLog("DVServerUMD_Node : Waiting for device to be created....");
	DWORD waitResult = WaitForSingleObject(hEvent, 10 * 1000);
	if (waitResult != WAIT_OBJECT_0)
	{
		WriteToLog("DVServerUMD_Node : Wait for device creation failed");
		SwDeviceClose(hSwDevice);
		return 1;
	}
	WriteToLog("DVServerUMD_Node : Device created");

	// Now wait for user to indicate the device should be stopped
	bool bExit = false;
	do
	{
		// Wait for key press
		int key = _getch();

		if (key == 'x' || key == 'X')
		{
			bExit = true;
		}
	} while (!bExit);

	// Stop the device, this will cause the sample to be unloaded
	SwDeviceClose(hSwDevice);

	return 0;
}
/*===========================================================================
; DVServeredid.cpp
;----------------------------------------------------------------------------
; * Copyright © 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file sends edid ioctl request to kmd & trim the unsupported modelist
;--------------------------------------------------------------------------*/

#include "DVServeredid.h"
#include <debug.h>

using namespace Microsoft::IndirectDisp;
extern struct IndirectSampleMonitor s_SampleMonitors[];
PSP_DEVICE_INTERFACE_DETAIL_DATA device_iface_edid_data;

unsigned int blacklisted_resolution_list[][2] = { {1400,1050} }; // blacklisted resolution can be appended here

/*******************************************************************************
*
* Description
*
* get_dvserver_edid_kmdf_device - This function checks for the DVserverKMD edid
* device node created and opens a handle for DVServerUMD to use
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int get_dvserver_edid_kmdf_device(void)
{
	TRACING();
	HDEVINFO devinfo_edid_handle = NULL;
	DWORD sizeof_deviceinterface_buf = 0;
	BOOL ret = FALSE;
	SP_DEVICE_INTERFACE_DATA device_interface_data;

	device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	devinfo_edid_handle = SetupDiGetClassDevs(NULL, NULL, NULL, DEVINFO_FLAGS);

	ret = SetupDiEnumDeviceInterfaces(devinfo_edid_handle, 0, &GUID_DEVINTERFACE_DVSERVERKMD, 0, &device_interface_data);
	if (ret == FALSE) {
		ERR("SetupDiEnumDeviceInterfaces failed\n");
		return DVSERVERUMD_FAILURE;
	}

	SetupDiGetDeviceInterfaceDetail(devinfo_edid_handle, &device_interface_data, 0, 0, &sizeof_deviceinterface_buf, 0);
	if (sizeof_deviceinterface_buf == 0) {
		ERR("SetupDiGetDeviceInterfaceDetail failed\n");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_edid_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(sizeof_deviceinterface_buf);
	if (device_iface_edid_data == NULL) {
		ERR("Failed allocating memory for device interface data\n");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_edid_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
	ret = SetupDiGetDeviceInterfaceDetail(devinfo_edid_handle, &device_interface_data, device_iface_edid_data, sizeof_deviceinterface_buf, 0, 0);
	if (ret == FALSE) {
		ERR("SetupDiGetDeviceInterfaceDetail Failed\n");
		return DVSERVERUMD_FAILURE;
	}
	return DVSERVERUMD_SUCCESS;
}

/*******************************************************************************
*
* Description
*
* get_edid_data - This function gets the handle of DVserverKMD edid device node
* and send an ioctl(IOCTL_DVSERVER_GET_EDID_DATA)to DVserverKMD to get edid data
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int get_edid_data(void)
{
	TRACING();
	unsigned int i= 0, edid_mode_index = 0;
	HANDLE devHandle_edid;
	struct edid_info* edata = NULL;
	ULONG bytesReturned = 0;

	if (get_dvserver_edid_kmdf_device() == -1) {
		ERR("KMD resource Init Failed\n");
		return DVSERVERUMD_FAILURE;
	}

	devHandle_edid = CreateFile(device_iface_edid_data->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	if (devHandle_edid == INVALID_HANDLE_VALUE) {
		ERR("CreateFile for EDID returned INVALID_HANDLE_VALUE\n");
		return DVSERVERUMD_FAILURE;
	}

	edata = (struct edid_info*)malloc(sizeof(struct edid_info));
	if (edata == NULL) {
		ERR("Failed to allocate edid structure\n");
		CloseHandle(devHandle_edid);
		return DVSERVERUMD_FAILURE;
	}
	SecureZeroMemory(edata, sizeof(struct edid_info));

	DBGPRINT("Requesting Mode List size through EDID IOCTL\n");
	if (!DeviceIoControl(devHandle_edid, IOCTL_DVSERVER_GET_EDID_DATA, edata, sizeof(struct edid_info), edata, sizeof(struct edid_info), &bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_GET_EDID_DATA call failed\n");
		free(edata);
		CloseHandle(devHandle_edid);
		return DVSERVERUMD_FAILURE;
	}

	edata->mode_list = (struct mode_info*)malloc(sizeof(struct mode_info) * edata->mode_size);
	if (edata->mode_list == NULL) {
		ERR("Failed to allocate mode list structure\n");
		free(edata);
		CloseHandle(devHandle_edid);
		return DVSERVERUMD_FAILURE;
	}
	SecureZeroMemory(edata->mode_list, sizeof(struct mode_info) * edata->mode_size);

	DBGPRINT("Requesting EDID Data through EDID IOCTL\n");
	if (!DeviceIoControl(devHandle_edid, IOCTL_DVSERVER_GET_EDID_DATA, edata, sizeof(struct edid_info), edata, sizeof(struct edid_info), &bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_GET_EDID_DATA call failed!\n");
		free(edata->mode_list);
		free(edata);
		CloseHandle(devHandle_edid);
		return DVSERVERUMD_FAILURE;
	}
	
	memcpy_s(s_SampleMonitors[0].pEdidBlock, s_SampleMonitors->szEdidBlock, edata->edid_data, s_SampleMonitors->szEdidBlock);

	for (i=0; i< edata->mode_size; i++) {
		//TRIMMING LOGIC: Restricting EDID size to 32 and discarding modes with width more than 3840 & less than 1024
		if ((edata->mode_list[i].width <= WIDTH_UPPER_CAP) && (edata->mode_list[i].width >= WIDTH_LOWER_CAP) && (edid_mode_index < s_SampleMonitors->szModeList) && (is_blacklist(edata->mode_list[i].width, edata->mode_list[i].height) == 0)) {
			s_SampleMonitors[0].pModeList[edid_mode_index].Width = edata->mode_list[i].width;
			s_SampleMonitors[0].pModeList[edid_mode_index].Height = edata->mode_list[i].height;
			if ((DWORD)edata->mode_list[i].refreshrate == REFRESH_RATE_59)
				s_SampleMonitors[0].pModeList[edid_mode_index].VSync = REFRESH_RATE_60;
			else
				s_SampleMonitors[0].pModeList[edid_mode_index].VSync = (DWORD)edata->mode_list[i].refreshrate;
			edid_mode_index++;
		}
	}
	free(edata->mode_list);
	free(edata);
	CloseHandle(devHandle_edid);
	return DVSERVERUMD_SUCCESS;
}

/*******************************************************************************
*
* Description
*
* is_blacklist - This function blacklists the resolution it receives from
* DVserverKMD comparing it from the unsuported reolutions formats which get 
* recieved from Qemu.
*
* Parameters
* width - resolution width
* height - resolution height
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int is_blacklist(unsigned int width, unsigned int height) {
	TRACING();
	unsigned int i = 0;

	for (i = 0; i < ARRAY_SIZE(blacklisted_resolution_list); i++) {
		if ((width == blacklisted_resolution_list[i][0]) && (height == blacklisted_resolution_list[i][1])) {
			return DVSERVERUMD_FAILURE;
		}
	}
	return DVSERVERUMD_SUCCESS;
}
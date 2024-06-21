/*===========================================================================
; DVServeredid.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file sends edid ioctl request to kmd & trim the unsupported modelist
;--------------------------------------------------------------------------*/

#include "DVServeredid.h"
#include<DVServeredid.tmh>

using namespace Microsoft::IndirectDisp;
PSP_DEVICE_INTERFACE_DETAIL_DATA device_iface_edid_data;
struct edid_info* edata = NULL;
struct screen_info* mdata = NULL;
ULONG bytesReturned = 0;

unsigned int blacklisted_resolution_list[][2] = { {1400,1050} }; // blacklisted resolution can be appended here

/*******************************************************************************
*
* Description
*
* get_edid_data - This function gets the handle of DVserverKMD edid device node
* and send an ioctl(IOCTL_DVSERVER_GET_EDID_DATA)to DVserverKMD to get edid data
*
* Parameters
* Device frame Handle to DVServerKMD
* pointer to IndirectSampleMonitor structure
* Screen ID
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int get_edid_data(HANDLE devHandle, void *m, DWORD id, BOOL d_edid)
{
	TRACING();
	IndirectSampleMonitor* monitor = (IndirectSampleMonitor*)m;
	unsigned int i = 0, edid_mode_index = 0, modeSize;

	if (!devHandle || !m) {
		ERR("Invalid parameter\n");
		return DVSERVERUMD_FAILURE;
	}

	static const struct IndirectSampleMonitor s_SampleMonitors[] =
		{
				// 1080 p EDID
			{
				{
					0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x49, 0x14, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00,
					0x2a, 0x18, 0x01, 0x04, 0xa5, 0x20, 0x14, 0x78, 0x06, 0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26,
					0x0f, 0x50, 0x54, 0x21, 0x08, 0x00, 0xe1, 0xc0, 0xd1, 0xc0, 0xd1, 0x00, 0xa9, 0x40, 0xb3, 0x00,
					0x95, 0x00, 0x81, 0x80, 0x81, 0x40, 0xea, 0x29, 0x00, 0xc0, 0x51, 0x20, 0x1c, 0x30, 0x40, 0x26,
					0x44, 0x40, 0x45, 0xcb, 0x10, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xf7, 0x00, 0xa0, 0x00, 0x40,
					0x82, 0x00, 0x28, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x32,
					0x7d, 0x1e, 0xa0, 0xff, 0x01, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc,
					0x00, 0x51, 0x45, 0x4d, 0x55, 0x20, 0x4d, 0x6f, 0x6e, 0x69, 0x74, 0x6f, 0x72, 0x0a, 0x01, 0x3a,
					0x02, 0x03, 0x0b, 0x00, 0x46, 0x7d, 0x65, 0x60, 0x59, 0x1f, 0x61, 0x00, 0x00, 0x00, 0x10, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f
				},
				{
					{ 1920, 1080, 60 },
					{ 1600, 1200, 60 },
					{ 1024, 768, 60 },
				},
				0
			},
		};

	if (d_edid == TRUE) {
		ERR("get Default EDID for Primary Index monitor \n");
		memcpy_s(monitor, sizeof(s_SampleMonitors), s_SampleMonitors, sizeof(s_SampleMonitors));
		return DVSERVERUMD_SUCCESS;
	}

	edata = (struct edid_info*)malloc(sizeof(struct edid_info));
	if (edata == NULL) {
		ERR("Failed to allocate edid structure\n");
		return DVSERVERUMD_FAILURE;
	}
	SecureZeroMemory(edata, sizeof(struct edid_info));
	edata->screen_num = id;

	DBGPRINT("Requesting Mode List size through EDID IOCTL for screen = %d\n", edata->screen_num);
	if (!DeviceIoControl(devHandle, IOCTL_DVSERVER_GET_EDID_DATA, edata, sizeof(struct edid_info), edata, sizeof(struct edid_info), &bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_GET_EDID_DATA call failed\n");
		free(edata);
		return DVSERVERUMD_FAILURE;
	}
	if (edata->mode_size > MODE_LIST_MAX_SIZE) {
		ERR("Invalid id \n");
		free(edata);
		return DVSERVERUMD_FAILURE;
	}

	/*Resetting the edata buffer for coverity*/
	modeSize = edata->mode_size;
	SecureZeroMemory(edata, sizeof(struct edid_info));
	edata->screen_num = id;
	edata->mode_size = modeSize;

	edata->mode_list = (struct mode_info*)malloc(sizeof(struct mode_info) * edata->mode_size);
	if (edata->mode_list == NULL) {
		ERR("Failed to allocate mode list structure\n");
		free(edata);
		return DVSERVERUMD_FAILURE;
	}
	SecureZeroMemory(edata->mode_list, sizeof(struct mode_info) * edata->mode_size);

	DBGPRINT("Requesting EDID Data through EDID IOCTL for screen = %d\n", edata->screen_num);
	if (!DeviceIoControl(devHandle, IOCTL_DVSERVER_GET_EDID_DATA, edata, sizeof(struct edid_info), edata, sizeof(struct edid_info), &bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_GET_EDID_DATA call failed!\n");
		free(edata->mode_list);
		free(edata);
		return DVSERVERUMD_FAILURE;
	}
	/*Rechecking for possible out of range */
	if (edata->mode_size > MODE_LIST_MAX_SIZE) {
		ERR("mode list is corrupted \n");
		free(edata->mode_list);
		free(edata);
		return DVSERVERUMD_FAILURE;
	}

	memcpy_s(monitor->pEdidBlock, monitor->szEdidBlock, edata->edid_data, monitor->szEdidBlock);
	monitor->ulPreferredModeIdx = 0;

	DBGPRINT("Modes\n");
	for (i = 0; i < edata->mode_size; i++) {
		//TRIMMING LOGIC: Restricting EDID size to 32 and discarding modes with width more than 3840 & less than 1024
		if ((edata->mode_list[i].width <= WIDTH_UPPER_CAP) &&
			(edata->mode_list[i].width >= WIDTH_LOWER_CAP) &&
			(edid_mode_index < monitor->szModeList) &&
			(is_blacklist(edata->mode_list[i].width, edata->mode_list[i].height) == 0)) {
			monitor->pModeList[edid_mode_index].Width = edata->mode_list[i].width;
			monitor->pModeList[edid_mode_index].Height = edata->mode_list[i].height;
			if ((DWORD)edata->mode_list[i].refreshrate == REFRESH_RATE_59)
				monitor->pModeList[edid_mode_index].VSync = REFRESH_RATE_60;
			else
				monitor->pModeList[edid_mode_index].VSync = (DWORD)edata->mode_list[i].refreshrate;
			DBGPRINT("[%d]: %dx%d@%d\n", edid_mode_index,
				monitor->pModeList[edid_mode_index].Width, monitor->pModeList[edid_mode_index].Height,
				monitor->pModeList[edid_mode_index].VSync);
			edid_mode_index++;
		}
	}
	free(edata->mode_list);
	free(edata);
	return DVSERVERUMD_SUCCESS;
}

/*******************************************************************************
*
* Description
*
* get_total_screens - This function sends an ioctl(IOCTL_DVSERVER_GET_TOTAL_SCREENS)
* to DVserverKMD to get the total number of screens
*
* Parameters
* Device frame Handle to DVServerKMD
*
* Return val
* int - -1 = ERROR, any other value = number of total screens connected
*
******************************************************************************/
int get_total_screens(HANDLE devHandle)
{
	TRACING();
	int ret = DVSERVERUMD_FAILURE;

	if (!devHandle) {
		ERR("Invalid devHandle\n");
		return ret;
	}

	mdata = (struct screen_info*)malloc(sizeof(struct screen_info));
	if (mdata == NULL) {
		ERR("Failed to allocate Screen structure\n");
		return ret;
	}
	SecureZeroMemory(mdata, sizeof(struct screen_info));

	DBGPRINT("Requesting Screen Count through Screen IOCTL\n");
	if (!DeviceIoControl(devHandle, IOCTL_DVSERVER_GET_TOTAL_SCREENS, mdata, sizeof(struct screen_info), mdata, sizeof(struct screen_info), &bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_GET_TOTAL_SCREENS call failed\n");
		free(mdata);
		return ret;
	}

	DBGPRINT("Total screens = %d\n", mdata->total_screens);
	ret = mdata->total_screens;
	free(mdata);
	return ret;
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
int is_blacklist(unsigned int width, unsigned int height)
{
	unsigned int i = 0;

	for (i = 0; i < ARRAY_SIZE(blacklisted_resolution_list); i++) {
		if ((width == blacklisted_resolution_list[i][0]) && (height == blacklisted_resolution_list[i][1])) {
			return DVSERVERUMD_FAILURE;
		}
	}
	return DVSERVERUMD_SUCCESS;
}

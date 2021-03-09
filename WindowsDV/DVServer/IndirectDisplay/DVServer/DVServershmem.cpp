/*===========================================================================
; DVServershmem.cpp
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   DVServer host and VM shared memory interface implementations
;--------------------------------------------------------------------------*/
#include <windows.h>
#include "Driver.h"
#include "DVServererror.h"
#include "DVServercommon.h"
#include "DVServershmem.h"
#include "DVServeredidparser.h"
#include "SetupAPI.h"

extern "C" {
#include "shared_data.h"
#include "Public.h"
}

static HDEVINFO device_info_set = NULL;
static PSP_DEVICE_INTERFACE_DETAIL_DATA device_detail_data = NULL;
static void update_registry(edid_data *data);

/*---------------------------------------------------------------------------
; Internal API to initialize the shared memory interface
; The valid returned handle is for the caller to issue DeviceIoControl()
; The caller needs to destroy this interface when finishing shared memory
;   accesses
;--------------------------------------------------------------------------*/
HANDLE idd_shmem_init(void)
{
	//DWORD    device_index = 0;
	HANDLE   device_handle = INVALID_HANDLE_VALUE;

	device_info_set = SetupDiGetClassDevs(
							NULL,
							NULL,
							NULL,
							DIGCF_PRESENT |
							DIGCF_ALLCLASSES |
							DIGCF_DEVICEINTERFACE);
	if (!device_info_set) {
		WriteToLog("SetupDiGetClassDevs(): Failed");
		return INVALID_HANDLE_VALUE;
	}

	SP_DEVICE_INTERFACE_DATA dev_inf_data;
	SecureZeroMemory(&dev_inf_data, sizeof(SP_DEVICE_INTERFACE_DATA));
	dev_inf_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	if (SetupDiEnumDeviceInterfaces(
							device_info_set,
							NULL,
							&GUID_DEVINTERFACE_IVSHMEM,
							0,
							&dev_inf_data) == FALSE) {
		DWORD error = GetLastError();
		if (error == ERROR_NO_MORE_ITEMS) {
			WriteToLog("Unable to enumerate the device, is it attached?");
		}
		WriteToLog("SetupDiEnumDeviceInterfaces failed");
		idd_shmem_destroy(device_handle);
		return INVALID_HANDLE_VALUE;
	}

	DWORD req_size = 0;
	SetupDiGetDeviceInterfaceDetail(device_info_set,
							&dev_inf_data,
							NULL,
							0,
							&req_size,
							NULL);
	if (!req_size) {
		WriteToLog("SetupDiGetDeviceInterfaceDetail");
		idd_shmem_destroy(device_handle);
		return INVALID_HANDLE_VALUE;
	}

	device_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(req_size);
	if (!device_detail_data) {
		WriteToLog("malloc() for device_detail_data failed");
		idd_shmem_destroy(device_handle);
		return INVALID_HANDLE_VALUE;
	}

	device_detail_data->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA);
	if (!SetupDiGetDeviceInterfaceDetail(
							device_info_set,
							&dev_inf_data,
							device_detail_data,
							req_size,
							NULL,
							NULL)) {
		WriteToLog("SetupDiGetDeviceInterfaceDetail");
		idd_shmem_destroy(device_handle);
		return INVALID_HANDLE_VALUE;
	}

	device_handle = CreateFile(
							device_detail_data->DevicePath,
							0,
							0,
							NULL,
							OPEN_EXISTING,
							0,
							0);
	if (device_handle == INVALID_HANDLE_VALUE) {
		WriteToLog("CreateFile returned INVALID_HANDLE_VALUE");
		idd_shmem_destroy(device_handle);
		return INVALID_HANDLE_VALUE;
	}

	return device_handle;
}

/*---------------------------------------------------------------------------
; Internal API to destroy the shared memory interface
; The caller needs to call this function to clean up shared memory interface
;   either during initialization or finishing shared memory accesses
;--------------------------------------------------------------------------*/
void idd_shmem_destroy(HANDLE handle)
{
	if (handle && handle != INVALID_HANDLE_VALUE) {
		CloseHandle(handle);
	}

	if (device_detail_data) {
		free(device_detail_data);
		device_detail_data = NULL;
	}

	if (device_info_set) {
		SetupDiDestroyDeviceInfoList(device_info_set);
		device_info_set = NULL;
	}
}

/*---------------------------------------------------------------------------
; Internal API to get config info from the host shared memory
;--------------------------------------------------------------------------*/
int idd_get_config_info(void)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	ULONG ret_length;
	edid_data data = { 0 };

	handle = idd_shmem_init();
	if (handle == INVALID_HANDLE_VALUE) {
		return IDD_FAILURE;
	}

	WriteToLog("IOCTL_IVSHMEM_REQUEST_SIZE");
	IVSHMEM_SIZE size = 0;
	if (!DeviceIoControl(
						handle,
						IOCTL_IVSHMEM_REQUEST_SIZE,
						NULL,
						0,
						&size,
						sizeof(IVSHMEM_SIZE),
						&ret_length,
						NULL) ||
		size == 0) {
		WriteToLog("DeviceIoControl: IOCTL_IVSHMEM_REQUEST_SIZE failed");
		idd_shmem_destroy(handle);
		return IDD_FAILURE;
	}

	IVSHMEM_MMAP_CONFIG config;
	config.cacheMode = IVSHMEM_CACHE_NONCACHED;
	IVSHMEM_MMAP map;
	SecureZeroMemory(&map, sizeof(IVSHMEM_MMAP));
	if (!DeviceIoControl(
						handle,
						IOCTL_IVSHMEM_REQUEST_MMAP,
						&config,
						sizeof(IVSHMEM_MMAP_CONFIG),
						&map,
						sizeof(IVSHMEM_MMAP),
						&ret_length,
						NULL) ||
		!map.ptr || map.size != size) {
		WriteToLog("DeviceIoControl: IOCTL_IVSHMEM_REQUEST_MMAP faile");
		idd_shmem_destroy(handle);
		return IDD_FAILURE;
	}

	memcpy_s(&data, sizeof(edid_data), map.ptr, sizeof(edid_data));

	update_registry(&data);

	if (!DeviceIoControl(
						handle,
						IOCTL_IVSHMEM_RELEASE_MMAP,
						NULL,
						0,
						NULL,
						0,
						&ret_length,
						NULL)) {
		WriteToLog("DeviceIoControl: IOCTL_IVSHMEM_RELEASE_MMAP faile");
		idd_shmem_destroy(handle);
		return IDD_FAILURE;
	}

	idd_shmem_destroy(handle);

	return IDD_SUCCESS;
}

/*---------------------------------------------------------------------------
; Static help function to update IDD registry with shared data
;--------------------------------------------------------------------------*/
static void update_registry(edid_data *data)
{
	WCHAR reg_name[MAX_REG_NAME_LENGTH];

	if (!data || !data->size || data->size > MAX_DATA_SIZE) {
		return;
	}

	mbstowcs_s(NULL, reg_name, MAX_REG_NAME_LENGTH, EDID_REG_NAME, _TRUNCATE);
	idd_write_registry_binary(reg_name, (BYTE*)data->data, data->size);
}

/*===========================================================================
; DVServerregistry.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file has utility API which reads & writes windows registry key
;--------------------------------------------------------------------------*/

#include "DVServercommon.h"
#include "DVServerregistry.tmh"

extern WDFDEVICE g_dvserver_device;

/*---------------------------------------------------------------------------
*
*Description
*
* open_dvserver_registry - This function helps to open the ServiceName subkey of the
* driver's software key for read/write access
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, Non-Zero = ERROR
*
*--------------------------------------------------------------------------*/
static NTSTATUS open_dvserver_registry(WDFKEY* key)
{
	TRACING();
	NTSTATUS status = STATUS_SUCCESS;

	if (!key) {
		return STATUS_UNSUCCESSFUL;
	}

	status = WdfDeviceOpenRegistryKey(
		g_dvserver_device,
		PLUGPLAY_REGKEY_DRIVER | WDF_REGKEY_DRIVER_SUBKEY,
		KEY_READ | KEY_SET_VALUE,
		WDF_NO_OBJECT_ATTRIBUTES,
		key);
	DBGPRINT("WdfDeviceOpenRegistryKey status: %x", status);
	return status;
}


/*---------------------------------------------------------------------------
*
*Description
*
* close_dvserver_registry - This function helps to close driver's registry key
*
* Parameters
* Null
*
* Return val
* void
*
*--------------------------------------------------------------------------*/
static void close_dvserver_registry(WDFKEY key)
{
	TRACING();
	WdfRegistryClose(key);
}


/*---------------------------------------------------------------------------
*
*Description
*
* write_dvserver_registry_binary - This function helps to write binary data
* from a buffer into registry
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
*--------------------------------------------------------------------------*/
int write_dvserver_registry_binary(PCWSTR name, BYTE* buffer, ULONG size)
{
	TRACING();
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_type = REG_BINARY;

	if (!name || !buffer || !size) {
		return DVSERVERUMD_FAILURE;
	}

	status = open_dvserver_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryAssignValue(
			key_idd,
			&uc_name,
			value_type,
			size,
			buffer);
		DBGPRINT("WdfRegistryAssignValue return status: %x", status);
		close_dvserver_registry(key_idd);
		if (NT_SUCCESS(status)) {
			return DVSERVERUMD_SUCCESS;
		}
	}
	return DVSERVERUMD_FAILURE;
}

/*---------------------------------------------------------------------------
*
*Description
*
* read_dvserver_registry_binary - This function helps to to read binary data
* from registry into a buffer
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
*--------------------------------------------------------------------------*/
int read_dvserver_registry_binary(PCWSTR name, BYTE* buffer, ULONG* size)
{
	TRACING();
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_len, value_type = REG_BINARY;

	if (!name || !buffer || !size) {
		return DVSERVERUMD_FAILURE;
	}

	status = open_dvserver_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryQueryValue(
			key_idd,
			&uc_name,
			*size,
			buffer,
			&value_len,
			&value_type);
		DBGPRINT("WdfRegistryQueryValue return status: %x, read length: %d", status, value_len);
		close_dvserver_registry(key_idd);
		if (NT_SUCCESS(status)) {
			*size = value_len;
			return DVSERVERUMD_SUCCESS;
		}
	}
	return DVSERVERUMD_FAILURE;
}
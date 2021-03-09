/*===========================================================================
; DVServercommon.cpp
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   This file defines the DVServer common helper functions
;--------------------------------------------------------------------------*/
#include "Driver.h"
#include "DVServercommon.h"
#include "DVServererror.h"

extern WDFDEVICE g_idd_device;
static char log_buff[BUFSIZ] = { 0 };

/*---------------------------------------------------------------------------
; Helper function to open the IDD registry key
;--------------------------------------------------------------------------*/
static NTSTATUS open_idd_registry(WDFKEY *key)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (!key) {
		return STATUS_UNSUCCESSFUL;
	}

	status = WdfDeviceOpenRegistryKey(
					g_idd_device,
					PLUGPLAY_REGKEY_DRIVER | WDF_REGKEY_DRIVER_SUBKEY,
					KEY_READ | KEY_SET_VALUE,
					WDF_NO_OBJECT_ATTRIBUTES,
					key);
	sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
	WriteToLog(log_buff);
	return status;
}

/*---------------------------------------------------------------------------
; Helper function to close the IDD registry key
;--------------------------------------------------------------------------*/
static void close_idd_registry(WDFKEY key)
{
	WdfRegistryClose(key);
}

/*---------------------------------------------------------------------------
; Internal API to read a UINT32 value from the registry
;--------------------------------------------------------------------------*/
int idd_read_registry_dword(PCWSTR name, ULONG *value)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;

	if (!name || !value) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryQueryULong(
						key_idd,
						&uc_name,
						(PULONG)value);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		return IDD_SUCCESS;
	}

	return IDD_FAILURE;
}

/*---------------------------------------------------------------------------
; Internal API to write a UNIT32 value to the registry
; If the registry value not present, create it and assign the value
;--------------------------------------------------------------------------*/
int idd_write_registry_dword(PCWSTR name, ULONG value)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;

	if (!name) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryAssignULong(
						key_idd,
						&uc_name,
						value);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		return IDD_SUCCESS;
	}

	return IDD_FAILURE;
}

/*---------------------------------------------------------------------------
; Internal API to read binary data from registry into a buffer
; [in] *size  : read buffer size
; [out] *size : byte count read from the registry
;--------------------------------------------------------------------------*/
int idd_read_registry_binary(PCWSTR name, BYTE *buffer, ULONG *size)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_len, value_type = REG_BINARY;

	if (!name || !buffer || !size) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryQueryValue(
						key_idd,
						&uc_name,
						*size,
						buffer,
						&value_len,
						&value_type);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x, read length: %d",
						__FUNCTION__, status, value_len);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		if (NT_SUCCESS(status)) {
			*size = value_len;
			return IDD_SUCCESS;
		}
	}

	return IDD_FAILURE;
}

/*---------------------------------------------------------------------------
; Internal API to write binary data from a buffer into registry
;--------------------------------------------------------------------------*/
int idd_write_registry_binary(PCWSTR name, BYTE *buffer, ULONG size)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_type = REG_BINARY;

	if (!name || !buffer || !size) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryAssignValue(
						key_idd,
						&uc_name,
						value_type,
						size,
						buffer);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		if (NT_SUCCESS(status)) {
			return IDD_SUCCESS;
		}
	}

	return IDD_FAILURE;
}

/*---------------------------------------------------------------------------
; Internal API to read WCHAR string from registry into a buffer
;--------------------------------------------------------------------------*/
int idd_read_registry_string(PCWSTR name, WCHAR *buffer, ULONG size)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_len, value_type = REG_SZ;

	if (!name || !buffer || !size) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryQueryValue(
						key_idd,
						&uc_name,
						size,
						buffer,
						&value_len,
						&value_type);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		if (NT_SUCCESS(status) && size >= value_len) {
			return IDD_SUCCESS;
		}
	}

	return IDD_FAILURE;
}

/*---------------------------------------------------------------------------
; Internal API to write a WCHAR string buffer into the registry
;--------------------------------------------------------------------------*/
int idd_write_registry_string(PCWSTR name, WCHAR *buffer, ULONG size)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFKEY key_idd;
	UNICODE_STRING uc_name;
	ULONG value_type = REG_SZ;

	if (!name || !buffer || !size) {
		return IDD_FAILURE;
	}

	status = open_idd_registry(&key_idd);

	if (NT_SUCCESS(status)) {
		RtlInitUnicodeString(&uc_name, name);
		status = WdfRegistryAssignValue(
						key_idd,
						&uc_name,
						value_type,
						size,
						buffer);
		sprintf_s(log_buff, BUFSIZ, "%s, status: %x", __FUNCTION__, status);
		WriteToLog(log_buff);
		close_idd_registry(key_idd);
		if (NT_SUCCESS(status)) {
			return IDD_SUCCESS;
		}
	}

	return IDD_FAILURE;
}


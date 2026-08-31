/*++
*
* Copyright (C) 2021 Intel Corporation
* SPDX-License-Identifier: BSD-3-Clause

Module Name:

	IoctlValidation.cpp

Abstract:

	This file implements utility functions to validate the IOCTLs.

Environment:

	Kernel-mode Driver Framework

--*/
#include "IoctlValidation.h"
#include "Trace.h"
#include <ntifs.h>
#include "Public.h"
#include "edid.h"
#include <IoctlValidation.tmh>
#include "helper.h"

/*******************************************************************************
 *
 * Description
 *
 * BuffersOverlap - This function checks whether the input and output buffers
 * overlap in memory, considering their starting addresses and lengths, and
 * handles pointer overflow cases.
 *
 * Parameters
 * inputBuffer - Pointer to the start of the input buffer
 * inputBufferLength - Length of the input buffer in bytes
 * outputBuffer - Pointer to the start of the output buffer
 * outputBufferLength - Length of the output buffer in bytes
 *
 * Return val
 * bool - true if buffers overlap, false otherwise
 *
 ******************************************************************************/
bool BuffersOverlap(const void *inputBuffer, size_t inputBufferLength, const void *outputBuffer,
					size_t outputBufferLength)
{
	if (!inputBuffer || !outputBuffer || inputBufferLength == 0 || outputBufferLength == 0) {
		ERR("Buffer or buffer length is NULL\n");
		return false;
	}

	const BYTE *inputStart = (const BYTE *)inputBuffer;
	const BYTE *outputStart = (const BYTE *)outputBuffer;

	// Check for pointer overflow
	if ((SIZE_MAX - (size_t)inputStart) < inputBufferLength || (SIZE_MAX - (size_t)outputStart) < outputBufferLength) {
		ERR("buffer length is too large and causes pointer overflow\n");
		return false;
	}

	const BYTE *inputEnd = inputStart + inputBufferLength;
	const BYTE *outputEnd = outputStart + outputBufferLength;
	bool overlap = (inputStart < outputEnd) && (outputStart < inputEnd);
	if (overlap) {
		ERR("BuffersOverlap: input and output buffers overlap)\n");
	}
	return overlap;
}

/*******************************************************************************
 *
 * Description
 *
 * ValidateIoctl - This function validates IOCTL input data for various request
 * types, such as frame, cursor, EDID, and hotplug event, ensuring all required
 * fields are within valid ranges and reporting errors through WDF request completion.
 *
 * Parameters
 * data - Pointer to the input buffer containing request-specific data
 * Request - WDFREQUEST handle for the current IOCTL request
 * type - IOCTL_VALIDATE_TYPE enum specifying the validation type
 *
 * Return val
 * NTSTATUS - STATUS_SUCCESS if validation passes, appropriate error status otherwise
 *
 ******************************************************************************/
NTSTATUS ValidateIoctl(const void *data, const WDFREQUEST Request, IOCTL_VALIDATE_TYPE type)
{
	if (data == NULL) {
		ERR("Input buffer is NULL\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_UNSUCCESSFUL;
	}
	switch (type) {
	case VALIDATE_FRAME: {
		const FrameMetaData *ptr = static_cast<const FrameMetaData *>(data);
		if (ptr->screen_num >= MAX_SCAN_OUT) {
			ERR("Screen number provided by UMD: %d is greater than or equal to the maximum supported: %d by the KMD\n",
				ptr->screen_num, MAX_SCAN_OUT);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if (ptr->width == 0 || ptr->height == 0) {
			ERR("Invalid frame dimensions: width=%u, height=%u\n", ptr->width, ptr->height);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if ((ptr->width > MAX_WIDTH_SIZE) || (ptr->height > MAX_HEIGHT_SIZE)) {
			ERR("Invalid frame dimensions: width=%d, height=%d. Max allowed size is %dx%d.\n", ptr->width, ptr->height,
				MAX_WIDTH_SIZE, MAX_HEIGHT_SIZE);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if (ptr->stride == 0) {
			ERR("Invalid frame dimensions. Stride is NULL\n");
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		const SIZE_T maxFrameSize = (SIZE_T)MAX_WIDTH_SIZE * MAX_HEIGHT_SIZE * 4; // 4 bytes per pixel
		if ((SIZE_T)ptr->stride > maxFrameSize / (SIZE_T)ptr->height) {
			ERR("Stride too large: stride=%u, height=%u\n", ptr->stride, ptr->height);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case VALIDATE_CURSOR: {
		const CursorData *cptr = static_cast<const CursorData *>(data);
		if (cptr->screen_num >= MAX_SCAN_OUT) {
			ERR("Screen number provided by UMD: %d is greater than or equal to the maximum supported: %d by the KMD\n",
				cptr->screen_num, MAX_SCAN_OUT);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if ((cptr->width > POINTER_SIZE) || (cptr->height > POINTER_SIZE)) {
			ERR("Invalid cursor dimensions: width=%d, height=%d. Max allowed is %d.\n", cptr->width, cptr->height,
				POINTER_SIZE);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case VALIDATE_CURSOR_POSITION: {
		const CursorData *cptr = static_cast<const CursorData *>(data);
		if (cptr->screen_num >= MAX_SCAN_OUT) {
			ERR("Screen number provided by UMD: %d is greater than or equal to the maximum supported: %d by the KMD\n",
				cptr->screen_num, MAX_SCAN_OUT);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if (cptr->cursor_x < 0 || cptr->cursor_y < 0) {
			ERR("Invalid cursor position: x=%d, y=%d\n", cptr->cursor_x, cptr->cursor_y);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case VALIDATE_EDID: {
		const edid_info *edata = static_cast<const edid_info *>(data);
		if (edata->screen_num >= MAX_SCAN_OUT) {
			ERR("Screen number provided by UMD: %d is greater than or equal to the maximum supported: %d by the KMD\n",
				edata->screen_num, MAX_SCAN_OUT);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case VALIDATE_TOTAL_SCREENS: {
		const screen_info *mdata = static_cast<const screen_info *>(data);
		if (mdata == NULL) {
			ERR("Output buffer is NULL\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return STATUS_UNSUCCESSFUL;
		}
		if (mdata->total_screens > MAX_SCAN_OUT) {
			ERR("Invalid total screens value: %d. Max supported is %d\n", mdata->total_screens, MAX_SCAN_OUT);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	case VALIDATE_HP_EVENT: {
		const hp_info *info = static_cast<const hp_info *>(data);
		if (info == NULL) {
			ERR("Input buffer is NULL\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return STATUS_UNSUCCESSFUL;
		}
		if (info->event == NULL) {
			ERR("Invalid event value\n");
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		break;
	}
	default:
		ERR("Unknown validation type\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}
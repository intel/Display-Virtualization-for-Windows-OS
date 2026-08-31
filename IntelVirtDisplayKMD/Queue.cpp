/*++
*
* Copyright (C) 2021 Intel Corporation
* SPDX-License-Identifier: BSD-3-Clause

Module Name:

	queue.c

Abstract:

	This file contains the queue entry points and callbacks.

Environment:

	Kernel-mode Driver Framework

--*/
#include <ntifs.h>
#include "driver.h"
#include "baseobj.h"
#include "viogpulite.h"
#include "Public.h"
#include "edid.h"
#include "Trace.h"
#include "IoctlValidation.h"
#include <Queue.tmh>
#include "Trace_override.h"
extern "C" {
#include "..\EDIDParser\edidshared.h"
}

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, IntelVirtDisplayKMDQueueInitialize)
#endif

NTSTATUS
IntelVirtDisplayKMDQueueInitialize(_In_ WDFDEVICE Device)
/*++

Routine Description:

	 The I/O dispatch callbacks for the frameworks device object
	 are configured in this function.

	 A single default I/O Queue is configured for parallel request
	 processing, and a driver context memory allocation is created
	 to hold our structure QUEUE_CONTEXT.

Arguments:

	Device - Handle to a framework device object.

Return Value:

	VOID

--*/
{
	WDFQUEUE queue;
	NTSTATUS status;
	WDF_IO_QUEUE_CONFIG queueConfig;

	PAGED_CODE();
	TRACING();

	//
	// Configure a default queue so that requests that are not
	// configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
	// other queues get dispatched here.
	//
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoDeviceControl = IntelVirtDisplayKMDEvtIoDeviceControl;
	queueConfig.EvtIoStop = IntelVirtDisplayKMDEvtIoStop;

	status = WdfIoQueueCreate(Device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);

	if (!NT_SUCCESS(status)) {
		ERR("Couldn't create IO queue\n");
		return status;
	}

	return status;
}

VOID IntelVirtDisplayKMDEvtIoDeviceControl(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t OutputBufferLength,
										   _In_ size_t InputBufferLength, _In_ ULONG IoControlCode)
/*++

Routine Description:

	This event is invoked when the framework receives IRP_MJ_DEVICE_CONTROL request.

Arguments:

	Queue -  Handle to the framework queue object that is associated with the
			 I/O request.

	Request - Handle to a framework request object.

	OutputBufferLength - Size of the output buffer in bytes

	InputBufferLength - Size of the input buffer in bytes

	IoControlCode - I/O control code.

Return Value:

	VOID

--*/
{
	NTSTATUS status = STATUS_SUCCESS;
	size_t bufSize;
	struct KMDF_IOCTL_Response *resp = NULL;
	WDFDEVICE Device = WdfIoQueueGetDevice(Queue);
	PDEVICE_CONTEXT pDeviceContext = DeviceGetContext(Device);
	size_t bytesReturned = 0;
	TRACING();

	UNREFERENCED_PARAMETER(Queue);

	if (!OutputBufferLength || !InputBufferLength) {
		ERR("Invalid input or output buffer length\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return;
	}

	switch (IoControlCode) {
	case IOCTL_INTELVIRTDISPLAY_FRAME_DATA:

		// Get the input buffer from the UMD which is passed to "IoctlRequestPresentFb" API and We use
		// "WdfRequestRetrieveInputBuffer" method retrieves an I/O request's input buffer.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfrequest/nf-wdfrequest-wdfrequestretrieveinputbuffer
		status = IoctlRequestPresentFb(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS) {
			return;
		}
		break;

	case IOCTL_INTELVIRTDISPLAY_CURSOR_DATA:

		status = IoctlSetPointerShape(pDeviceContext, InputBufferLength, OutputBufferLength, Request);
		if (status != STATUS_SUCCESS)
			return;
		break;

	case IOCTL_INTELVIRTDISPLAY_CURSOR_POS:
		status = IoctlSetPointerPosition(pDeviceContext, InputBufferLength, Request);
		if (status != STATUS_SUCCESS)
			return;

		if (OutputBufferLength < sizeof(struct KMDF_IOCTL_Response)) {
			ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
				sizeof(struct KMDF_IOCTL_Response));
			WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
			return;
		}

		status = WdfRequestRetrieveOutputBuffer(Request, sizeof(struct KMDF_IOCTL_Response), (PVOID *)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		// Return value from the KMDF IntelVirtDisplay
		resp->retval = INTELVIRTDISPLAYKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;

	case IOCTL_INTELVIRTDISPLAY_GET_EDID_DATA:
		status = IoctlRequestEdid(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		break;

	case IOCTL_INTELVIRTDISPLAY_SET_MODE:
		status = IoctlRequestSetMode(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;

		if (OutputBufferLength < sizeof(struct KMDF_IOCTL_Response)) {
			ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
				sizeof(struct KMDF_IOCTL_Response));
			WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
			return;
		}

		status = WdfRequestRetrieveOutputBuffer(Request, sizeof(struct KMDF_IOCTL_Response), (PVOID *)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		// Return value from the KMDF IntelVirtDisplay
		resp->retval = INTELVIRTDISPLAYKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;

	case IOCTL_INTELVIRTDISPLAY_TEST_IMAGE:
		status = IoctlRequestPresentFb(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		break;
	case IOCTL_INTELVIRTDISPLAY_GET_TOTAL_SCREENS:
		status =
			IoctlRequestTotalScreens(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		break;
	case IOCTL_INTELVIRTDISPLAY_HP_EVENT:
		status =
			IoctlRequestHPEventInfo(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		break;
	}

	WdfRequestComplete(Request, STATUS_SUCCESS);
	return;
}

VOID IntelVirtDisplayKMDEvtIoStop(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ ULONG ActionFlags)
/*++

Routine Description:

	This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

	Queue -  Handle to the framework queue object that is associated with the
			 I/O request.

	Request - Handle to a framework request object.

	ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
				  that identify the reason that the callback function is being called
				  and whether the request is cancelable.

Return Value:

	VOID

--*/
{
	UNREFERENCED_PARAMETER(Queue);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(ActionFlags);

	//
	// In most cases, the EvtIoStop callback function completes, cancels, or postpones
	// further processing of the I/O request.
	//
	// Typically, the driver uses the following rules:
	//
	// - If the driver owns the I/O request, it calls WdfRequestUnmarkCancelable
	//   (if the request is cancelable) and either calls WdfRequestStopAcknowledge
	//   with a Requeue value of TRUE, or it calls WdfRequestComplete with a
	//   completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
	//
	//   Before it can call these methods safely, the driver must make sure that
	//   its implementation of EvtIoStop has exclusive access to the request.
	//
	//   In order to do that, the driver must synchronize access to the request
	//   to prevent other threads from manipulating the request concurrently.
	//   The synchronization method you choose will depend on your driver's design.
	//
	//   For example, if the request is held in a shared context, the EvtIoStop callback
	//   might acquire an internal driver lock, take the request from the shared context,
	//   and then release the lock. At this point, the EvtIoStop callback owns the request
	//   and can safely complete or requeue the request.
	//
	// - If the driver has forwarded the I/O request to an I/O target, it either calls
	//   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
	//   further processing of the request and calls WdfRequestStopAcknowledge with
	//   a Requeue value of FALSE.
	//
	// A driver might choose to take no action in EvtIoStop for requests that are
	// guaranteed to complete in a small amount of time.
	//
	// In this case, the framework waits until the specified request is complete
	// before moving the device (or system) to a lower power state or removing the device.
	// Potentially, this inaction can prevent a system from entering its hibernation state
	// or another low system power state. In extreme cases, it can cause the system
	// to crash with bugcheck code 9F.
	//

	return;
}

static NTSTATUS IoctlRequestSetMode(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
									const size_t OutputBufferLength, const WDFREQUEST Request, size_t *BytesReturned)
{
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct FrameMetaData *ptr = NULL;

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Coudlnt' find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (InputBufferLength < sizeof(struct FrameMetaData)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength,
			sizeof(struct FrameMetaData));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID *)&ptr, NULL);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INVALID_USER_BUFFER);
		return STATUS_INVALID_USER_BUFFER;
	}

	status = ValidateIoctl(ptr, Request, VALIDATE_FRAME);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	CURRENT_MODE tempCurrentMode = {0};
	tempCurrentMode.DispInfo.Width = ptr->width;
	tempCurrentMode.DispInfo.Height = ptr->height;
	tempCurrentMode.DispInfo.Pitch = ptr->pitch;
	tempCurrentMode.DispInfo.TargetId = ptr->screen_num;
	tempCurrentMode.DispInfo.ColorFormat = (D3DDDIFORMAT)ptr->format;
	tempCurrentMode.FrameBuffer.Ptr = (BYTE *)ptr->addr;
	tempCurrentMode.Stride = ptr->stride;

	status = pAdapter->SetCurrentModeExt(&tempCurrentMode);
	if (status != STATUS_SUCCESS) {
		ERR("SetCurrentModeExt failed with status = %d\n", status);
		WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
		return STATUS_UNSUCCESSFUL;
	}

	// BlackOutScreen
	CURRENT_MODE CurrentMode = {0};
	CurrentMode.DispInfo.Width = ptr->width;
	CurrentMode.DispInfo.Height = ptr->height;
	CurrentMode.DispInfo.Pitch = ptr->pitch;
	CurrentMode.DispInfo.TargetId = ptr->screen_num;
	CurrentMode.FrameBuffer.Ptr = pAdapter->GetFbVAddr(ptr->screen_num);
	CurrentMode.Flags.FrameBufferIsActive = 1;

	pAdapter->BlackOutScreen(&CurrentMode);

	if (tempCurrentMode.FrameBuffer.Ptr) {
		pAdapter->Close(ptr->screen_num);
	}

	// After ValidateIoctl and before using ptr->addr:
	if (ptr->addr != NULL) {
		__try {
			SIZE_T frameSize = 0;
			if (ptr->stride != 0 && ptr->height <= (MAXSIZE_T / (SIZE_T)ptr->stride)) {
				frameSize = (SIZE_T)ptr->height * (SIZE_T)ptr->stride;
			} else {
				ERR("Invalid frame metadata: height=%u, stride=%u\n", ptr->height, ptr->stride);
				WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
				return STATUS_INVALID_PARAMETER;
			}
			ProbeForRead(ptr->addr, frameSize, sizeof(BYTE));
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ERR("Invalid user-mode addr in SET_MODE\n");
			NTSTATUS excStatus = GetExceptionCode();
			WdfRequestComplete(Request, excStatus);
			return excStatus;
		}
	}

	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestPresentFb(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
									  const size_t OutputBufferLength, const WDFREQUEST Request, size_t *BytesReturned)
{
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	FrameMetaData *ptr = NULL;

	PIRP irp = WdfRequestWdmGetIrp(Request);
	if (!irp) {
		ERR("Couldn't retrieve IRP\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
	if (!irpSp) {
		ERR("Couldn't retrieve IRP stack\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	PVOID inputBuffer = irpSp->Parameters.DeviceIoControl.Type3InputBuffer;
	PVOID outBuffer = irp->UserBuffer;

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Couldnt' find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (InputBufferLength < sizeof(struct FrameMetaData)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength,
			sizeof(struct FrameMetaData));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (inputBuffer == NULL) {
		ERR("Input buffer is NULL\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
		ERR("Cannot access user-mode buffer at IRQL > PASSIVE_LEVEL\n");
		WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	FrameMetaData localPtr = {0};
	__try {
		ProbeForRead(inputBuffer, sizeof(FrameMetaData), __alignof(FrameMetaData));
		// Capture user data into a kernel-local copy to prevent
		// double-fetch / TOCTOU. All subsequent validation and use must
		// reference 'localPtr' only, never the user-mode 'inputBuffer'.
		localPtr = *(FrameMetaData *)inputBuffer;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ERR("Invalid user-mode buffer access (input)\n");
		status = GetExceptionCode();
		WdfRequestComplete(Request, status);
		return status;
	}
	ptr = &localPtr;

	// Validate the captured copy first so stride==0 etc. cannot reach the
	// arithmetic below as a divide-by-zero.
	status = ValidateIoctl(ptr, Request, VALIDATE_FRAME);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	__try {
		SIZE_T size = 0;
		if (ptr->stride != 0 && ptr->height <= (MAXSIZE_T / (SIZE_T)ptr->stride)) {
			size = (SIZE_T)ptr->height * (SIZE_T)ptr->stride;
		} else {
			ERR("Invalid frame metadata: height=%u, width=%u, stride=%u\n", ptr->height, ptr->width, ptr->stride);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		if (BuffersOverlap(ptr->addr, size, outBuffer, OutputBufferLength)) {
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		ProbeForRead(ptr->addr, size, sizeof(BYTE));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		status = GetExceptionCode();
		ERR("Invalid user-mode buffer access (addr). Exception code: 0x%X\n", status);
		WdfRequestComplete(Request, status);
		return status;
	}

	status = pAdapter->ExecutePresentDisplayZeroCopy((BYTE *)ptr->addr, ptr->bitrate, ptr->pitch, ptr->width,
													 ptr->height, ptr->screen_num, ptr->stride);

	if (status != STATUS_SUCCESS) {
		ERR("ExecutePresentDisplayZeroCopy failed with status = %d\n", status);
		WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
		return STATUS_UNSUCCESSFUL;
	}

	if (OutputBufferLength < sizeof(struct KMDF_IOCTL_Response)) {
		ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
			sizeof(struct KMDF_IOCTL_Response));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (outBuffer == NULL) {
		ERR("Output buffer is NULL\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	__try {
		ProbeForWrite(outBuffer, sizeof(KMDF_IOCTL_Response), __alignof(KMDF_IOCTL_Response));
		((KMDF_IOCTL_Response *)outBuffer)->retval = INTELVIRTDISPLAYKMD_SUCCESS;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		status = GetExceptionCode();
		ERR("Exception while writing to output buffer: 0x%X\n", status);
		WdfRequestComplete(Request, status);
		return status;
	}

	if (BuffersOverlap(inputBuffer, InputBufferLength, outBuffer, OutputBufferLength)) {
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestEdid(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
								 const size_t OutputBufferLength, const WDFREQUEST Request, size_t *BytesReturned)
{
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	VioGpuAdapterLite *pAdapter;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct edid_info *edata = NULL;
	size_t bufSize;

	if (DeviceContext == NULL) {
		ERR("Invalid Device Context\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	} else {
		pAdapter = (VioGpuAdapterLite *)DeviceContext->pvDeviceExtension;
		if (!pAdapter) {
			ERR("Coudlnt' find adapter\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	if (InputBufferLength < sizeof(struct edid_info)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength,
			sizeof(struct edid_info));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(struct edid_info), (PVOID *)&edata, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INVALID_USER_BUFFER);
		return STATUS_INVALID_USER_BUFFER;
	}

	status = ValidateIoctl(edata, Request, VALIDATE_EDID);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	if (OutputBufferLength < sizeof(struct edid_info)) {
		ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
			sizeof(struct edid_info));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	// Capture screen_num from the validated input before re-using edata for output:
	UINT32 validatedScreenNum = edata->screen_num; // captured from input buffer

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(struct edid_info), (PVOID *)&edata, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Output buffer\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	UINT32 reportedModeSize = pAdapter->GetModeListSize(validatedScreenNum);
	if (reportedModeSize == 0) {
		reportedModeSize = QEMU_MODELIST_SIZE;
	}

	// Clamp to the size of edata->mode_list[MODE_LIST_MAX_SIZE] to prevent
	// out-of-bounds writes inside CopyResolution.
	if (reportedModeSize > MODE_LIST_MAX_SIZE) {
		reportedModeSize = MODE_LIST_MAX_SIZE;
	}
	edata->mode_size = reportedModeSize;
	edata->screen_num = validatedScreenNum;
	RtlCopyMemory(edata->edid_data, pAdapter->GetEdidData(validatedScreenNum), EDID_V1_BLOCK_SIZE);
	pAdapter->CopyResolution(validatedScreenNum, edata);
	WdfRequestSetInformation(Request, sizeof(struct edid_info));
	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestTotalScreens(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
										 const size_t OutputBufferLength, const WDFREQUEST Request,
										 size_t *BytesReturned)
{
	TRACING();
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct screen_info *mdata = NULL;
	size_t bufSize;

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Couldn't find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (OutputBufferLength < sizeof(struct screen_info)) {
		ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
			sizeof(struct screen_info));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(struct screen_info), (PVOID *)&mdata, &bufSize);
	if (!NT_SUCCESS(status)) {
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = ValidateIoctl(mdata, Request, VALIDATE_TOTAL_SCREENS);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	mdata->total_screens = pAdapter->GetNumScreens();
	WdfRequestSetInformation(Request, sizeof(struct screen_info));

	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestHPEventInfo(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
										const size_t OutputBufferLength, const WDFREQUEST Request,
										size_t *BytesReturned)
{
	TRACING();
	UNREFERENCED_PARAMETER(DeviceContext);
	UNREFERENCED_PARAMETER(Request);
	UNREFERENCED_PARAMETER(BytesReturned);

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct hp_info *info = NULL;
	size_t bufSize;

	if (InputBufferLength < sizeof(struct hp_info)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength, sizeof(struct hp_info));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveInputBuffer(Request, sizeof(struct hp_info), (PVOID *)&info, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INVALID_USER_BUFFER);
		return STATUS_INVALID_USER_BUFFER;
	}

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Couldn't find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (OutputBufferLength < sizeof(struct hp_info)) {
		ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
			sizeof(struct hp_info));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveOutputBuffer(Request, sizeof(struct hp_info), (PVOID *)&info, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Output buffer\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = ValidateIoctl(info, Request, VALIDATE_HP_EVENT);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	pAdapter->SetEvent(info->event);
	pAdapter->FillPresentStatus(info);
	WdfRequestSetInformation(Request, sizeof(struct hp_info));

	return STATUS_SUCCESS;
}

static NTSTATUS IoctlSetPointerShape(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
									 const size_t OutputBufferLength, const WDFREQUEST Request)
{
	TRACING();
	POINTER_SHAPE pointerShape;
	struct CursorData *cptr = NULL;
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	PIRP irp = WdfRequestWdmGetIrp(Request);
	if (!irp) {
		ERR("Couldn't retrieve IRP\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
	if (!irpSp) {
		ERR("Couldn't retrieve IRP stack\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	PVOID inputBuffer = irpSp->Parameters.DeviceIoControl.Type3InputBuffer;
	PVOID outBuffer = irp->UserBuffer;

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Couldnt' find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (InputBufferLength < sizeof(struct CursorData)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength,
			sizeof(struct CursorData));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (inputBuffer == NULL) {
		ERR("Input buffer is NULL\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
		ERR("Cannot access user-mode buffer at IRQL > PASSIVE_LEVEL\n");
		WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	CursorData localCptr = {0};
	__try {
		ProbeForRead(inputBuffer, sizeof(CursorData), __alignof(CursorData));
		// Capture user data into a kernel-local copy to prevent
		// double-fetch / TOCTOU. All subsequent validation and use must
		// reference 'localCptr' only, never the user-mode 'inputBuffer'.
		localCptr = *(CursorData *)inputBuffer;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ERR("Invalid user-mode buffer access (input)\n");
		status = GetExceptionCode();
		WdfRequestComplete(Request, status);
		return status;
	}
	cptr = &localCptr;

	// Validate the captured copy first.
	status = ValidateIoctl(cptr, Request, VALIDATE_CURSOR);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	__try {
		SIZE_T size = 0;
		const SIZE_T bpp = 4;
		// Guard width*bpp overflow first, then guard height*(width*bpp) overflow
		if (cptr->width == 0 || cptr->height == 0) {
			ERR("Invalid cursor metadata: height=%u, width=%u, screen=%d\n", cptr->height, cptr->width, cptr->screen_num);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}

		SIZE_T widthBytes = (SIZE_T)cptr->width * bpp;
		if (cptr->height > (MAXSIZE_T / widthBytes)) {
			ERR("Cursor size overflows: height=%u, widthBytes=%Iu\n", cptr->height, widthBytes);
			WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
			return STATUS_INVALID_PARAMETER;
		}
		size = (SIZE_T)cptr->height * widthBytes;
		ProbeForRead(cptr->data, size, sizeof(BYTE));
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		ERR("Invalid user-mode buffer access (cursor data)\n");
		status = GetExceptionCode();
		WdfRequestComplete(Request, status);
		return status;
	}

	RtlZeroMemory(&pointerShape, sizeof(POINTER_SHAPE));
	pointerShape.pointer.VidPnSourceId = cptr->screen_num;
	pointerShape.pointer.Height = cptr->height;
	pointerShape.pointer.Width = cptr->width;
	pointerShape.pointer.Pitch = cptr->pitch;
	pointerShape.pointer.pPixels = cptr->data;
	pointerShape.pointer.XHot = cptr->x_hot;
	pointerShape.pointer.YHot = cptr->y_hot;
	pointerShape.X = cptr->cursor_x;
	pointerShape.Y = cptr->cursor_y;

	status = pAdapter->SetPointerShape(&pointerShape, cptr->color_format, cptr->iscursorvisible);
	if (status != STATUS_SUCCESS) {
		ERR("SetPointerShape failed with status = %d\n", status);
		WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
		return STATUS_UNSUCCESSFUL;
	}

	if (OutputBufferLength < sizeof(struct KMDF_IOCTL_Response)) {
		ERR("Output Buffer is too small: provided = %Iu, expected >= %Iu\n", OutputBufferLength,
			sizeof(struct KMDF_IOCTL_Response));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	if (outBuffer == NULL) {
		ERR("Output buffer is NULL\n");
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	__try {
		ProbeForWrite(outBuffer, sizeof(KMDF_IOCTL_Response), __alignof(KMDF_IOCTL_Response));
		((KMDF_IOCTL_Response *)outBuffer)->retval = INTELVIRTDISPLAYKMD_SUCCESS;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		status = GetExceptionCode();
		ERR("Exception while writing to output buffer: 0x%X\n", status);
		WdfRequestComplete(Request, status);
		return status;
	}

	if (BuffersOverlap(inputBuffer, InputBufferLength, outBuffer, OutputBufferLength)) {
		WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
		return STATUS_INVALID_PARAMETER;
	}

	WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
	return STATUS_SUCCESS;
}

static NTSTATUS IoctlSetPointerPosition(const PDEVICE_CONTEXT DeviceContext, const size_t InputBufferLength,
										const WDFREQUEST Request)
{
	DXGKARG_SETPOINTERPOSITION pointerPosition;
	struct CursorData *cptr = NULL;
	size_t bufSize;
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	TRACING();

	VioGpuAdapterLite *pAdapter = (VioGpuAdapterLite *)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Couldnt' find adapter\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (InputBufferLength < sizeof(struct CursorData)) {
		ERR("Input Buffer is too small: provided = %Iu, expected >= %Iu\n", InputBufferLength,
			sizeof(struct CursorData));
		WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
		return STATUS_BUFFER_TOO_SMALL;
	}

	status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID *)&cptr, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INVALID_USER_BUFFER);
		return STATUS_INVALID_USER_BUFFER;
	}

	status = ValidateIoctl(cptr, Request, VALIDATE_CURSOR_POSITION);
	if (status != STATUS_SUCCESS) {
		return status;
	}

	RtlZeroMemory(&pointerPosition, sizeof(DXGKARG_SETPOINTERPOSITION));
	pointerPosition.X = cptr->cursor_x;
	pointerPosition.Y = cptr->cursor_y;
	pointerPosition.VidPnSourceId = cptr->screen_num;

	status = pAdapter->SetPointerPosition(&pointerPosition);

	if (status != STATUS_SUCCESS) {
		ERR("SetPointerPosition failed with status = %d\n", status);
		WdfRequestComplete(Request, STATUS_UNSUCCESSFUL);
		return STATUS_UNSUCCESSFUL;
	}
	return STATUS_SUCCESS;
}

/*++
* 
* Copyright © 2021 Intel Corporation
* SPDX-License-Identifier: MS-PL

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
#include "debug.h"

extern "C" {
#include "..\EDIDParser\edidshared.h"
}

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, DVServerKMDQueueInitialize)
#endif

extern GPU_DISP_MODE_EXT gpu_disp_mode_ext[MAX_MODELIST_SIZE];
extern output_modelist mode_list;

NTSTATUS
DVServerKMDQueueInitialize(
	_In_ WDFDEVICE Device
)
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
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
		&queueConfig,
		WdfIoQueueDispatchParallel
	);

	queueConfig.EvtIoDeviceControl = DVServerKMDEvtIoDeviceControl;
	queueConfig.EvtIoStop = DVServerKMDEvtIoStop;

	status = WdfIoQueueCreate(
		Device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status)) {
		ERR("Couldn't create IO queue\n");
		return status;
	}

	return status;
}

VOID
DVServerKMDEvtIoDeviceControl(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ size_t OutputBufferLength,
	_In_ size_t InputBufferLength,
	_In_ ULONG IoControlCode
)
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
	struct CursorData* cptr = NULL;
	struct KMDF_IOCTL_Response* resp = NULL;
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

	switch (IoControlCode)
	{
	case IOCTL_DVSERVER_FRAME_DATA:

		// Get the input buffer from the UMD which is passed to "IoctlRequestPresentFb" API and We use "WdfRequestRetrieveInputBuffer" 
		// method retrieves an I/O request's input buffer.
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfrequest/nf-wdfrequest-wdfrequestretrieveinputbuffer
		status = IoctlRequestPresentFb(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		//Return value from the KMDF DVServer
		resp->retval = DVSERVERKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;


	case IOCTL_DVSERVER_CURSOR_DATA:
		status = WdfRequestRetrieveInputBuffer(Request, 0, (PVOID*)&cptr, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Input buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		PRINT_CURSOR_DATA(("x=%d, y=%d", cptr->cursor_x, cptr->cursor_y));

		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		//Return value from the KMDF DVServer
		resp->retval = DVSERVERKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;

	case IOCTL_DVSERVER_GET_EDID_DATA:
		status = IoctlRequestEdid(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		break;

	case IOCTL_DVSERVER_SET_MODE:
		status = IoctlRequestSetMode(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		//Return value from the KMDF DVServer
		resp->retval = DVSERVERKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;

	case IOCTL_DVSERVER_GENERAL:
		// << TODO>>
		break;

	case IOCTL_DVSERVER_TEST_IMAGE:
		status = IoctlRequestPresentFb(pDeviceContext, InputBufferLength, OutputBufferLength, Request, &bytesReturned);
		if (status != STATUS_SUCCESS)
			return;
		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&resp, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return;
		}

		//Return value from the KMDF DVServer
		resp->retval = DVSERVERKMD_SUCCESS;
		WdfRequestSetInformation(Request, sizeof(struct KMDF_IOCTL_Response));
		break;
	}

	WdfRequestComplete(Request, STATUS_SUCCESS);
	return;
}

VOID
DVServerKMDEvtIoStop(
	_In_ WDFQUEUE Queue,
	_In_ WDFREQUEST Request,
	_In_ ULONG ActionFlags
)
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

static NTSTATUS IoctlRequestSetMode(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned)
{
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct FrameMetaData* ptr = NULL;

	VioGpuAdapterLite* pAdapter =
		(VioGpuAdapterLite*)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Coudlnt' find adapter\n");
		return status;
	}

	status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID*)&ptr, NULL);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INVALID_USER_BUFFER;
	}

	CURRENT_MODE tempCurrentMode = { 0 };
	tempCurrentMode.DispInfo.Width = ptr->width;
	tempCurrentMode.DispInfo.Height = ptr->height;
	tempCurrentMode.DispInfo.Pitch = ptr->pitch;
	tempCurrentMode.DispInfo.ColorFormat = (D3DDDIFORMAT) ptr->format;
	tempCurrentMode.FrameBuffer.Ptr = (BYTE*)ptr->addr;
	
	status = pAdapter->SetCurrentModeExt(&tempCurrentMode);
	if (status != STATUS_SUCCESS)
		return STATUS_UNSUCCESSFUL;

	// BlackOutScreen
	CURRENT_MODE CurrentMode = { 0 };
	CurrentMode.DispInfo.Width = ptr->width;
	CurrentMode.DispInfo.Height = ptr->height;
	CurrentMode.DispInfo.Pitch = ptr->pitch;
	CurrentMode.FrameBuffer.Ptr = pAdapter->m_FrameSegment.GetFbVAddr();
	CurrentMode.Flags.FrameBufferIsActive = 1;

	pAdapter->BlackOutScreen(&CurrentMode);

	if (tempCurrentMode.FrameBuffer.Ptr) {
		pAdapter->m_FrameSegment.Close();
	}

	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestPresentFb(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned)
{
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct FrameMetaData* ptr = NULL;

	VioGpuAdapterLite* pAdapter =
		(VioGpuAdapterLite*)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Coudlnt' find adapter\n");
		return status;
	}

	status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, (PVOID*)&ptr, NULL);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return STATUS_INVALID_USER_BUFFER;
	}

	status = pAdapter->ExecutePresentDisplayZeroCopy(
		(BYTE*)ptr->addr,
		ptr->bitrate,
		ptr->pitch,
		ptr->width,
		ptr->height);

	if (status != STATUS_SUCCESS)
		return STATUS_UNSUCCESSFUL;

	return STATUS_SUCCESS;
}

static NTSTATUS IoctlRequestEdid(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned)
{
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(BytesReturned);
	TRACING();

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	struct edid_info* edata = NULL;
	size_t bufSize;

	if (DeviceContext == NULL) {
		ERR("Invalid Device Context\n");
		return status;
	}

	VioGpuAdapterLite* pAdapter =
		(VioGpuAdapterLite*)(DeviceContext ? DeviceContext->pvDeviceExtension : 0);

	if (!pAdapter) {
		ERR("Coudlnt' find adapter\n");
		return status;
	}

	status = WdfRequestRetrieveInputBuffer(Request, 0, (PVOID*)&edata, &bufSize);
	if (!NT_SUCCESS(status)) {
		ERR("Couldn't retrieve Input buffer\n");
		WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
		return status;
	}
	if (edata->mode_size == 0) {
		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&edata, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return status;
		}
		//Return value from the KMDF DVServer
		if (mode_list.modelist_size != 0) {
			edata->mode_size = mode_list.modelist_size;
		} else {
			edata->mode_size = QEMU_MODELIST_SIZE;
		}
		WdfRequestSetInformation(Request, sizeof(struct edid_info));
	} else if ((edata->mode_size == mode_list.modelist_size) || (edata->mode_size == QEMU_MODELIST_SIZE)) {
		status = WdfRequestRetrieveOutputBuffer(Request, 0, (PVOID*)&edata, &bufSize);
		if (!NT_SUCCESS(status)) {
			ERR("Couldn't retrieve Output buffer\n");
			WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
			return status;
		}
		//Return value from the KMDF DVServer
		RtlCopyMemory(edata->edid_data, pAdapter->GetEdidData(0), EDID_V1_BLOCK_SIZE);

		for (unsigned int i = 0; i < edata->mode_size; i++) {
			edata->mode_list[i].width = gpu_disp_mode_ext[i].XResolution;
			edata->mode_list[i].height = gpu_disp_mode_ext[i].YResolution;
			edata->mode_list[i].refreshrate = gpu_disp_mode_ext[i].refresh;
		}
		WdfRequestSetInformation(Request, sizeof(struct edid_info));
	}
	return STATUS_SUCCESS;
}

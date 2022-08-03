/*++

Module Name:

    queue.h

Abstract:

    This file contains the queue definitions.

Environment:

    Kernel-mode Driver Framework

--*/

EXTERN_C_START

//
// This is the context that can be placed per queue
// and would contain per queue information.
//
typedef struct _QUEUE_CONTEXT {

    ULONG PrivateDeviceData;  // just a placeholder

} QUEUE_CONTEXT, *PQUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, QueueGetContext)

NTSTATUS
DVServerKMDQueueInitialize(
    _In_ WDFDEVICE Device
    );

static NTSTATUS IoctlRequestSetMode(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned);

static NTSTATUS IoctlRequestPresentFb(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned);

static NTSTATUS IoctlRequestEdid(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t* BytesReturned);

static NTSTATUS IoctlRequestTotalScreens(
	const PDEVICE_CONTEXT DeviceContext,
	const size_t          InputBufferLength,
	const size_t          OutputBufferLength,
	const WDFREQUEST      Request,
	size_t * BytesReturned);

//
// Events from the IoQueue object
//
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL DVServerKMDEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_STOP DVServerKMDEvtIoStop;

EXTERN_C_END

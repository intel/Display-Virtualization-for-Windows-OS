/*++

Module Name:

	device.h

Abstract:

	This file contains the device definitions.

Environment:

	Kernel-mode Driver Framework

--*/

#include "public.h"

EXTERN_C_START

//
// The device context performs the same job as
// a WDM device extension in the driver frameworks
//
typedef struct _DEVICE_CONTEXT
{
	ULONG PrivateDeviceData; // just a placeholder
	PVOID pvDeviceExtension; // for IntelVirtDisplayKMD
	WDFDEVICE WdfDevice;
	WDFINTERRUPT WdfInterrupt;
	WDFCMRESLIST ResourcesRaw;
	WDFCMRESLIST ResourcesTranslated;
	BUS_INTERFACE_STANDARD BusInterface;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

//
// This macro will generate an inline function called DeviceGetContext
// which will be used to get a pointer to the device context memory
// in a type safe manner.
//
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

//
// Function to initialize the device and its callbacks
//
NTSTATUS
IntelVirtDisplayKMDCreateDevice(_Inout_ PWDFDEVICE_INIT DeviceInit);

EVT_WDF_DEVICE_PREPARE_HARDWARE IntelVirtDisplayKMDEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE IntelVirtDisplayKMDEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY IntelVirtDisplayKMDEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT IntelVirtDisplayKMDEvtD0Exit;
EVT_WDF_INTERRUPT_ISR IntelVirtDisplayKMDEvtInterruptISR;
EVT_WDF_INTERRUPT_DPC IntelVirtDisplayKMDEvtInterruptDPC;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED IntelVirtDisplayKMDEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED IntelVirtDisplayKMDEvtDeviceD0EntryPostInterruptsEnabled;
EVT_WDF_INTERRUPT_ENABLE IntelVirtDisplayKMDEvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE IntelVirtDisplayKMDEvtInterruptDisable;
EXTERN_C_END

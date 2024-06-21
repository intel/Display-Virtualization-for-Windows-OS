/*++

Module Name:

	driver.c

Abstract:

	This file contains the driver entry points and callbacks.

Environment:

	Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "Trace.h"
#include <Driver.tmh>
extern "C" {
#include "kdebugprint.h"
	tDebugPrintFunc VirtioDebugPrintProc;
}

int nDebugLevel;
int virtioDebugLevel;
int bDebugPrint;
int bBreakAlways;
char module_name[80] = "KMD";

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, DVServerKMDEvtDeviceAdd)
#pragma alloc_text (PAGE, DVServerKMDEvtDriverContextCleanup)
#pragma alloc_text (PAGE, DVServerKMDEvtPrepareHardware)
#pragma alloc_text (PAGE, DVServerKMDEvtReleaseHardware)
#pragma alloc_text (PAGE, DVServerKMDEvtD0Entry)
#pragma alloc_text (PAGE, DVServerKMDEvtD0Exit)
#pragma alloc_text (PAGE, DVServerKMDEvtDriverUnload)
#endif



NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:
	DriverEntry initializes the driver and is the first routine called by the
	system after the driver is loaded. DriverEntry specifies the other entry
	points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

	DriverObject - represents the instance of the function driver that is loaded
	into memory. DriverEntry must initialize members of DriverObject before it
	returns to the caller. DriverObject is allocated by the system before the
	driver is loaded, and it is released by the system after the system unloads
	the function driver from memory.

	RegistryPath - represents the driver specific path in the Registry.
	The function driver can use the path to store driver related data between
	reboots. The path does not store hardware instance specific data.

Return Value:

	STATUS_SUCCESS if successful,
	STATUS_UNSUCCESSFUL otherwise.

--*/
{
	WDF_DRIVER_CONFIG config;
	NTSTATUS status;
	WDF_OBJECT_ATTRIBUTES attributes;

	//
	// Initialize WPP Tracing
	//
	//
	// Register a cleanup callback so that we can call WPP_CLEANUP when
	// the framework driver object is deleted during driver unload.
	//
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.EvtCleanupCallback = DVServerKMDEvtDriverContextCleanup;

	WPP_INIT_TRACING(DriverObject, RegistryPath);
	TRACING();

	WDF_DRIVER_CONFIG_INIT(&config,
		DVServerKMDEvtDeviceAdd
	);
	config.EvtDriverUnload = DVServerKMDEvtDriverUnload;
	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status)) {
		WPP_CLEANUP(DriverObject);
		return status;
	}

	return status;
}

VOID
DVServerKMDEvtDriverUnload(
	_In_ WDFDRIVER driver
)
/*++
Routine Description:

	OnDriverUnload is called by the framework when the driver unloads.

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry.

Return Value:

	void

--*/
{
	TRACING();
	WPP_CLEANUP(WdfDriverWdmGetDriverObject(driver));

	return;
}

NTSTATUS
DVServerKMDEvtDeviceAdd(
	_In_    WDFDRIVER       Driver,
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
/*++
Routine Description:

	EvtDeviceAdd is called by the framework in response to AddDevice
	call from the PnP manager. We create and initialize a device object to
	represent a new instance of the device.

Arguments:

	Driver - Handle to a framework driver object created in DriverEntry

	DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

	NTSTATUS

--*/
{
	NTSTATUS status;
	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;

	UNREFERENCED_PARAMETER(Driver);
	TRACING();

	PAGED_CODE();


	WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

	//
	// Zero out the PnpPowerCallbacks structure.
	//
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

	//
	// Set Callbacks for any of the functions we are interested in.
	// If no callback is set, Framework will take the default action
	// by itself.
	//
	pnpPowerCallbacks.EvtDevicePrepareHardware = DVServerKMDEvtPrepareHardware;
	pnpPowerCallbacks.EvtDeviceReleaseHardware = DVServerKMDEvtReleaseHardware;

	//
	// These two callbacks set up and tear down hardware state that must be
	// done every time the device moves in and out of the D0-working state.
	//
	pnpPowerCallbacks.EvtDeviceD0Entry = DVServerKMDEvtD0Entry;
	pnpPowerCallbacks.EvtDeviceD0Exit = DVServerKMDEvtD0Exit;

	pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = DVServerKMDEvtDeviceD0ExitPreInterruptsDisabled;
	pnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = DVServerKMDEvtDeviceD0EntryPostInterruptsEnabled;

	//
	// Register the PnP Callbacks..
	//
	WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

	//
	// Create the device
	//
	status = DVServerKMDCreateDevice(DeviceInit);


	return status;
}

VOID
DVServerKMDEvtDriverContextCleanup(
	_In_ WDFOBJECT DriverObject
)
/*++
Routine Description:

	Free all the resources allocated in DriverEntry.

Arguments:

	DriverObject - handle to a WDF Driver object.

Return Value:

	VOID.

--*/
{
	UNREFERENCED_PARAMETER(DriverObject);

	PAGED_CODE();
	TRACING();
}

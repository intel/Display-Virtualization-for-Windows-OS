/*++

Module Name:

    driver.h

Abstract:

    This file contains the driver definitions.

Environment:

    Kernel-mode Driver Framework

--*/

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>

#include "device.h"
#include "queue.h"
#include "trace.h"

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD DVServerKMDEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP DVServerKMDEvtDriverContextCleanup;

#define ENABLE_FRAME_TRACE
#define ENABLE_CURSOR_TRACE
#define ENABLE_IMAGE_DUMP
#define NUMBEROFBYTES 10

#define WPP_DEBUG(b) DbgPrint b

#ifdef ENABLE_FRAME_TRACE
#define PRINT_FRAME_DATA(b)		WPP_DEBUG(b)
#else
#define PRINT_FRAME_DATA(b)
#endif

#ifdef ENABLE_CURSOR_TRACE
#define PRINT_CURSOR_DATA(b)	WPP_DEBUG(b)
#else
#define PRINT_CURSOR_DATA(b)
#endif

#ifdef ENABLE_IMAGE_DUMP
#define PRINT_IMAGE_DATA(b)		WPP_DEBUG(b)
#else
#define PRINT_IMAGE_DATA(b)
#endif

#define IMAGE_HEX_DUMP(n) for(int x = 0; x < n; x++) PRINT_IMAGE_DATA(("%x", addr[x]))

#define DVSERVERKMD_SUCCESS 1
#define DVSERVERKMD_FAILURE 0

EXTERN_C_END

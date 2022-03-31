/*++

Copyright (c) Microsoft Corporation

Abstract:

    This module contains a sample implementation of an indirect display driver. See the included README.md file and the
    various TODO blocks throughout this file and all accompanying files for information on building a production driver.

    MSDN documentation on indirect displays can be found at https://msdn.microsoft.com/en-us/library/windows/hardware/mt761968(v=vs.85).aspx.

Environment:

    User Mode, UMDF

--*/

#include "Driver.h"
//#include "Driver.tmh"
#include "DVServercommon.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <Windows.h>
#include <winioctl.h>
#include <SetupAPI.h>
#include <initguid.h>

#pragma comment (lib, "Setupapi.lib")

//DVServer KMD related
PSP_DEVICE_INTERFACE_DETAIL_DATA device_iface_data;
HDEVINFO devinfo_handle;
HANDLE devHandle_frame;
BOOL g_init_kmd_resources = TRUE;

using namespace std;
using namespace Microsoft::IndirectDisp;
using namespace Microsoft::WRL;

#pragma region SampleMonitors

static constexpr DWORD IDD_SAMPLE_MONITOR_COUNT = 1; // If monitor count > ARRAYSIZE(s_SampleMonitors), we create edid-less monitors

// Default modes reported for edid-less monitors. The first mode is set as preferred
static const struct IndirectSampleMonitor::SampleMonitorMode s_SampleDefaultModes[] = 
{
    { 1920, 1080, 60 },
    { 1600,  900, 60 },
    { 1024,  768, 75 },
};

#ifdef DVSERVER_HWDCURSOR

IDDCX_MONITOR m_IddCxMonitorObject;
IDARG_IN_QUERY_HWCURSOR g_inputargs = { 0 };
Microsoft::WRL::Wrappers::Event g_iddcursor_os_event;
HANDLE cursor_event = NULL;
HANDLE devHandle_cursor;

#define CURSOR_BUFFER_SIZE				128*1024
#define CURSOR_MAX_WIDTH				32
#define CURSOR_MAX_HEIGHT				32
#define INITIAL_CURSOR_SHAPE_ID			0
#define IDD_CURSOREVENT_WAIT_TIMEOUT	16 // 16ms wait timeout for the IDD cursor event

#endif //end of DVSERVER_HWDCURSOR

// FOR SAMPLE PURPOSES ONLY, Static info about monitors that will be reported to OS
struct IndirectSampleMonitor s_SampleMonitors[] =
{
    // Modified EDID from Dell S2719DGF
    {
        {
            0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x10,0xAC,0xE6,0xD0,0x55,0x5A,0x4A,0x30,0x24,0x1D,0x01,
            0x04,0xA5,0x3C,0x22,0x78,0xFB,0x6C,0xE5,0xA5,0x55,0x50,0xA0,0x23,0x0B,0x50,0x54,0x00,0x02,0x00,
            0xD1,0xC0,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x58,0xE3,0x00,
            0xA0,0xA0,0xA0,0x29,0x50,0x30,0x20,0x35,0x00,0x55,0x50,0x21,0x00,0x00,0x1A,0x00,0x00,0x00,0xFF,
            0x00,0x37,0x4A,0x51,0x58,0x42,0x59,0x32,0x0A,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFC,0x00,
            0x53,0x32,0x37,0x31,0x39,0x44,0x47,0x46,0x0A,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFD,0x00,0x28,
            0x9B,0xFA,0xFA,0x40,0x01,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x2C
        },
        {
            { 1024, 768,  60 },
            { 1600, 1200, 60 },
            { 1920, 1080, 60 },
        },
        0
    },
    // Modified EDID from Lenovo Y27fA
    {
        {
            0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x30,0xAE,0xBF,0x65,0x01,0x01,0x01,0x01,0x20,0x1A,0x01,
            0x04,0xA5,0x3C,0x22,0x78,0x3B,0xEE,0xD1,0xA5,0x55,0x48,0x9B,0x26,0x12,0x50,0x54,0x00,0x08,0x00,
            0xA9,0xC0,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x68,0xD8,0x00,
            0x18,0xF1,0x70,0x2D,0x80,0x58,0x2C,0x45,0x00,0x53,0x50,0x21,0x00,0x00,0x1E,0x00,0x00,0x00,0x10,
            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFD,0x00,
            0x30,0x92,0xB4,0xB4,0x22,0x01,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x00,0xFC,0x00,0x4C,
            0x45,0x4E,0x20,0x59,0x32,0x37,0x66,0x41,0x0A,0x20,0x20,0x20,0x00,0x11
        },
        {
            { 3840, 2160,  60 },
            { 1600,  900,  60 },
            { 1024,  768,  60 },
        },
        0
    }
};

#pragma endregion

#pragma region helpers

static inline void FillSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& Mode, DWORD Width, DWORD Height, DWORD VSync, bool bMonitorMode)
{
    Mode.totalSize.cx = Mode.activeSize.cx = Width;
    Mode.totalSize.cy = Mode.activeSize.cy = Height;

    // See https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_video_signal_info
    Mode.AdditionalSignalInfo.vSyncFreqDivider = bMonitorMode ? 0 : 1;
    Mode.AdditionalSignalInfo.videoStandard = 255;

    Mode.vSyncFreq.Numerator = VSync;
    Mode.vSyncFreq.Denominator = 1;
    Mode.hSyncFreq.Numerator = VSync * Height;
    Mode.hSyncFreq.Denominator = 1;

    Mode.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;

    Mode.pixelRate = ((UINT64) VSync) * ((UINT64) Width) * ((UINT64) Height);
}

static IDDCX_MONITOR_MODE CreateIddCxMonitorMode(DWORD Width, DWORD Height, DWORD VSync, IDDCX_MONITOR_MODE_ORIGIN Origin = IDDCX_MONITOR_MODE_ORIGIN_DRIVER)
{
    IDDCX_MONITOR_MODE Mode = {};

    Mode.Size = sizeof(Mode);
    Mode.Origin = Origin;
    FillSignalInfo(Mode.MonitorVideoSignalInfo, Width, Height, VSync, true);

    return Mode;
}

static IDDCX_TARGET_MODE CreateIddCxTargetMode(DWORD Width, DWORD Height, DWORD VSync)
{
    IDDCX_TARGET_MODE Mode = {};

    Mode.Size = sizeof(Mode);
    FillSignalInfo(Mode.TargetVideoSignalInfo.targetVideoSignalInfo, Width, Height, VSync, false);

    return Mode;
}

#pragma endregion

extern "C" DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD IddSampleDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY IddSampleDeviceD0Entry;

EVT_IDD_CX_ADAPTER_INIT_FINISHED IddSampleAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES IddSampleAdapterCommitModes;

EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION IddSampleParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES IddSampleMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES IddSampleMonitorQueryModes;

EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN IddSampleMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN IddSampleMonitorUnassignSwapChain;

struct IndirectDeviceContextWrapper
{
    IndirectDeviceContext* pContext;

    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

struct IndirectMonitorContextWrapper
{
    IndirectMonitorContext* pContext;

    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

// This macro creates the methods for accessing an IndirectDeviceContextWrapper as a context for a WDF object
WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);

WDF_DECLARE_CONTEXT_TYPE(IndirectMonitorContextWrapper);

extern "C" BOOL WINAPI DllMain(
    _In_ HINSTANCE hInstance,
    _In_ UINT dwReason,
    _In_opt_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(lpReserved);
    UNREFERENCED_PARAMETER(dwReason);

    return TRUE;
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(
    PDRIVER_OBJECT  pDriverObject,
    PUNICODE_STRING pRegistryPath
)
{
    WDF_DRIVER_CONFIG Config;
    NTSTATUS Status;

    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);

    WDF_DRIVER_CONFIG_INIT(&Config,
        IddSampleDeviceAdd
    );

    Status = WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return Status;
}

_Use_decl_annotations_
NTSTATUS IddSampleDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;

    UNREFERENCED_PARAMETER(Driver);

    // Register for power callbacks - in this sample only power-on is needed
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
    PnpPowerCallbacks.EvtDeviceD0Entry = IddSampleDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

    IDD_CX_CLIENT_CONFIG IddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&IddConfig);

    // If the driver wishes to handle custom IoDeviceControl requests, it's necessary to use this callback since IddCx
    // redirects IoDeviceControl requests to an internal queue. This sample does not need this.
    // IddConfig.EvtIddCxDeviceIoControl = IddSampleIoDeviceControl;

    IddConfig.EvtIddCxAdapterInitFinished = IddSampleAdapterInitFinished;

    IddConfig.EvtIddCxParseMonitorDescription = IddSampleParseMonitorDescription;
    IddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = IddSampleMonitorGetDefaultModes;
    IddConfig.EvtIddCxMonitorQueryTargetModes = IddSampleMonitorQueryModes;
    IddConfig.EvtIddCxAdapterCommitModes = IddSampleAdapterCommitModes;
    IddConfig.EvtIddCxMonitorAssignSwapChain = IddSampleMonitorAssignSwapChain;
    IddConfig.EvtIddCxMonitorUnassignSwapChain = IddSampleMonitorUnassignSwapChain;

    Status = IddCxDeviceInitConfig(pDeviceInit, &IddConfig);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);
    Attr.EvtCleanupCallback = [](WDFOBJECT Object)
    {
        // Automatically cleanup the context when the WDF object is about to be deleted
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Object);
        if (pContext)
        {
            pContext->Cleanup();
        }
    };

    WDFDEVICE Device = nullptr;
    Status = WdfDeviceCreate(&pDeviceInit, &Attr, &Device);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = IddCxDeviceInitialize(Device);

    // Create a new device context object and attach it to the WDF device object
    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext = new IndirectDeviceContext(Device);
	
	if (get_edid_data() == DVSERVERUMD_FAILURE) {
		WriteToLog("QEMU EDID initialization failed, falling back to default IDD EDID");
	}

    return Status;
}

_Use_decl_annotations_
NTSTATUS IddSampleDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);

    // This function is called by WDF to start the device in the fully-on power state.

    auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    pContext->pContext->InitAdapter();

    return STATUS_SUCCESS;
}

#pragma region Direct3DDevice

Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid)
{

}

Direct3DDevice::Direct3DDevice()
{
    AdapterLuid = LUID{};
}

HRESULT Direct3DDevice::Init()
{
    // The DXGI factory could be cached, but if a new render adapter appears on the system, a new factory needs to be
    // created. If caching is desired, check DxgiFactory->IsCurrent() each time and recreate the factory if !IsCurrent.
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
    if (FAILED(hr))
    {
        return hr;
    }

    // Find the specified render adapter
    hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(hr))
    {
        return hr;
    }

    // Create a D3D device using the render adapter. BGRA support is required by the WHQL test suite.
    hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext);
    if (FAILED(hr))
    {
        // If creating the D3D device failed, it's possible the render GPU was lost (e.g. detachable GPU) or else the
        // system is in a transient state.
        return hr;
    }

    return S_OK;
}

#pragma endregion

#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent)
    : m_hSwapChain(hSwapChain), m_Device(Device), m_hAvailableBufferEvent(NewFrameEvent)
{
	init();

    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));

	if (get_dvserver_kmdf_device() == DVSERVERUMD_FAILURE) {
		WriteToLog("\nKMD resource Init Failed");
		goto exit;
	}

	// ****** Frame Resources ******
	//Create Device frame Handle to DVServerKMD
	devHandle_frame = CreateFile(device_iface_data->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	if (devHandle_frame == INVALID_HANDLE_VALUE) {
		WriteToLog("\nCreateFile for Frame returned INVALID_HANDLE_VALUE");
		goto exit;
	}

	//Allocate memory for IOCTL Response Buffer from DVServerKMD (for frame)
	m_ioctlresp_frame = (struct KMDF_IOCTL_Response*)malloc(sizeof(struct KMDF_IOCTL_Response));
	if (m_ioctlresp_frame == NULL) {
		WriteToLog("\nFailed allocating IOCTL Response structure (for frame) !!!");
		goto exit;
	}

	//Allocate memory for FrameMeta Data pointer
	m_framedata = (struct FrameMetaData*)malloc(sizeof(struct FrameMetaData));
	if (m_framedata == NULL) {
		WriteToLog("\nFailed allocating frame metadata structure !!!");
		goto exit;
	}

	m_GPUResourceMutex = CreateMutex(NULL, FALSE, NULL);
	if (m_GPUResourceMutex == NULL){
		WriteToLog("GPU CreateMutex Failed");
		goto exit;
	}

	// Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
	m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));

#ifdef DVSERVER_HWDCURSOR
	// ****** Cursor Resources ******
	//Create Device cursor Handle to DVServerKMD
	devHandle_cursor = CreateFile(device_iface_data->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	if (devHandle_cursor == INVALID_HANDLE_VALUE) {
		WriteToLog("\nCreateFile for cursor returned INVALID_HANDLE_VALUE");
		goto exit;
	}

	//Allocate memory for IOCTL Response Buffer from DVServerKMD (for cursor)
	m_ioctlresp_cursor = (struct KMDF_IOCTL_Response*)malloc(sizeof(struct KMDF_IOCTL_Response));
	if (m_ioctlresp_cursor == NULL) {
		WriteToLog("\nFailed allocating IOCTL Response structure (for cursor) !!!");
		goto exit;
	}

	//Allocate memory for Cursor Data pointer
	m_cursordata = (struct CursorData*)malloc(sizeof(struct CursorData));
	if (m_cursordata == NULL) {
		WriteToLog("\nFailed allocating cursor structure !!!");
		goto exit;
	}

	//Create DVServer Cursor Thread
	cursorthread_handle = CreateThread(nullptr, 0, CursorThread, this, 0, nullptr);
	if (cursorthread_handle == INVALID_HANDLE_VALUE) {
		WriteToLog("\nFailed creating cursor thread !!!");
		goto exit;
	}
#endif //end of DVSERVER_HWDCURSOR

	WriteToLog("\n Init Resources Successful!!!");
	return;

exit:
	g_init_kmd_resources = FALSE;
	cleanup_resources();
}

SwapChainProcessor::~SwapChainProcessor()
{
    // Alert the swap-chain processing thread to terminate
    SetEvent(m_hTerminateEvent.Get());

    if (m_hThread.Get())
    {
        // Wait for the thread to terminate
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }

	//Cleanup other supporting resources
	cleanup_resources();
}

/*******************************************************************************
*
* Description
*
* cleanup_resources - Once the SwapChain stops processing the frames,
* this function will close the DVServer device handle and releases the
* assigned frame metadata and IOCTL
*
* Parameters
* Null
*
* Return val
* Null
*
******************************************************************************/
void SwapChainProcessor::cleanup_resources()
{
	// ****** Frame Resources ******
	if (devHandle_frame != INVALID_HANDLE_VALUE)
		CloseHandle(devHandle_frame);

	if (m_framedata != INVALID_HANDLE_VALUE)
		free(m_framedata);

	if (m_ioctlresp_frame != INVALID_HANDLE_VALUE)
		free(m_ioctlresp_frame);

	if (m_GPUResourceMutex != INVALID_HANDLE_VALUE) {
		ReleaseMutex(m_GPUResourceMutex);
		CloseHandle(m_GPUResourceMutex);
	}

#ifdef DVSERVER_HWDCURSOR
	// ****** Cursor Resources ******
	if (devHandle_cursor != INVALID_HANDLE_VALUE)
		CloseHandle(devHandle_cursor);

	if (m_cursordata != INVALID_HANDLE_VALUE)
		free(m_cursordata);

	if (m_ioctlresp_cursor != INVALID_HANDLE_VALUE)
		free(m_ioctlresp_cursor);
	
	if (g_inputargs.pShapeBuffer != INVALID_HANDLE_VALUE)
		free(g_inputargs.pShapeBuffer);

	if (cursorthread_handle != INVALID_HANDLE_VALUE)
		CloseHandle(cursorthread_handle);
#endif //end of DVSERVER_HWDCURSOR 

	// ****** Outher KMD Resources ******
	if (device_iface_data != INVALID_HANDLE_VALUE)
		free(device_iface_data);

	if (devinfo_handle != INVALID_HANDLE_VALUE)
		SetupDiDestroyDeviceInfoList(devinfo_handle);
}

/*******************************************************************************
*
* Description
*
* init - This function will initialize the frame related variables
*
* Parameters
* Null
*
* Return val
* Null
*
******************************************************************************/
void SwapChainProcessor::init()
{
	g_init_kmd_resources = TRUE;
	m_resolution_changed = TRUE;

	//Frame related
	m_width = 0;
	m_height = 0;
	m_pitch = 0;
	m_stride = 0;
	m_format = FRAME_TYPE_INVALID;
	m_ioctlresp_size = 0;
	m_framedata = NULL;
	m_ioctlresp_frame = NULL;
	m_destimage = NULL;
	m_IAcquiredDesktopImage = NULL;
	m_frame_statistics_counter = 1; //init the frame statistics counter
	m_staging_buffer.pData = NULL;
	m_staging_buffer.DepthPitch = 0;
	m_staging_buffer.RowPitch = 0;	
	m_GPUResourceMutex = NULL;
}


#ifdef DVSERVER_HWDCURSOR
DWORD CALLBACK SwapChainProcessor::CursorThread(LPVOID Argument)
{
	//Setting the cursor thread priority to HIGH; Runcore thread will get more priority from OS, inorder to see smooth cursor movement 
	//we are rasing the cursor thread priority to HIGH and we will be waiting for OS to send cursor events 
	if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
		WriteToLog("CURSOR: Failed to set the thread priority to THREAD_PRIORITY_HIGHEST ");
		return GetLastError();
	}
	reinterpret_cast<SwapChainProcessor*>(Argument)->GetCursorData();

	return 0;
}
#endif

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    // For improved performance, make use of the Multimedia Class Scheduler Service, which will intelligently
    // prioritize this thread for improved throughput in high CPU-load scenarios.
    DWORD AvTask = 0;
    HANDLE AvTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &AvTask);

    RunCore();

    // Always delete the swap-chain object when swap-chain processing loop terminates in order to kick the system to
    // provide a new swap-chain if necessary.
    WdfObjectDelete((WDFOBJECT)m_hSwapChain);
    m_hSwapChain = nullptr;

    AvRevertMmThreadCharacteristics(AvTaskHandle);
}

void SwapChainProcessor::RunCore()
{
    // Get the DXGI device interface
    ComPtr<IDXGIDevice> DxgiDevice;
	HRESULT status;
	char temp_str[50];

    HRESULT hr = m_Device->Device.As(&DxgiDevice);
    if (FAILED(hr))
    {
        return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
    SetDevice.pDevice = DxgiDevice.Get();

    hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
    if (FAILED(hr))
    {
        return;
    }

	//reset the resolution flag whenever there is a resolution change
	m_resolution_changed = TRUE;

    // Acquire and release buffers in a loop
    for (;;)
    {
        ComPtr<IDXGIResource> AcquiredBuffer;

        // Ask for the next buffer from the producer
        IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer = {};
        hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &Buffer);

        // AcquireBuffer immediately returns STATUS_PENDING if no buffer is yet available
        if (hr == E_PENDING)
        {
            // We must wait for a new buffer
            HANDLE WaitHandles [] =
            {
                m_hAvailableBufferEvent,
                m_hTerminateEvent.Get()
            };
            DWORD WaitResult = WaitForMultipleObjects(ARRAYSIZE(WaitHandles), WaitHandles, FALSE, 16);
            if (WaitResult == WAIT_OBJECT_0 || WaitResult == WAIT_TIMEOUT)
            {
                // We have a new buffer, so try the AcquireBuffer again
                continue;
            }
            else if (WaitResult == WAIT_OBJECT_0 + 1)
            {
                // We need to terminate
                break;
            }
            else
            {
                // The wait was cancelled or something unexpected happened
                hr = HRESULT_FROM_WIN32(WaitResult);
                break;
            }
        }
        else if (SUCCEEDED(hr))
        {
			if ((g_init_kmd_resources == TRUE)) {

				// We have new frame to process, the surface has a reference on it that the driver has to release
				AcquiredBuffer.Attach(Buffer.MetaData.pSurface);
				status = Buffer.MetaData.pSurface->QueryInterface(IID_PPV_ARGS(&m_IAcquiredDesktopImage));
				if (FAILED(status)) {
					sprintf_s(temp_str, "RUNCORE: Failed QueryInterface %d", status);
					WriteToLog(temp_str);
					AcquiredBuffer.Reset();
					break;
				}

				//Get Frame	from GPU
				if (GetFrameData(m_Device, m_IAcquiredDesktopImage) == DVSERVERUMD_FAILURE) {
					WriteToLog("RUNCORE: Failed getting frame from GPU.. Exiting Runcore");
					//We need to reset the swapchain in case of any catastrophic failures or any kind of TDR in GFX driver
					//so exiting the runcore, OS will restart the IDD display
					break;
				}

				AcquiredBuffer.Reset();

				// Indicate to OS that we have finished inital processing of the frame, it is a hint that
				// OS could start preparing another frame
				hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
				if (FAILED(hr))
				{
					break;
				}

				//Need to report frame statistics to OS for every 60 frames
				if (m_frame_statistics_counter == REPORT_FRAME_STATS) {
					report_frame_statistics(Buffer);
					m_frame_statistics_counter = 0; // reset the frame statistics counter
				}
				m_frame_statistics_counter++;
			}
        }
        else
        {
            // The swap-chain was likely abandoned (e.g. DXGI_ERROR_ACCESS_LOST), so exit the processing loop
            break;
        }
    }
}

/*******************************************************************************
*
* Description
*
* GetFrameData - This function configures the frame metadata and gets the  
* frame from GPU.It passes the frame to DVserverKMD using IOCTL call :  
* set_mode and frame_data
*
* Parameters
* idd_device - shared_ptr to  Direct3D Device (Direct3D render device)
* desktopimage - ptr to ID3D11Texture2D  (A 2D texture interface manages  
* texel data). Uses method struct D3D11_TEXTURE2D_DESC to get the properties  
* of texture resource
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int SwapChainProcessor::GetFrameData(std::shared_ptr<Direct3DDevice> idd_device, ID3D11Texture2D* desktopimage)
{
	HRESULT status;

	if (desktopimage == INVALID_HANDLE_VALUE) {
		WriteToLog("\nRUNCORE : desktopimage pointer is NULL.. exiting");
		return DVSERVERUMD_FAILURE;
	}

	if (m_resolution_changed == TRUE) {
		WriteToLog("\nRUNCORE : ResolutionChanged, setting up new staging buffer");
		ZeroMemory(&m_staging_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
		ZeroMemory(&m_staging_desc, sizeof(m_staging_desc));

		desktopimage->GetDesc(&m_input_desc);
		m_width = m_input_desc.Width;
		m_height = m_input_desc.Height;

		switch (m_input_desc.Format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM: m_format = FRAME_TYPE_BGRA; break;
		case DXGI_FORMAT_R8G8B8A8_UNORM: m_format = FRAME_TYPE_RGBA; break;
		case DXGI_FORMAT_R10G10B10A2_UNORM: m_format = FRAME_TYPE_RGBA10; break;
		default:
			WriteToLog("RUNCORE: Unsupported source format");
		}

		/* Configure the staging descriptor  */
		m_staging_desc.Width = m_input_desc.Width;
		m_staging_desc.Height = m_input_desc.Height;
		m_staging_desc.MipLevels = 1;
		m_staging_desc.ArraySize = m_input_desc.ArraySize;
		m_staging_desc.SampleDesc.Count = 1;
		m_staging_desc.SampleDesc.Quality = 0;
		m_staging_desc.Usage = D3D11_USAGE_STAGING;
		m_staging_desc.Format = m_input_desc.Format;
		m_staging_desc.BindFlags = 0;
		m_staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		m_staging_desc.MiscFlags = 0;

		/* Check if all the parameters in the staging descriptor is proper or not */
		if (idd_device->Device->CreateTexture2D(&m_staging_desc, NULL, NULL) != S_FALSE) {
			WriteToLog("RUNCORE: Failed Staging Buffer invalid configurations");
			return DVSERVERUMD_FAILURE;
		}
	}

	/* Create Texture2D with the staging descriptor parameters */
	idd_device->Device->CreateTexture2D(&m_staging_desc, NULL, &m_destimage);
	if (m_destimage == NULL) {
		WriteToLog("RUNCORE: Failed Staging Buffer CreateTexture2D is NULL");
		return DVSERVERUMD_FAILURE;
	}

	WaitForSingleObject(m_GPUResourceMutex,INFINITE);
	idd_device->DeviceContext->CopyResource((ID3D11Resource*)m_destimage, (ID3D11Resource*)desktopimage);
	desktopimage->Release();
	status = idd_device->DeviceContext->Map((ID3D11Resource*)m_destimage, 0, D3D11_MAP_READ, 0, &m_staging_buffer);
	if (FAILED(status)) {
		WriteToLog("RUNCORE : Failed to Map the resource idd_device->DeviceContext->Map");
		m_destimage->Release();
		ReleaseMutex(m_GPUResourceMutex);
		return DVSERVERUMD_FAILURE;
	}
	ReleaseMutex(m_GPUResourceMutex);

	m_pitch = m_staging_buffer.RowPitch;
	m_stride = m_staging_buffer.RowPitch / 4;
	m_framedata->width = m_width;
	m_framedata->height = m_height;
	m_framedata->format = DVSERVERUMD_COLORFORMAT;
	m_framedata->pitch = m_pitch;
	m_framedata->stride = m_stride;	
	m_framedata->bitrate = DVSERVER_BBP;
	//Assigning the m_staging_buffer buffer addr
	m_framedata->addr = (void*)m_staging_buffer.pData;

	if (m_resolution_changed == TRUE) {
		WriteToLog("RUNCORE : ResolutionChanged - sending SET MODE IOCTL");
		//m_framedata->refresh_rate = FRAME_RR;
		if (!DeviceIoControl(devHandle_frame, IOCTL_DVSERVER_SET_MODE, \
                        m_framedata, \
                        sizeof(struct FrameMetaData), m_ioctlresp_frame, \
                        sizeof(struct KMDF_IOCTL_Response), \
                        &m_ioctlresp_size, NULL)) {
			WriteToLog("RUNCORE : IOCTL_DVSERVER_SET_MODE call failed!\n");
			idd_device->DeviceContext->Unmap(m_destimage, 0);
			m_destimage->Release();
			return DVSERVERUMD_FAILURE;
		}
		m_resolution_changed = FALSE;
	}

	if (!DeviceIoControl(devHandle_frame, IOCTL_DVSERVER_FRAME_DATA, \
                        m_framedata, sizeof(struct FrameMetaData), \
                        m_ioctlresp_frame, sizeof(struct KMDF_IOCTL_Response), \
                        &m_ioctlresp_size, NULL)) {
		WriteToLog("RUNCORE : IOCTL_DVSERVER_FRAME_DATA call failed!");
		idd_device->DeviceContext->Unmap(m_destimage, 0);
		m_destimage->Release();
		return DVSERVERUMD_FAILURE;
	}

	idd_device->DeviceContext->Unmap(m_destimage, 0);
	m_destimage->Release();
	return DVSERVERUMD_SUCCESS;
}

/********************************************************************************
* Description
*
* report_frame_statistics - This function report frame statistics to OS for
* every 60 frames
*
* Parameters
* buffer - Access Per-frame metadata and frame information
*
* Return val
* Null
*
******************************************************************************/
void SwapChainProcessor::report_frame_statistics(IDARG_OUT_RELEASEANDACQUIREBUFFER buffer)
{
		IDDCX_FRAME_STATISTICS_STEP FrameStep = { 0 };
		FrameStep.Size = sizeof(FrameStep);
		FrameStep.Type = IDDCX_FRAME_STATISTICS_STEP_TYPE_DRIVER_DEFINED_1;
		FrameStep.QpcTime = buffer.MetaData.PresentDisplayQPCTime;

		IDARG_IN_REPORTFRAMESTATISTICS ReportStatsIn = { 0 };
		ReportStatsIn.FrameStatistics.Size = sizeof(ReportStatsIn.FrameStatistics);
		ReportStatsIn.FrameStatistics.PresentationFrameNumber = buffer.MetaData.PresentationFrameNumber;
		ReportStatsIn.FrameStatistics.FrameStatus = IDDCX_FRAME_STATUS_COMPLETED;
		ReportStatsIn.FrameStatistics.FrameSliceTotal = 1;
		ReportStatsIn.FrameStatistics.FrameProcessingStepsCount = 1;
		ReportStatsIn.FrameStatistics.pFrameProcessingStep = &FrameStep;
		ReportStatsIn.FrameStatistics.FrameAcquireQpcTime = buffer.MetaData.PresentDisplayQPCTime;
		ReportStatsIn.FrameStatistics.SendStartQpcTime = buffer.MetaData.PresentDisplayQPCTime;
		ReportStatsIn.FrameStatistics.SendStopQpcTime = buffer.MetaData.PresentDisplayQPCTime;
		ReportStatsIn.FrameStatistics.SendCompleteQpcTime = buffer.MetaData.PresentDisplayQPCTime;
		ReportStatsIn.FrameStatistics.ProcessedPixelCount = m_width * m_height;
		ReportStatsIn.FrameStatistics.FrameSizeInBytes = ReportStatsIn.FrameStatistics.ProcessedPixelCount * 4;

		//This API will report the frame statistics to OS 
		IddCxSwapChainReportFrameStatistics(m_hSwapChain, &ReportStatsIn);
}

#ifdef DVSERVER_HWDCURSOR
void SwapChainProcessor::GetCursorData()
{
	HRESULT status;
	DWORD WaitResult;

	while (1) {
		if (g_init_kmd_resources == TRUE)
		{
			/* wait for 16ms to get the cursor events from OS */
			WaitResult = WaitForSingleObject(g_iddcursor_os_event.Get(), IDD_CURSOREVENT_WAIT_TIMEOUT);
			if (WaitResult == WAIT_OBJECT_0) {
				status = IddCxMonitorQueryHardwareCursor(m_IddCxMonitorObject, &g_inputargs, &m_outputargs);
				if (!NT_SUCCESS(status)) {
					WriteToLog("CURSOR: Failed IddCxMonitorQueryHardwareCursor");
					//We should try multiple attempts from OS for querying the hwd cursor, sometimes it will fail.
					continue;
				}
				else { //Success
					m_cursordata->cursor_x = m_outputargs.X;
					m_cursordata->cursor_y = m_outputargs.Y;
					m_cursordata->width = m_outputargs.CursorShapeInfo.Width;
					m_cursordata->height = m_outputargs.CursorShapeInfo.Height;
					//<< ToDo - for now we are sending cursor co-oridantes, later we will send the ptr data also >>

					if (!DeviceIoControl(devHandle_cursor, IOCTL_DVSERVER_CURSOR_DATA, \
                                            m_cursordata, sizeof(struct CursorData), \
                                            m_ioctlresp_cursor, sizeof(struct KMDF_IOCTL_Response), \
                                            &m_ioctlresp_size, NULL)) {
						WriteToLog("CURSOR: IOCTL_DVSERVER_CURSOR_DATA call failed!");
					}
				}
			}
			else if (WaitResult == WAIT_ABANDONED) {
				// We need to terminate
				break;
			}
		}
		else {
			//Since the cursor thread is running at high priority, before msft path is disabled sleep for some time and check 
			// if the g_init_kmd_resources flag is true or not.. This will avoid the the Runcore thread starvation
			Sleep(2000);
		}
	} // end of while(1)
}
#endif

/*******************************************************************************
*
* Description
*
* get_dvserver_kmdf_device - This function checks for the DVserverKMD device
* created and opens a handle for DVserverUMD to use
*
* Parameters
* Null
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int SwapChainProcessor::get_dvserver_kmdf_device()
{
	DWORD sizeof_deviceinterface_buf = 0;
	BOOL ret;
	SP_DEVICE_INTERFACE_DATA device_interface_data;

	device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	devinfo_handle = SetupDiGetClassDevs(NULL, NULL, NULL, DEVINFO_FLAGS);
	
	ret = SetupDiEnumDeviceInterfaces(devinfo_handle, 0, &GUID_DEVINTERFACE_DVSERVERKMD, 0, &device_interface_data);
	if (ret == FALSE) {
		WriteToLog("\nERROR : SetupDiEnumDeviceInterfaces - failed");
		return DVSERVERUMD_FAILURE;
	}

	SetupDiGetDeviceInterfaceDetail(devinfo_handle, &device_interface_data, 0, 0, &sizeof_deviceinterface_buf, 0);
	if (sizeof_deviceinterface_buf == 0) {
		WriteToLog("\nERROR : SetupDiGetDeviceInterfaceDetail - failed");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(sizeof_deviceinterface_buf);
	if (device_iface_data == NULL) {
		WriteToLog("\nERROR: Failed allocating memory for device interface data !!!");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
	ret = SetupDiGetDeviceInterfaceDetail(devinfo_handle, &device_interface_data, device_iface_data, sizeof_deviceinterface_buf, 0, 0);
	if (ret == FALSE) {
		WriteToLog("\nSetupDiGetDeviceInterfaceDetail Failed !!!");
		free(device_iface_data);
		return DVSERVERUMD_FAILURE;
	}

	return DVSERVERUMD_SUCCESS;
}

#pragma endregion

#pragma region IndirectDeviceContext

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice) :
    m_WdfDevice(WdfDevice)
{
    m_Adapter = {};
}

IndirectDeviceContext::~IndirectDeviceContext()
{
}

void IndirectDeviceContext::InitAdapter()
{
    // ==============================
    // TODO: Update the below diagnostic information in accordance with the target hardware. The strings and version
    // numbers are used for telemetry and may be displayed to the user in some situations.
    //
    // This is also where static per-adapter capabilities are determined.
    // ==============================

    IDDCX_ADAPTER_CAPS AdapterCaps = {};
    AdapterCaps.Size = sizeof(AdapterCaps);

	//This flag enables IDD Display to change the resolution
	AdapterCaps.Flags = IDDCX_ADAPTER_FLAGS_USE_SMALLEST_MODE;

    // Declare basic feature support for the adapter (required)
    AdapterCaps.MaxMonitorsSupported = IDD_SAMPLE_MONITOR_COUNT;
    AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
    AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;

    // Declare your device strings for telemetry (required)
    AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"IddSample Device";
    AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"Microsoft";
    AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"IddSample Model";

    // Declare your hardware and firmware versions (required)
    IDDCX_ENDPOINT_VERSION Version = {};
    Version.Size = sizeof(Version);
    Version.MajorVer = 1;
    AdapterCaps.EndPointDiagnostics.pFirmwareVersion = &Version;
    AdapterCaps.EndPointDiagnostics.pHardwareVersion = &Version;

    // Initialize a WDF context that can store a pointer to the device context object
    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT AdapterInit = {};
    AdapterInit.WdfDevice = m_WdfDevice;
    AdapterInit.pCaps = &AdapterCaps;
    AdapterInit.ObjectAttributes = &Attr;

    // Start the initialization of the adapter, which will trigger the AdapterFinishInit callback later
    IDARG_OUT_ADAPTER_INIT AdapterInitOut;
    NTSTATUS Status = IddCxAdapterInitAsync(&AdapterInit, &AdapterInitOut);

    if (NT_SUCCESS(Status))
    {
        // Store a reference to the WDF adapter handle
        m_Adapter = AdapterInitOut.AdapterObject;

        // Store the device context object into the WDF object context
        auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
        pContext->pContext = this;
    }
}

void IndirectDeviceContext::FinishInit(UINT ConnectorIndex)
{
    // ==============================
    // TODO: In a real driver, the EDID should be retrieved dynamically from a connected physical monitor. The EDIDs
    // provided here are purely for demonstration.
    // Monitor manufacturers are required to correctly fill in physical monitor attributes in order to allow the OS
    // to optimize settings like viewing distance and scale factor. Manufacturers should also use a unique serial
    // number every single device to ensure the OS can tell the monitors apart.
    // ==============================

    WDF_OBJECT_ATTRIBUTES Attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectMonitorContextWrapper);

    // In the sample driver, we report a monitor right away but a real driver would do this when a monitor connection event occurs
    IDDCX_MONITOR_INFO MonitorInfo = {};
    MonitorInfo.Size = sizeof(MonitorInfo);
	MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED;//DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    MonitorInfo.ConnectorIndex = ConnectorIndex;

    MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
    MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    if (ConnectorIndex >= ARRAYSIZE(s_SampleMonitors))
    {
        MonitorInfo.MonitorDescription.DataSize = 0;
        MonitorInfo.MonitorDescription.pData = nullptr;
    }
    else
    {
        MonitorInfo.MonitorDescription.DataSize = IndirectSampleMonitor::szEdidBlock;
        MonitorInfo.MonitorDescription.pData = const_cast<BYTE*>(s_SampleMonitors[ConnectorIndex].pEdidBlock);
    }

    // ==============================
    // TODO: The monitor's container ID should be distinct from "this" device's container ID if the monitor is not
    // permanently attached to the display adapter device object. The container ID is typically made unique for each
    // monitor and can be used to associate the monitor with other devices, like audio or input devices. In this
    // sample we generate a random container ID GUID, but it's best practice to choose a stable container ID for a
    // unique monitor or to use "this" device's container ID for a permanent/integrated monitor.
    // ==============================

    // Create a container ID
    CoCreateGuid(&MonitorInfo.MonitorContainerId);

    IDARG_IN_MONITORCREATE MonitorCreate = {};
    MonitorCreate.ObjectAttributes = &Attr;
    MonitorCreate.pMonitorInfo = &MonitorInfo;

    // Create a monitor object with the specified monitor descriptor
    IDARG_OUT_MONITORCREATE MonitorCreateOut;
    NTSTATUS Status = IddCxMonitorCreate(m_Adapter, &MonitorCreate, &MonitorCreateOut);
    if (NT_SUCCESS(Status))
    {
        // Create a new monitor context object and attach it to the Idd monitor object
        auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorCreateOut.MonitorObject);
        pMonitorContextWrapper->pContext = new IndirectMonitorContext(MonitorCreateOut.MonitorObject);

        // Tell the OS that the monitor has been plugged in
        IDARG_OUT_MONITORARRIVAL ArrivalOut;
        Status = IddCxMonitorArrival(MonitorCreateOut.MonitorObject, &ArrivalOut);
    }
}

IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor) :
    m_Monitor(Monitor)
{
}

IndirectMonitorContext::~IndirectMonitorContext()
{
    m_ProcessingThread.reset();

#ifdef DVSERVER_HWDCURSOR
	if (g_inputargs.pShapeBuffer != INVALID_HANDLE_VALUE)
		free(g_inputargs.pShapeBuffer);

	if (cursor_event != INVALID_HANDLE_VALUE)
		CloseHandle(cursor_event);
#endif //end of DVSERVER_HWDCURSOR 
}

#ifdef DVSERVER_HWDCURSOR
void IndirectMonitorContext::SetupIDDCursor(IDDCX_PATH* pPath)
{
	cursor_event = CreateEvent(NULL, false, false, NULL);
	if (cursor_event == INVALID_HANDLE_VALUE) {
		WriteToLog("SetupIDDCursor : Cursor Event Create Failed");
		return;
	}
	
	/* Configure HW cursor if path is active */
	if ((pPath->Flags & IDDCX_PATH_FLAGS_ACTIVE)) {
		IDARG_IN_SETUP_HWCURSOR SetupHwCursor = {};
		SetupHwCursor.CursorInfo.Size = sizeof(SetupHwCursor.CursorInfo);
		SetupHwCursor.CursorInfo.ColorXorCursorSupport = IDDCX_XOR_CURSOR_SUPPORT_NONE;
		SetupHwCursor.CursorInfo.AlphaCursorSupport = true;
		SetupHwCursor.CursorInfo.MaxX = CURSOR_MAX_WIDTH;
		SetupHwCursor.CursorInfo.MaxY = CURSOR_MAX_HEIGHT;

		//g_iddcursor_event.Attach(CreateEvent(nullptr, false, false, nullptr));
		g_iddcursor_os_event.Attach(cursor_event);
		SetupHwCursor.hNewCursorDataAvailable = g_iddcursor_os_event.Get();
		NTSTATUS Status = IddCxMonitorSetupHardwareCursor(m_Monitor, &SetupHwCursor);

		if (NT_SUCCESS(Status)) {
			m_IddCxMonitorObject = m_Monitor;
			g_inputargs.LastShapeId = INITIAL_CURSOR_SHAPE_ID;
			g_inputargs.ShapeBufferSizeInBytes = CURSOR_BUFFER_SIZE;
			g_inputargs.pShapeBuffer = (PBYTE)malloc(CURSOR_BUFFER_SIZE);
			if (g_inputargs.pShapeBuffer == INVALID_HANDLE_VALUE) {
				WriteToLog("SetupIDDCursor : Failed allocating cursor buffer !!!");
				g_init_kmd_resources = FALSE;
				return;
			}
			WriteToLog("SetupIDDCursor : Hardware cursor setup success");
		}
		else {
			WriteToLog("SetupIDDCursor : Hardware cursor setup failed");
			g_init_kmd_resources = FALSE;
		}
	}
}
#endif

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
    m_ProcessingThread.reset();

    auto Device = make_shared<Direct3DDevice>(RenderAdapter);
    if (FAILED(Device->Init()))
    {
        // It's important to delete the swap-chain if D3D initialization fails, so that the OS knows to generate a new
        // swap-chain and try again.
        WdfObjectDelete(SwapChain);
    }
    else
    {
        // Create a new swap-chain processing thread
        m_ProcessingThread.reset(new SwapChainProcessor(SwapChain, Device, NewFrameEvent));
    }
}

void IndirectMonitorContext::UnassignSwapChain()
{
    // Stop processing the last swap-chain
    m_ProcessingThread.reset();
}

#pragma endregion

#pragma region DDI Callbacks

_Use_decl_annotations_
NTSTATUS IddSampleAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    // This is called when the OS has finished setting up the adapter for use by the IddCx driver. It's now possible
    // to report attached monitors.

    auto* pDeviceContextWrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (NT_SUCCESS(pInArgs->AdapterInitStatus))
    {
        for (DWORD i = 0; i < IDD_SAMPLE_MONITOR_COUNT; i++)
        {
            pDeviceContextWrapper->pContext->FinishInit(i);
        }
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);

    // For the sample, do nothing when modes are picked - the swap-chain is taken care of by IddCx

    // ==============================
    // TODO: In a real driver, this function would be used to reconfigure the device to commit the new modes. Loop
    // through pInArgs->pPaths and look for IDDCX_PATH_FLAGS_ACTIVE. Any path not active is inactive (e.g. the monitor
    // should be turned off).
    // ==============================
#ifdef DVSERVER_HWDCURSOR
	auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(pInArgs->pPaths->MonitorObject);
	pMonitorContextWrapper->pContext->SetupIDDCursor(pInArgs->pPaths);
#endif //end of DVSERVER_HWDCURSOR 

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    // ==============================
    // TODO: In a real driver, this function would be called to generate monitor modes for an EDID by parsing it. In
    // this sample driver, we hard-code the EDID, so this function can generate known modes.
    // ==============================

    pOutArgs->MonitorModeBufferOutputCount = IndirectSampleMonitor::szModeList;

    if (pInArgs->MonitorModeBufferInputCount < IndirectSampleMonitor::szModeList)
    {
        // Return success if there was no buffer, since the caller was only asking for a count of modes
        return (pInArgs->MonitorModeBufferInputCount > 0) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
    }
    else
    {
        // In the sample driver, we have reported some static information about connected monitors
        // Check which of the reported monitors this call is for by comparing it to the pointer of
        // our known EDID blocks.

        if (pInArgs->MonitorDescription.DataSize != IndirectSampleMonitor::szEdidBlock)
            return STATUS_INVALID_PARAMETER;

        DWORD SampleMonitorIdx = 0;
        for(; SampleMonitorIdx < ARRAYSIZE(s_SampleMonitors); SampleMonitorIdx++)
        {
            if (memcmp(pInArgs->MonitorDescription.pData, s_SampleMonitors[SampleMonitorIdx].pEdidBlock, IndirectSampleMonitor::szEdidBlock) == 0)
            {
                // Copy the known modes to the output buffer
                for (DWORD ModeIndex = 0; ModeIndex < IndirectSampleMonitor::szModeList; ModeIndex++)
                {
                    pInArgs->pMonitorModes[ModeIndex] = CreateIddCxMonitorMode(
                        s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].Width,
                        s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].Height,
                        s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].VSync,
                        IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR
                    );
                }

                // Set the preferred mode as represented in the EDID
                pOutArgs->PreferredMonitorModeIdx = s_SampleMonitors[SampleMonitorIdx].ulPreferredModeIdx;
        
                return STATUS_SUCCESS;
            }
        }

        // This EDID block does not belong to the monitors we reported earlier
        return STATUS_INVALID_PARAMETER;
    }
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);

    // ==============================
    // TODO: In a real driver, this function would be called to generate monitor modes for a monitor with no EDID.
    // Drivers should report modes that are guaranteed to be supported by the transport protocol and by nearly all
    // monitors (such 640x480, 800x600, or 1024x768). If the driver has access to monitor modes from a descriptor other
    // than an EDID, those modes would also be reported here.
    // ==============================

    if (pInArgs->DefaultMonitorModeBufferInputCount == 0)
    {
        pOutArgs->DefaultMonitorModeBufferOutputCount = ARRAYSIZE(s_SampleDefaultModes); 
    }
    else
    {
        for (DWORD ModeIndex = 0; ModeIndex < ARRAYSIZE(s_SampleDefaultModes); ModeIndex++)
        {
            pInArgs->pDefaultMonitorModes[ModeIndex] = CreateIddCxMonitorMode(
                s_SampleDefaultModes[ModeIndex].Width,
                s_SampleDefaultModes[ModeIndex].Height,
                s_SampleDefaultModes[ModeIndex].VSync,
                IDDCX_MONITOR_MODE_ORIGIN_DRIVER
            );
        }

        pOutArgs->DefaultMonitorModeBufferOutputCount = ARRAYSIZE(s_SampleDefaultModes); 
        pOutArgs->PreferredMonitorModeIdx = 0;
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);

    vector<IDDCX_TARGET_MODE> TargetModes;

    // Create a set of modes supported for frame processing and scan-out. These are typically not based on the
    // monitor's descriptor and instead are based on the static processing capability of the device. The OS will
    // report the available set of modes for a given output as the intersection of monitor modes with target modes.

	DWORD SampleMonitorIdx = 0;
	for (SampleMonitorIdx=0; SampleMonitorIdx < ARRAYSIZE(s_SampleMonitors); SampleMonitorIdx++) {
		if (memcmp(pInArgs->MonitorDescription.pData, s_SampleMonitors[SampleMonitorIdx].pEdidBlock, IndirectSampleMonitor::szEdidBlock) == 0) {
			// Copy the known modes to the output buffer
			for (DWORD ModeIndex = 0; ModeIndex < IndirectSampleMonitor::szModeList; ModeIndex++) {
                if (SampleMonitorIdx < ARRAYSIZE(s_SampleMonitors)) {
                    TargetModes.push_back(CreateIddCxTargetMode(s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].Width, s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].Height, s_SampleMonitors[SampleMonitorIdx].pModeList[ModeIndex].VSync));
                }
            }
			pOutArgs->TargetModeBufferOutputCount = (UINT)TargetModes.size();
			if (pInArgs->TargetModeBufferInputCount >= TargetModes.size()) {
				copy(TargetModes.begin(), TargetModes.end(), pInArgs->pTargetModes);
			}
			return STATUS_SUCCESS;
		}
	}

	// This EDID block does not belong to the monitors we reported earlier
	return STATUS_INVALID_PARAMETER;
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    pMonitorContextWrapper->pContext->AssignSwapChain(pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS IddSampleMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
    auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
    pMonitorContextWrapper->pContext->UnassignSwapChain();
    return STATUS_SUCCESS;
}

#pragma endregion

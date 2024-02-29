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
#include "Trace.h"
#include "Driver.tmh"
#include "DVServercommon.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <Windows.h>
#include <winioctl.h>
#include <SetupAPI.h>
#include <initguid.h>

#pragma comment (lib, "Setupapi.lib")

using namespace std;
using namespace Microsoft::IndirectDisp;
using namespace Microsoft::WRL;

//DVServer KMD related
DeviceInfo* g_DevInfo;
UINT g_DevInfoCounter = 0;
BOOL g_init_kmd_resources = TRUE;
struct hp_info* g_hdata = NULL;
ULONG g_bytesReturned = 0;
HANDLE g_hpdthread_handle = NULL;

IDDCX_MONITOR g_monitorobject_list[MAX_SCAN_OUT] = { 0 };

#pragma region SampleMonitors

DWORD dvserver_monitor_count = 1;
IndirectSampleMonitor* g_monitors = NULL;

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

	Mode.pixelRate = ((UINT64)VSync) * ((UINT64)Width) * ((UINT64)Height);
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

EVT_WDF_DRIVER_DEVICE_ADD DVServerUMDDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY DVServerUMDDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT DVServerUMDDeviceD0Exit;
EVT_WDF_DRIVER_UNLOAD DVServerUMDUnload;

EVT_IDD_CX_ADAPTER_INIT_FINISHED DVServerUMDAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES DVServerUMDAdapterCommitModes;

EVT_IDD_CX_PARSE_MONITOR_DESCRIPTION DVServerUMDParseMonitorDescription;
EVT_IDD_CX_MONITOR_GET_DEFAULT_DESCRIPTION_MODES DVServerUMDMonitorGetDefaultModes;
EVT_IDD_CX_MONITOR_QUERY_TARGET_MODES DVServerUMDMonitorQueryModes;

EVT_IDD_CX_MONITOR_ASSIGN_SWAPCHAIN DVServerUMDMonitorAssignSwapChain;
EVT_IDD_CX_MONITOR_UNASSIGN_SWAPCHAIN DVServerUMDMonitorUnassignSwapChain;

WDFDEVICE g_dvserver_device = NULL;

struct IndirectDeviceContextWrapper
{
	IndirectDeviceContext* pContext;

	void Cleanup()
	{
		delete pContext;
		pContext = nullptr;

		if (g_DevInfo) {
			delete g_DevInfo;
			g_DevInfo = nullptr;
		}
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

	WPP_INIT_TRACING(pDriverObject, pRegistryPath);
	TRACING();

	WDF_OBJECT_ATTRIBUTES Attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);

	WDF_DRIVER_CONFIG_INIT(&Config,
		DVServerUMDDeviceAdd
	);
	Config.EvtDriverUnload = DVServerUMDUnload;
	Status = WdfDriverCreate(pDriverObject, pRegistryPath, &Attributes, &Config, WDF_NO_HANDLE);
	if (!NT_SUCCESS(Status))
	{
		WPP_CLEANUP(pDriverObject);
		return Status;
	}

	return Status;
}

/*******************************************************************************
*
* Description
*
* OnDriverUnload is called by the framework when the driver unloads.
*
* Parameters
*   Driver - Handle to a framework driver object created in DriverEntry.
*
* Return val
*   Null
*
******************************************************************************/
_Use_decl_annotations_
VOID DVServerUMDUnload(WDFDRIVER Driver)
{

	UNREFERENCED_PARAMETER(Driver);
	WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));

	return;
}
_Use_decl_annotations_
NTSTATUS DVServerUMDDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
	DWORD count = 0;
	NTSTATUS Status = STATUS_SUCCESS;
	WDF_PNPPOWER_EVENT_CALLBACKS PnpPowerCallbacks;

	UNREFERENCED_PARAMETER(Driver);
	TRACING();

	// Register for power callbacks
	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPowerCallbacks);
	PnpPowerCallbacks.EvtDeviceD0Entry = DVServerUMDDeviceD0Entry;
	PnpPowerCallbacks.EvtDeviceD0Exit = DVServerUMDDeviceD0Exit;
	WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &PnpPowerCallbacks);

	IDD_CX_CLIENT_CONFIG DVServerConfig;
	IDD_CX_CLIENT_CONFIG_INIT(&DVServerConfig);

	// If the driver wishes to handle custom IoDeviceControl requests, it's necessary to use this callback since IddCx
	// redirects IoDeviceControl requests to an internal queue. This sample does not need this.
	// DVServerConfig.EvtIddCxDeviceIoControl = DVServerUMDIoDeviceControl;

	DVServerConfig.EvtIddCxAdapterInitFinished = DVServerUMDAdapterInitFinished;

	DVServerConfig.EvtIddCxParseMonitorDescription = DVServerUMDParseMonitorDescription;
	DVServerConfig.EvtIddCxMonitorGetDefaultDescriptionModes = DVServerUMDMonitorGetDefaultModes;
	DVServerConfig.EvtIddCxMonitorQueryTargetModes = DVServerUMDMonitorQueryModes;
	DVServerConfig.EvtIddCxAdapterCommitModes = DVServerUMDAdapterCommitModes;
	DVServerConfig.EvtIddCxMonitorAssignSwapChain = DVServerUMDMonitorAssignSwapChain;
	DVServerConfig.EvtIddCxMonitorUnassignSwapChain = DVServerUMDMonitorUnassignSwapChain;

	Status = IddCxDeviceInitConfig(pDeviceInit, &DVServerConfig);
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
	g_dvserver_device = Device;

	// Create a new device context object and attach it to the WDF device object
	auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
	pContext->pContext = new IndirectDeviceContext(Device);

	g_DevInfo = new DeviceInfo();

	dvserver_monitor_count = get_total_screens(g_DevInfo->get_Handle());
	if ((dvserver_monitor_count == DVSERVERUMD_FAILURE) || \
		(dvserver_monitor_count > MAX_MONITOR_SUPPORTED)) {
		ERR("Failed to get total screens from KMD");
		return DVSERVERUMD_FAILURE;
	}

	try {
		g_monitors = new IndirectSampleMonitor[dvserver_monitor_count];
	} catch(const std::bad_alloc e){
		ERR("Dynamic memory allocation failure");
		return DVSERVERUMD_FAILURE;
	}
	for (count = 0; count < dvserver_monitor_count; count++) {
		if (get_edid_data(g_DevInfo->get_Handle(), &g_monitors[count], count) == DVSERVERUMD_FAILURE) {
			ERR("Failed to get EDID from QEMU");
			return DVSERVERUMD_FAILURE;
		}
	}

	return Status;
}

_Use_decl_annotations_
NTSTATUS DVServerUMDDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
	UNREFERENCED_PARAMETER(PreviousState);
	TRACING();

	// This function is called by WDF to start the device in the fully-on power state.

	auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(Device);
	pContext->pContext->InitAdapter(PreviousState);

	return STATUS_SUCCESS;
}

NTSTATUS DVServerUMDDeviceD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE TargetState)
{
	UNREFERENCED_PARAMETER(Device);
	DBGPRINT("TargetState = %d", TargetState);
	HANDLE hp_event = NULL;
	TRACING();

	hp_event = OpenEvent(EVENT_MODIFY_STATE, FALSE, HOTPLUG_TERMINATE_EVENT);
	if (hp_event == NULL) {
		ERR("Error opening named event. Error code: %d\n", GetLastError());
		return 1;
	}

	// Signal the HPD thread using the D3 callback to gracefully exit from the HPD thread
	int status = SetEvent(hp_event);
	if (status == NULL) {
		ERR(" Set HPevent failed with error [%d]\n ", GetLastError());
	}

	if (g_hpdthread_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(g_hpdthread_handle);
	}
	CloseHandle(hp_event);
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
	TRACING();
	// The DXGI factory could be cached, but if a new render adapter appears on the system, a new factory needs to be
	// created. If caching is desired, check DxgiFactory->IsCurrent() each time and recreate the factory if !IsCurrent.
	HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
	if (FAILED(hr))
	{
		ERR("CreateDXGIFactory2 failed\n");
		return hr;
	}

	// Find the specified render adapter
	hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
	if (FAILED(hr))
	{
		ERR("EnumAdapterByLuid failed\n");
		return hr;
	}

	// Create a D3D device using the render adapter. BGRA support is required by the WHQL test suite.
	hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &DeviceContext);
	if (FAILED(hr))
	{
		// If creating the D3D device failed, it's possible the render GPU was lost (e.g. detachable GPU) or else the
		// system is in a transient state.
		ERR("D3D11CreateDevice failed\n");
		return hr;
	}

	return S_OK;
}

#pragma endregion

#pragma region DeviceInfo

DeviceInfo::DeviceInfo()
{
	devHandle_frame = NULL;
	if (get_dvserver_kmdf_device() == DVSERVERUMD_FAILURE) {
		ERR("KMD resource Init Failed\n");
		return;
	}
	// ****** Frame Resources ******
	//Create Device frame Handle to DVServerKMD
	devHandle_frame = CreateFile(device_iface_data->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	if (devHandle_frame == INVALID_HANDLE_VALUE) {
		ERR("CreateFile for Frame returned INVALID_HANDLE_VALUE\n");
		return;
	}
}

DeviceInfo::~DeviceInfo()
{
	if (devHandle_frame != INVALID_HANDLE_VALUE) {
		CloseHandle(devHandle_frame);
		devHandle_frame = INVALID_HANDLE_VALUE;
	}

	if (device_iface_data) {
		free(device_iface_data);
		device_iface_data = NULL;
	}

	if (devinfo_handle != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(devinfo_handle);
		devinfo_handle = INVALID_HANDLE_VALUE;
	}
}

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
int DeviceInfo::get_dvserver_kmdf_device()
{
	DWORD sizeof_deviceinterface_buf = 0;
	BOOL ret;
	SP_DEVICE_INTERFACE_DATA device_interface_data;
	TRACING();

	device_interface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	devinfo_handle = SetupDiGetClassDevs(NULL, NULL, NULL, DEVINFO_FLAGS);

	ret = SetupDiEnumDeviceInterfaces(devinfo_handle, 0, &GUID_DEVINTERFACE_DVSERVERKMD, 0, &device_interface_data);
	if (ret == FALSE) {
		ERR("SetupDiEnumDeviceInterfaces failed\n");
		return DVSERVERUMD_FAILURE;
	}

	SetupDiGetDeviceInterfaceDetail(devinfo_handle, &device_interface_data, 0, 0, &sizeof_deviceinterface_buf, 0);
	if (sizeof_deviceinterface_buf == 0) {
		ERR("SetupDiGetDeviceInterfaceDetail failed\n");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(sizeof_deviceinterface_buf);
	if (device_iface_data == NULL) {
		ERR("Failed allocating memory for device interface data\n");
		return DVSERVERUMD_FAILURE;
	}

	device_iface_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
	ret = SetupDiGetDeviceInterfaceDetail(devinfo_handle, &device_interface_data, device_iface_data, sizeof_deviceinterface_buf, 0, 0);
	if (ret == FALSE) {
		ERR("etupDiGetDeviceInterfaceDetail Failed\nn");
		free(device_iface_data);
		device_iface_data = NULL;
		return DVSERVERUMD_FAILURE;
	}

	return DVSERVERUMD_SUCCESS;
}

#pragma endregion

#pragma region SwapChainProcessor

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent, UINT MonitorIndex)
	: m_hSwapChain(hSwapChain), m_Device(Device), m_hAvailableBufferEvent(NewFrameEvent), m_screen_num(0), print_counter(0)
{
	init();
	TRACING();

	m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));

	//Allocate memory for IOCTL Response Buffer from DVServerKMD (for frame)
	m_ioctlresp_frame = (struct KMDF_IOCTL_Response*)malloc(sizeof(struct KMDF_IOCTL_Response));
	if (m_ioctlresp_frame == NULL) {
		ERR("Failed allocating IOCTL Response structure (for frame)\n");
		goto exit;
	}

	//Allocate memory for FrameMeta Data pointer
	m_framedata = (struct FrameMetaData*)malloc(sizeof(struct FrameMetaData));
	if (m_framedata == NULL) {
		ERR("Failed allocating frame metadata structure\n");
		goto exit;
	}

	m_GPUResourceMutex = CreateMutex(NULL, FALSE, NULL);
	if (m_GPUResourceMutex == NULL) {
		ERR("GPU CreateMutex Failed\n");
		goto exit;
	}

	m_screen_num = MonitorIndex;
	DBGPRINT("screen num = %d\n", m_screen_num);

	// Immediately create and run the swap-chain processing thread, passing 'this' as the thread parameter
	m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));

#ifdef DVSERVER_HWDCURSOR
	// ****** Cursor Resources ******
	//Create Device cursor Handle to DVServerKMD
	devHandle_cursor = CreateFile(device_iface_data->DevicePath, 0, 0, NULL, OPEN_EXISTING, 0, 0);
	if (devHandle_cursor == INVALID_HANDLE_VALUE) {
		ERR("CreateFile for cursor returned INVALID_HANDLE_VALUE\n");
		goto exit;
	}

	//Allocate memory for IOCTL Response Buffer from DVServerKMD (for cursor)
	m_ioctlresp_cursor = (struct KMDF_IOCTL_Response*)malloc(sizeof(struct KMDF_IOCTL_Response));
	if (m_ioctlresp_cursor == NULL) {
		ERR("Failed allocating IOCTL Response structure (for cursor) !!!\n");
		goto exit;
	}

	//Allocate memory for Cursor Data pointer
	m_cursordata = (struct CursorData*)malloc(sizeof(struct CursorData));
	if (m_cursordata == NULL) {
		ERR("Failed allocating cursor structure !!!\n");
		goto exit;
	}

	//Create DVServer Cursor Thread
	cursorthread_handle = CreateThread(nullptr, 0, CursorThread, this, 0, nullptr);
	if (cursorthread_handle == INVALID_HANDLE_VALUE) {
		ERR("Failed creating cursor thread !!!\n");
		goto exit;
	}
#endif //end of DVSERVER_HWDCURSOR

	DBGPRINT("Init Resources Successful!!!\n");
	return;

exit:
	g_init_kmd_resources = FALSE;
	cleanup_resources();
}

SwapChainProcessor::~SwapChainProcessor()
{
	TRACING();
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
	TRACING();
	// ****** Frame Resources ******
	if (m_framedata != INVALID_HANDLE_VALUE)
		free(m_framedata);

	if (m_ioctlresp_frame != INVALID_HANDLE_VALUE)
		free(m_ioctlresp_frame);

	if (m_GPUResourceMutex != INVALID_HANDLE_VALUE) {
		ReleaseMutex(m_GPUResourceMutex);
		CloseHandle(m_GPUResourceMutex);
	}

	if ((m_destimage != NULL) && (m_Device != NULL)) {
		m_Device->DeviceContext->Unmap(m_destimage, 0);
		m_destimage->Release();
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
		ERR("Failed to set the thread priority to THREAD_PRIORITY_HIGHEST\n");
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
	TRACING();
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
	char err[256];
	memset(err, 0, 256);
	TRACING();

	HRESULT hr = m_Device->Device.As(&DxgiDevice);
	if (FAILED(hr))
	{
		ERR("Device.As failed\n");
		return;
	}

	IDARG_IN_SWAPCHAINSETDEVICE SetDevice = {};
	SetDevice.pDevice = DxgiDevice.Get();

	hr = IddCxSwapChainSetDevice(m_hSwapChain, &SetDevice);
	if (FAILED(hr))
	{
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
		ERR("IddCxSwapChainSetDevice failed, screen = %d, err = %s\n", m_screen_num, err);
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
			HANDLE WaitHandles[] =
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
				ERR("WaitResult == WAIT_OBJECT_0 + 1\n");
				break;
			}
			else
			{
				// The wait was cancelled or something unexpected happened
				hr = HRESULT_FROM_WIN32(WaitResult);
				FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr,
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
				ERR("Wait was either cancelled or something unexpected happened, screen = %d, err = %s\n", m_screen_num, err);
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
					ERR("Failed QueryInterface %d\n", status);
					AcquiredBuffer.Reset();
					break;
				}

				//Get Frame	from GPU
				if (GetFrameData(m_Device, m_IAcquiredDesktopImage) == DVSERVERUMD_FAILURE) {
					ERR("Failed getting frame from GPU\n");
					AcquiredBuffer.Reset();
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
					FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr,
						MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
					ERR("IddCxSwapChainFinishedProcessingFrame Failed, screen = %d, err = %s\n", m_screen_num, err);
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
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, hr,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("The swap-chain was likely abandoned (e.g. DXGI_ERROR_ACCESS_LOST), so exiting, screen = %d, err = %s\n", m_screen_num, err);
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
* dvserver_device - shared_ptr to  Direct3D Device (Direct3D render device)
* desktopimage - ptr to ID3D11Texture2D  (A 2D texture interface manages
* texel data). Uses method struct D3D11_TEXTURE2D_DESC to get the properties
* of texture resource
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int SwapChainProcessor::GetFrameData(std::shared_ptr<Direct3DDevice> dvserver_device, ID3D11Texture2D* desktopimage)
{
	HRESULT status;
	char err[256];
	memset(err, 0, 256);

	if (desktopimage == INVALID_HANDLE_VALUE) {
		ERR("desktopimage pointer is NULL\n");
		return DVSERVERUMD_FAILURE;
	}

	if (m_resolution_changed == TRUE) {
		DBGPRINT("ResolutionChanged, setting up new staging buffer\n");
		ZeroMemory(&m_staging_buffer, sizeof(D3D11_MAPPED_SUBRESOURCE));
		ZeroMemory(&m_staging_desc, sizeof(m_staging_desc));
		ZeroMemory(&m_input_desc, sizeof(m_input_desc));

		desktopimage->GetDesc(&m_input_desc);
		m_width = m_input_desc.Width;
		m_height = m_input_desc.Height;

		switch (m_input_desc.Format) {
		case DXGI_FORMAT_B8G8R8A8_UNORM: m_format = FRAME_TYPE_BGRA; break;
		case DXGI_FORMAT_R8G8B8A8_UNORM: m_format = FRAME_TYPE_RGBA; break;
		case DXGI_FORMAT_R10G10B10A2_UNORM: m_format = FRAME_TYPE_RGBA10; break;
		default:
			ERR("Unsupported source format\n");
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

		DBGPRINT("New mode info: %dx%d\n", m_staging_desc.Width, m_staging_desc.Height);

		/* Check if all the parameters in the staging descriptor is proper or not */
		if (dvserver_device->Device->CreateTexture2D(&m_staging_desc, NULL, NULL) != S_FALSE) {
			ERR("Failed Staging Buffer invalid configurations\n");
			return DVSERVERUMD_FAILURE;
		}

		/* Create Texture2D with the staging descriptor parameters */
		dvserver_device->Device->CreateTexture2D(&m_staging_desc, NULL, &m_destimage);
		if (m_destimage == NULL) {
			ERR("Failed Staging Buffer CreateTexture2D is NULL\n");
			return DVSERVERUMD_FAILURE;
		}
	}

	WaitForSingleObject(m_GPUResourceMutex, INFINITE);
	dvserver_device->DeviceContext->CopyResource((ID3D11Resource*)m_destimage, (ID3D11Resource*)desktopimage);
	desktopimage->Release();
	status = dvserver_device->DeviceContext->Map((ID3D11Resource*)m_destimage, 0, D3D11_MAP_READ, 0, &m_staging_buffer);
	if (FAILED(status)) {
		ERR("Failed to Map the resource dvserver_device->DeviceContext->Map\n");
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
	m_framedata->screen_num = m_screen_num;
	//Assigning the m_staging_buffer buffer addr
	m_framedata->addr = (void*)m_staging_buffer.pData;

	if (!(print_counter++ % PRINT_FREQ)) {
		DBGPRINT("m_framedata->width = %d\n", m_framedata->width);
		DBGPRINT("m_framedata->height = %d\n", m_framedata->height);
		DBGPRINT("m_framedata->format = %d\n", m_framedata->format);
		DBGPRINT("m_framedata->pitch = %d\n", m_framedata->pitch);
		DBGPRINT("m_framedata->stride = %d\n", m_framedata->stride);
		DBGPRINT("m_framedata->bitrate = %d\n", m_framedata->bitrate);
		DBGPRINT("m_framedata->screen_num = %d\n", m_framedata->screen_num);
		DBGPRINT("m_framedata->addr = %p\n", m_framedata->addr);
	}

	if (m_resolution_changed == TRUE) {
		DBGPRINT("ResolutionChanged - sending SET MODE IOCTL\n");
		//m_framedata->refresh_rate = FRAME_RR;
		if (!DeviceIoControl(g_DevInfo->get_Handle(), IOCTL_DVSERVER_SET_MODE, \
			m_framedata, \
			sizeof(struct FrameMetaData), m_ioctlresp_frame, \
			sizeof(struct KMDF_IOCTL_Response), \
			& m_ioctlresp_size, NULL)) {
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
			ERR("IOCTL_DVSERVER_SET_MODE call failed with error: %s!\n", err);
			dvserver_device->DeviceContext->Unmap(m_destimage, 0);
			m_destimage->Release();
			return DVSERVERUMD_FAILURE;
		}
		m_resolution_changed = FALSE;
	}

	if (!DeviceIoControl(g_DevInfo->get_Handle(), IOCTL_DVSERVER_FRAME_DATA, \
		m_framedata, sizeof(struct FrameMetaData), \
		m_ioctlresp_frame, sizeof(struct KMDF_IOCTL_Response), \
		& m_ioctlresp_size, NULL)) {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
		ERR("IOCTL_DVSERVER_FRAME_DATA call failed with error: %s!\n", err);
		dvserver_device->DeviceContext->Unmap(m_destimage, 0);
		m_destimage->Release();
		return DVSERVERUMD_FAILURE;
	}

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
					ERR("Failed IddCxMonitorQueryHardwareCursor\n");
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
						& m_ioctlresp_size, NULL)) {
						ERR("IOCTL_DVSERVER_CURSOR_DATA call failed!\n");
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

void IndirectDeviceContext::InitAdapter(WDF_POWER_DEVICE_STATE PreviousState)
{
	// ==============================
	// TODO: Update the below diagnostic information in accordance with the target hardware. The strings and version
	// numbers are used for telemetry and may be displayed to the user in some situations.
	//
	// This is also where static per-adapter capabilities are determined.
	// ==============================

	TRACING();
	DBGPRINT("System entry happens from state %d", PreviousState);

	if (PreviousState != WdfPowerDeviceD3) {
		IDDCX_ADAPTER_CAPS AdapterCaps = {};
		AdapterCaps.Size = sizeof(AdapterCaps);

		//This flag enables IDD Display to change the resolution
		AdapterCaps.Flags = IDDCX_ADAPTER_FLAGS_USE_SMALLEST_MODE;

		// Declare basic feature support for the adapter (required)
		AdapterCaps.MaxMonitorsSupported = dvserver_monitor_count;
		AdapterCaps.EndPointDiagnostics.Size = sizeof(AdapterCaps.EndPointDiagnostics);
		AdapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
		AdapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;

		// Declare your device strings for telemetry (required)
		AdapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"DVServerUMD Device";
		AdapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"Intel";
		AdapterCaps.EndPointDiagnostics.pEndPointModelName = L"DVServerUMD Model";

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

		if (NT_SUCCESS(Status)) {
			// Store a reference to the WDF adapter handle
			m_Adapter = AdapterInitOut.AdapterObject;

			// Store the device context object into the WDF object context
			auto* pContext = WdfObjectGet_IndirectDeviceContextWrapper(AdapterInitOut.AdapterObject);
			pContext->pContext = this;
		}
	}
	else {
		// Create the HPD thread if the system boots from WdfPowerDeviceD3
		g_hpdthread_handle = CreateThread(nullptr, 0, IndirectDeviceContext::HPDThread, m_Adapter, 0, nullptr);
		if (g_hpdthread_handle == NULL) {
			ERR("Failed to create HPD Thread");
		}

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

	TRACING();
	WDF_OBJECT_ATTRIBUTES Attr;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attr, IndirectMonitorContextWrapper);

	// In the sample driver, we report a monitor right away but a real driver would do this when a monitor connection event occurs
	IDDCX_MONITOR_INFO MonitorInfo = {};
	MonitorInfo.Size = sizeof(MonitorInfo);
	MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED;//DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
	MonitorInfo.ConnectorIndex = ConnectorIndex;

	MonitorInfo.MonitorDescription.Size = sizeof(MonitorInfo.MonitorDescription);
	MonitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
	if (ConnectorIndex >= dvserver_monitor_count)
	{
		MonitorInfo.MonitorDescription.DataSize = 0;
		MonitorInfo.MonitorDescription.pData = nullptr;
	}
	else
	{
		MonitorInfo.MonitorDescription.DataSize = IndirectSampleMonitor::szEdidBlock;
		MonitorInfo.MonitorDescription.pData = const_cast<BYTE*>(g_monitors[ConnectorIndex].pEdidBlock);
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
		pMonitorContextWrapper->pContext = new IndirectMonitorContext(MonitorCreateOut.MonitorObject, ConnectorIndex);

		// Tell the OS that the monitor has been plugged in
		IDARG_OUT_MONITORARRIVAL ArrivalOut;
		Status = IddCxMonitorArrival(MonitorCreateOut.MonitorObject, &ArrivalOut);
		g_monitorobject_list[ConnectorIndex] = MonitorCreateOut.MonitorObject;
	}
}

/*******************************************************************************
*
* Description
*
* HPDThread - In This function we are creating a new thread for HOT PLUG feature
* where we will pass the display addapter, which is created by OS to add and
* remove the monitors in runtime
*
* Parameters
* IDDCX_ADAPTER - Display adapter from OS to add the additional monitor
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
DWORD CALLBACK IndirectDeviceContext::HPDThread(LPVOID Argument)
{
	TRACING();

	IDDCX_ADAPTER AdapterObject = (IDDCX_ADAPTER)Argument;

	if (hpd_event_create(AdapterObject) == DVSERVERUMD_FAILURE) {
		ERR("Failed to create HPD Event");
	}

	return 0;
}

IndirectMonitorContext::IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor, _In_ UINT ConnectorIndex) :
	m_Monitor(Monitor), m_MonitorIndex(ConnectorIndex)
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
		ERR("Cursor Event Create Failed\n");
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
				ERR("Failed allocating cursor buffer\n");
				g_init_kmd_resources = FALSE;
				return;
			}
			DBGPRINT("Hardware cursor setup success\n");
		}
		else {
			ERR("Hardware cursor setup failed\n");
			g_init_kmd_resources = FALSE;
		}
	}
}
#endif

void IndirectMonitorContext::AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
	TRACING();
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
		m_ProcessingThread.reset(new SwapChainProcessor(SwapChain, Device, NewFrameEvent, m_MonitorIndex));
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
NTSTATUS DVServerUMDAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
	// This is called when the OS has finished setting up the adapter for use by the IddCx driver. It's now possible
	// to report attached monitors.
	if (NT_SUCCESS(pInArgs->AdapterInitStatus)) {
		g_hpdthread_handle = CreateThread(nullptr, 0, IndirectDeviceContext::HPDThread, AdapterObject, 0, nullptr);
		if (g_hpdthread_handle == NULL) {
			ERR("Failed to Create HPD thread");
			return STATUS_UNSUCCESSFUL;
		}
	}

	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DVServerUMDAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
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
NTSTATUS DVServerUMDParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
	// ==============================
	// TODO: In a real driver, this function would be called to generate monitor modes for an EDID by parsing it. In
	// this sample driver, we hard-code the EDID, so this function can generate known modes.
	// ==============================
	TRACING();

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

		for (DWORD SampleMonitorIdx = 0; SampleMonitorIdx < dvserver_monitor_count; SampleMonitorIdx++)
		{
			if (memcmp(pInArgs->MonitorDescription.pData, g_monitors[SampleMonitorIdx].pEdidBlock, IndirectSampleMonitor::szEdidBlock) == 0)
			{
				// Copy the known modes to the output buffer
				for (DWORD ModeIndex = 0; ModeIndex < IndirectSampleMonitor::szModeList; ModeIndex++)
				{
					pInArgs->pMonitorModes[ModeIndex] = CreateIddCxMonitorMode(
						g_monitors[SampleMonitorIdx].pModeList[ModeIndex].Width,
						g_monitors[SampleMonitorIdx].pModeList[ModeIndex].Height,
						g_monitors[SampleMonitorIdx].pModeList[ModeIndex].VSync,
						IDDCX_MONITOR_MODE_ORIGIN_MONITORDESCRIPTOR
					);
				}

				// Set the preferred mode as represented in the EDID
				pOutArgs->PreferredMonitorModeIdx = g_monitors[SampleMonitorIdx].ulPreferredModeIdx;
				return STATUS_SUCCESS;
			}
		}

		// This EDID block does not belong to the monitors we reported earlier
		return STATUS_INVALID_PARAMETER;
	}
}

_Use_decl_annotations_
NTSTATUS DVServerUMDMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
	UNREFERENCED_PARAMETER(MonitorObject);
	TRACING();

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
NTSTATUS DVServerUMDMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
	UNREFERENCED_PARAMETER(MonitorObject);
	TRACING();

	vector<IDDCX_TARGET_MODE> TargetModes;

	// Create a set of modes supported for frame processing and scan-out. These are typically not based on the
	// monitor's descriptor and instead are based on the static processing capability of the device. The OS will
	// report the available set of modes for a given output as the intersection of monitor modes with target modes.

	DWORD SampleMonitorIdx = 0;
	for (SampleMonitorIdx = 0; SampleMonitorIdx < dvserver_monitor_count; SampleMonitorIdx++) {
		if (memcmp(pInArgs->MonitorDescription.pData, g_monitors[SampleMonitorIdx].pEdidBlock, IndirectSampleMonitor::szEdidBlock) == 0) {
			// Copy the known modes to the output buffer
			for (DWORD ModeIndex = 0; ModeIndex < IndirectSampleMonitor::szModeList; ModeIndex++) {
				if (SampleMonitorIdx < dvserver_monitor_count) {
					TargetModes.push_back(CreateIddCxTargetMode(g_monitors[SampleMonitorIdx].pModeList[ModeIndex].Width,
						g_monitors[SampleMonitorIdx].pModeList[ModeIndex].Height,
						g_monitors[SampleMonitorIdx].pModeList[ModeIndex].VSync));
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
NTSTATUS DVServerUMDMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
	auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
	pMonitorContextWrapper->pContext->AssignSwapChain(pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DVServerUMDMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
	auto* pMonitorContextWrapper = WdfObjectGet_IndirectMonitorContextWrapper(MonitorObject);
	pMonitorContextWrapper->pContext->UnassignSwapChain();
	return STATUS_SUCCESS;
}


/*******************************************************************************
*
* Description
*
* hpd_event_create - This function creates an HOTPLUG event. Passes the
* event handle to KMD and gets the current display status. Then compares
* the perivous display state with current display state and calls  FinishInit/
* IddCxMonitorDeparture
*
* Parameters
* IDDCX_ADAPTER -- Display adapter from OS to add the additional monitor
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int hpd_event_create(IDDCX_ADAPTER AdapterObject)
{
	TRACING();

	HANDLE hp_event = NULL;
	HANDLE dve_event = NULL;
	HANDLE hp_terminate_event = NULL;
	DWORD waitstatus;
	bool do_set_event = FALSE;
	int status;
	int count;
	disp_info dinfo = { 0 };
	hp_info hdata = { 0 };
	static bool monitor_status[MAX_SCAN_OUT] = { 0 };
	HANDLE devHandle = g_DevInfo->get_Handle();
	auto* pDeviceContextWrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);

	//Create Security Descriptor for HOTPLUG_EVENT, To allow the user application(Dvenabler) to access the event
	PSECURITY_DESCRIPTOR hp_psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(hp_psd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(hp_psd, TRUE, NULL, FALSE);

	SECURITY_ATTRIBUTES hp_sa = { 0 };
	hp_sa.nLength = sizeof(hp_sa);
	hp_sa.lpSecurityDescriptor = hp_psd;
	hp_sa.bInheritHandle = FALSE;

	hp_event = CreateEvent(&hp_sa, FALSE, FALSE, HOTPLUG_EVENT);
	if (NULL == hp_event) {
		ERR("Cannot create HOTPULG event!\n");
		return DVSERVERUMD_FAILURE;
	}
	hdata.event = hp_event;

	hp_terminate_event = CreateEvent(&hp_sa, FALSE, FALSE, HOTPLUG_TERMINATE_EVENT);
	if (NULL == hp_terminate_event) {
		ERR("Cannot create HOTPULG event!\n");
		CloseHandle(hp_event);
		return DVSERVERUMD_FAILURE;
	}

	//Create Security Descriptor for DVE_EVENT, To allow the user application(DVenabler) to access the event
	PSECURITY_DESCRIPTOR dve_psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	InitializeSecurityDescriptor(dve_psd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(dve_psd, TRUE, NULL, FALSE);

	SECURITY_ATTRIBUTES dve_sa = { 0 };
	dve_sa.nLength = sizeof(dve_sa);
	dve_sa.lpSecurityDescriptor = dve_psd;
	dve_sa.bInheritHandle = FALSE;

	dve_event = CreateEvent(&dve_sa, FALSE, FALSE, DVE_EVENT);
	if (NULL == dve_event) {
		ERR("Cannot create DVE event!\n");
		CloseHandle(hp_event);
		CloseHandle(hp_terminate_event);
		return DVSERVERUMD_FAILURE;
	}

	SECURITY_ATTRIBUTES secAttr;
	SECURITY_DESCRIPTOR secDesc;
	// Create a security descriptor with desired permissions
	InitializeSecurityDescriptor(&secDesc, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&secDesc, TRUE, NULL, FALSE);
	secAttr.nLength = sizeof(secAttr);
	secAttr.bInheritHandle = FALSE;
	secAttr.lpSecurityDescriptor = &secDesc;

	// Create the shared memory section
	HANDLE hSharedMem = CreateFileMapping(INVALID_HANDLE_VALUE, &secAttr, PAGE_READWRITE, 0, sizeof(struct disp_info), DISP_INFO);

	if (hSharedMem == NULL) {
		ERR("Failed to create shared memory section (%d)\n", GetLastError());
		CloseHandle(dve_event);
		CloseHandle(hp_event);
		CloseHandle(hp_terminate_event);
		return DVSERVERUMD_FAILURE;
	}

	ERR("Shared memory section created successfully\n");

	struct disp_info* pSharedMem = (struct disp_info*)MapViewOfFile(
		hSharedMem,          // Handle to the shared memory section
		FILE_MAP_ALL_ACCESS, // Read/write access
		0,                   // File offset - high-order DWORD
		0,                   // File offset - low-order DWORD
		0);                  // Mapping size (0 means to map the entire section)

	if (pSharedMem == NULL) {
		ERR(L"Failed to map view of shared memory section (%d)\n", GetLastError());
		CloseHandle(hp_event);
		CloseHandle(hp_terminate_event);
		CloseHandle(dve_event);
		CloseHandle(hSharedMem);
		return DVSERVERUMD_FAILURE;
	}

	pSharedMem->mutex = CreateMutex(&secAttr, FALSE, NULL);
	if (pSharedMem->mutex == NULL) {
		ERR(L"Failed to create mutex for shared memeory (%d)\n", GetLastError());
		UnmapViewOfFile(pSharedMem);
		CloseHandle(hSharedMem);
		CloseHandle(hp_event);
		CloseHandle(hp_terminate_event);
		CloseHandle(dve_event);
		return DVSERVERUMD_FAILURE;
	}

	pDeviceContextWrapper->pContext->FinishInit(PRIMARY_IDD_INDEX);

	// Default IDD monitor will be enabled at this time. so setting disp_count to 1.
	WaitForSingleObject(pSharedMem->mutex, INFINITE);
	pSharedMem->disp_count = 1;
	ReleaseMutex(pSharedMem->mutex);

	// Doing this set event to avoid dead lock during UMD driver reset.
	status = SetEvent(dve_event);
	if (status == NULL) {
		ERR("Set dve-event failed during first display arrival with error [%d]\n ", GetLastError());
	}

	HANDLE hp_handles[] = { hp_event , hp_terminate_event };

	while (1) {
		//After user login, first time DVenabler will set this event to enable the HPD Path.
		//KMD will set this for every HP interrupt recieved from QEMU.
		waitstatus = WaitForMultipleObjects(ARRAYSIZE(hp_handles), hp_handles, FALSE, INFINITE);
		if (waitstatus == WAIT_OBJECT_0) {
			hdata.screen_present[3] = { 0 };
			if (get_hpd_data(devHandle, &hdata) != DVSERVERUMD_SUCCESS) {
				ERR("HotPlug resource allocation failed... Going back to the loop again");
				continue;
			}

			//call display arrival and departure based on previous and current display state.
			for (count = 0; count < MAX_SCAN_OUT; count++) {
				if (monitor_status[count] != hdata.screen_present[count]) {
					monitor_status[count] = hdata.screen_present[count];
					if ((hdata.screen_present[count] == 0) && (g_monitorobject_list[count] != NULL)) {
						DBGPRINT("call depature for DISPLAY = %d\n", count);
						IddCxMonitorDeparture(g_monitorobject_list[count]);
						g_monitorobject_list[count] = NULL;
						dinfo.disp_count--;
					}
					else {
						DBGPRINT("call finishinit for DISPLAY = %d\n", count);
						pDeviceContextWrapper->pContext->FinishInit(count);
						dinfo.disp_count++;
					}
					do_set_event = TRUE;
				}
				else {
					DBGPRINT("No changes in DISPLAY = %d\n", count);
				}
			}

			if ((do_set_event)) {
				ERR("disp_count = %d", dinfo.disp_count);
				WaitForSingleObject(pSharedMem->mutex, INFINITE);
				pSharedMem->disp_count = dinfo.disp_count;
				ReleaseMutex(pSharedMem->mutex);
				status = SetEvent(dve_event);
				if (status == NULL) {
					ERR("Set dve-event failed during Display Arrival/departure with error [%d]\n ", GetLastError());
				}
			}
			do_set_event = FALSE;

		}
		else if (waitstatus == WAIT_OBJECT_0 + 1) {
			DBGPRINT("UMD is entering D3 state so kill the HPD thread");
			break;
		}
		else {
			ERR("HPD Wait was either cancelled or something unexpected happened");
			break;
		}
	}

	for (count = 0; count < MAX_SCAN_OUT; count++) {
		if (hdata.screen_present[count]) {
			DBGPRINT("Call departure for display %d from exit", count);
			monitor_status[count] = FALSE;
			IddCxMonitorDeparture(g_monitorobject_list[count]);
			g_monitorobject_list[count] = NULL;
		}
	}

	UnmapViewOfFile(pSharedMem);
	CloseHandle(hSharedMem);
	CloseHandle(hp_event);
	CloseHandle(dve_event);
	CloseHandle(hp_terminate_event);
	return DVSERVERUMD_SUCCESS;
}

/*******************************************************************************
*
* Description
*
* get_hpd_data - whenever the HPEVENT is set, this function sends an
* ioctl(IOCTL_DVSERVER_HP_EVENT) to DVserverKMD to get the current
* display status which is recieved from QEMU
*
* Parameters
* devHandle - device frame Handle to DVServerKMD
* data - pointer to hp_info structure
*
* Return val
* int - 0 == SUCCESS, -1 = ERROR
*
******************************************************************************/
int get_hpd_data(HANDLE devHandle, hp_info* data)
{
	TRACING();

	int i;

	g_hdata = (struct hp_info*)malloc(sizeof(struct hp_info));
	if (g_hdata == NULL) {
		ERR("Failed to allocate HPD structure\n");
		return DVSERVERUMD_FAILURE;
	}

	SecureZeroMemory(g_hdata, sizeof(struct hp_info));
	g_hdata->event = data->event;

	if (!DeviceIoControl(devHandle, IOCTL_DVSERVER_HP_EVENT, g_hdata, sizeof(struct hp_info), g_hdata, sizeof(struct hp_info), &g_bytesReturned, NULL)) {
		ERR("IOCTL_DVSERVER_HPD_EVENT  call failed\n");
		free(g_hdata);
		return DVSERVERUMD_FAILURE;
	}

	for (i = 0; i < MAX_SCAN_OUT; i++) {
		data->screen_present[i] = g_hdata->screen_present[i];
	}

	free(g_hdata);
	return DVSERVERUMD_SUCCESS;
}

#pragma endregion

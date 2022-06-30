#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <iddcx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <memory>
#include <vector>

#include "Trace.h"
#include "DVServeredid.h"
#include "..\..\DVServerKMD\Public.h"

DEFINE_GUID(GUID_DEVINTERFACE_DVSERVERKMD,
	0x1c514918, 0xa855, 0x460a, 0x97, 0xda, 0xed, 0x69, 0x1d, 0xd5, 0x63, 0xcf);

//#define DVSERVER_HWDCURSOR

#define DVSERVERUMD_COLORFORMAT			21 // D3DDDIFMT_A8R8G8B8
#define DVSERVER_BBP					4  // 4 Bytes per pixel
#define DEVINFO_FLAGS					DIGCF_PRESENT | DIGCF_ALLCLASSES | DIGCF_DEVICEINTERFACE
#define REPORT_FRAME_STATS				60 // we need to report frame stats to OS for every 60 frames

typedef enum FrameType
{
	FRAME_TYPE_INVALID,
	FRAME_TYPE_BGRA, // BGRA interleaved: B,G,R,A 32bpp
	FRAME_TYPE_RGBA, // RGBA interleaved: R,G,B,A 32bpp
	FRAME_TYPE_RGBA10, // RGBA interleaved: R,G,B,A 10,10,10,2 bpp
	FRAME_TYPE_YUV420, // YUV420
	FRAME_TYPE_MAX, // sentinel value
}
FrameType;

#ifdef DVSERVER_HWDCURSOR
typedef struct CursorData
{
	INT	cursor_x;
	INT	cursor_y;
	UINT16	iscursorvisible;
	UINT32	cursor_version;
	UINT16	cursor_type;
	UINT32  width;
	UINT32  height;
	UINT32  pitch;
	void* data;
}CursorData;
#endif //end of DVSERVER_HWDCURSOR


namespace Microsoft
{
    namespace WRL
    {
        namespace Wrappers
        {
            // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace Microsoft
{
    namespace IndirectDisp
    {
        /// <summary>
        /// Manages the creation and lifetime of a Direct3D render device.
        /// </summary>
        struct IndirectSampleMonitor
        {
            static constexpr size_t szEdidBlock = 256;
            static constexpr size_t szModeList = 32;

            BYTE pEdidBlock[szEdidBlock];
            struct SampleMonitorMode {
                DWORD Width;
                DWORD Height;
                DWORD VSync;
            } pModeList[szModeList];
            const DWORD ulPreferredModeIdx;
        };

        /// <summary>
        /// Manages the creation and lifetime of a Direct3D render device.
        /// </summary>
        struct Direct3DDevice
        {
            Direct3DDevice(LUID AdapterLuid);
            Direct3DDevice();
            HRESULT Init();

            LUID AdapterLuid;
            Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
            Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
            Microsoft::WRL::ComPtr<ID3D11Device> Device;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
        };

        /// <summary>
        /// Manages a thread that consumes buffers from an indirect display swap-chain object.
        /// </summary>
        class SwapChainProcessor
        {
        public:
            SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent);
            ~SwapChainProcessor();
			int	 GetFrameData(std::shared_ptr<Direct3DDevice> idd_device, ID3D11Texture2D* desktopimage);
			int get_dvserver_kmdf_device();
			void cleanup_resources();
			void report_frame_statistics(IDARG_OUT_RELEASEANDACQUIREBUFFER Buffer);
			void init();

        private:
            static DWORD CALLBACK RunThread(LPVOID Argument);
            void Run();
            void RunCore();

            IDDCX_SWAPCHAIN m_hSwapChain;
            std::shared_ptr<Direct3DDevice> m_Device;
            HANDLE m_hAvailableBufferEvent;
            Microsoft::WRL::Wrappers::Thread m_hThread;
            Microsoft::WRL::Wrappers::Event m_hTerminateEvent;

			//FrameMetaData related 
			ID3D11Texture2D* m_destimage;
			ID3D11Texture2D* m_IAcquiredDesktopImage;
			D3D11_MAPPED_SUBRESOURCE m_staging_buffer;
			D3D11_TEXTURE2D_DESC m_input_desc, m_staging_desc;
			uint32_t m_width, m_height, m_pitch, m_stride;
			FrameType m_format;
			HANDLE m_GPUResourceMutex;
			uint32_t m_frame_statistics_counter;
			BOOL m_resolution_changed;

			//IOCTL related buffers
			ULONG m_ioctlresp_size;
			struct FrameMetaData* m_framedata;
			struct KMDF_IOCTL_Response* m_ioctlresp_frame;

#ifdef DVSERVER_HWDCURSOR
			//Cursor related
			struct CursorData* m_cursordata;
			struct KMDF_IOCTL_Response* m_ioctlresp_cursor;
			IDARG_OUT_QUERY_HWCURSOR m_outputargs;
			HANDLE cursorthread_handle;

			void GetCursorData();
			static DWORD CALLBACK CursorThread(LPVOID Argument);

#endif //end of DVSERVER_HWDCURSOR
        };

        /// <summary>
        /// Provides a sample implementation of an indirect display driver.
        /// </summary>
        class IndirectDeviceContext
        {
        public:
            IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
            virtual ~IndirectDeviceContext();

            void InitAdapter();
            void FinishInit(UINT ConnectorIndex);

        protected:
            WDFDEVICE m_WdfDevice;
            IDDCX_ADAPTER m_Adapter;
        };

        class IndirectMonitorContext
        {
        public:
            IndirectMonitorContext(_In_ IDDCX_MONITOR Monitor);
            virtual ~IndirectMonitorContext();

            void AssignSwapChain(IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
            void UnassignSwapChain();

#ifdef DVSERVER_HWDCURSOR
			void SetupIDDCursor(IDDCX_PATH* pPath);
#endif //end of DVSERVER_HWDCURSOR

        private:
            IDDCX_MONITOR m_Monitor;
            std::unique_ptr<SwapChainProcessor> m_ProcessingThread;
        } ;
    }
}

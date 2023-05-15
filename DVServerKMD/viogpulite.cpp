/*
 * Copyright (C) 2021 Intel Corporation
 * Copyright (C) 2019-2020 Red Hat, Inc.
 *
 * Written By: Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met :
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and / or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of their contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "helper.h"
#include "driver.h"
#include "viogpulite.h"
#include "baseobj.h"
#include "bitops.h"
#include "qemu_edid.h"
#include "Trace.h"
#include <viogpulite.tmh>

extern "C" {
#include "..\EDIDParser\edidshared.h"
}


#if !DBG
#include "viogpulite.tmh"
#endif

/*
 * Following extern is for edid parser lib to use floating point ops.
 * Define _fltused, since we're not linking against the MS C runtime, but use floats.
 */
extern "C" int _fltused = 0;

static UINT g_InstanceId = 0;

PAGED_CODE_SEG_BEGIN

ScreenInfo::ScreenInfo()
{
	PAGED_CODE();
	TRACING();

	m_ModeInfo = NULL;
	m_ModeCount = 0;
	m_ModeNumbers = NULL;
	m_CurrentMode = 0;
	m_CustomMode = 0;
	mode_list.modelist_size = 0;
	m_pFrameBuf = NULL;
	m_FlushCount = 0;
	enabled = FALSE;
}

ScreenInfo::~ScreenInfo()
{
	Reset();
	m_CurrentMode = 0;
	m_CustomMode = 0;
	m_ModeCount = 0;
}

void ScreenInfo::Reset()
{
	if (m_ModeInfo) {
		delete[] m_ModeInfo;
		m_ModeInfo = NULL;
	}
	if (m_ModeNumbers) {
		delete[] m_ModeNumbers;
		m_ModeNumbers = NULL;
	}
	RtlZeroMemory(&mode_list, sizeof(output_modelist));
	mode_list.modelist_size = 0;
	RtlZeroMemory(&gpu_disp_mode_ext, sizeof(GPU_DISP_MODE_EXT) * MAX_MODELIST_SIZE);
	m_pFrameBuf = NULL;
}

VioGpuAdapterLite::VioGpuAdapterLite(_In_ PVOID pvDeviceContext) : IVioGpuAdapterLite(pvDeviceContext)
{
	PAGED_CODE();
	TRACING();

	m_Id = g_InstanceId++;
	m_pCursorBuf = NULL;
	m_PendingWorks = 0;
	KeInitializeEvent(&m_ConfigUpdateEvent,
		SynchronizationEvent,
		FALSE);
	m_bStopWorkThread = FALSE;
	m_pWorkThread = NULL;
	m_bBlobSupported = FALSE;
	hpd_event = NULL;
}

VioGpuAdapterLite::~VioGpuAdapterLite(void)
{
	PAGED_CODE();
	TRACING();
	if (hpd_event) {
		ObDereferenceObject(hpd_event);
	}
	VioGpuAdapterLiteClose();
	HWClose();
	m_Id = 0;
	g_InstanceId--;
}

static BOOLEAN IsSameMode(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode)
{
	BOOLEAN result = FALSE;

	if (!pModeInfo || !pCurrentMode)
		return result;

	if (pModeInfo->VisScreenWidth == pCurrentMode->DispInfo.Width &&
		pModeInfo->VisScreenHeight == pCurrentMode->DispInfo.Height &&
		pModeInfo->ScreenStride == pCurrentMode->DispInfo.Pitch)
		result = TRUE;

	return result;
}

NTSTATUS VioGpuAdapterLite::SetCurrentModeExt(CURRENT_MODE* pCurrentMode)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	PAGED_CODE();
	TRACING();

	if (!pCurrentMode) {
		ERR("Mode pointer is NULL\n");
		return status;
	}

	DBGPRINT("ScreenNum = %d, Mode = %dx%d\n", pCurrentMode->DispInfo.TargetId, pCurrentMode->DispInfo.Width, pCurrentMode->DispInfo.Height);

	for (ULONG idx = 0; idx < m_screen[pCurrentMode->DispInfo.TargetId].GetModeCount(); idx++)
	{
		status = STATUS_SUCCESS;

		if (!IsSameMode(&m_screen[pCurrentMode->DispInfo.TargetId].m_ModeInfo[idx], pCurrentMode))
			continue;

		if (!m_screen[pCurrentMode->DispInfo.TargetId].m_FlushCount) {
			DestroyFrameBufferObj(pCurrentMode->DispInfo.TargetId, FALSE);
			CreateFrameBufferObj(&m_screen[pCurrentMode->DispInfo.TargetId].m_ModeInfo[idx], pCurrentMode);
			DBGPRINT("screen %d: setting current mode (%d x %d)\n",
				pCurrentMode->DispInfo.TargetId, m_screen[pCurrentMode->DispInfo.TargetId].m_ModeInfo[idx].VisScreenWidth,
				m_screen[pCurrentMode->DispInfo.TargetId].m_ModeInfo[idx].VisScreenHeight);
		}
		else {
			DBGPRINT("For screen %d Pending flush (%d) with Qemu so not sending another request\n",
				pCurrentMode->DispInfo.TargetId, m_screen[pCurrentMode->DispInfo.TargetId].m_FlushCount);
		}
		break;
	}

	return status;
}

NTSTATUS VioGpuAdapterLite::VioGpuAdapterLiteInit()
{
	PAGED_CODE();
	NTSTATUS status = STATUS_SUCCESS;

	TRACING();

	if (IsHardwareInit()) {
		DBGPRINT("Already Initialized\n");
		VioGpuDbgBreak();
		return status;
	}
	status = VirtIoDeviceInit();
	if (!NT_SUCCESS(status)) {
		DBGPRINT("Failed to initialize virtio device, error %x\n", status);
		VioGpuDbgBreak();
		return status;
	}

	m_u64HostFeatures = virtio_get_features(&m_VioDev);
	m_u64GuestFeatures = 0;
	do
	{
		struct virtqueue* vqs[2];
		if (!AckFeature(VIRTIO_F_VERSION_1))
		{
			status = STATUS_UNSUCCESSFUL;
			break;
		}
#if (NTDDI_VERSION >= NTDDI_WIN10)
		AckFeature(VIRTIO_F_ACCESS_PLATFORM);
#endif
		status = virtio_set_features(&m_VioDev, m_u64GuestFeatures);
		if (!NT_SUCCESS(status))
		{
			ERR("virtio_set_features failed with %x\n", status);
			VioGpuDbgBreak();
			break;
		}

		status = virtio_find_queues(
			&m_VioDev,
			2,
			vqs);
		if (!NT_SUCCESS(status)) {
			ERR("virtio_find_queues failed with error %x\n", status);
			VioGpuDbgBreak();
			break;
		}

		if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0) ||
			!m_CursorQueue.Init(&m_VioDev, vqs[1], 1)) {
			ERR("Failed to initialize virtio queues\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			break;
		}

		virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_scanouts),
			&m_u32NumScanouts, sizeof(m_u32NumScanouts));
		if (m_u32NumScanouts > MAX_SCAN_OUT) {
			ERR("Number of displays provided by Hypervisor: %d are more than what the KMD supports: %d\n",
				m_u32NumScanouts, MAX_SCAN_OUT);
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			break;
		}

		virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_capsets),
			&m_u32NumCapsets, sizeof(m_u32NumCapsets));
	} while (0);
	if (status == STATUS_SUCCESS)
	{
		virtio_device_ready(&m_VioDev);
		SetHardwareInit(TRUE);
	}
	else
	{
		virtio_add_status(&m_VioDev, VIRTIO_CONFIG_S_FAILED);
		VioGpuDbgBreak();
	}

	/*
	 * We need to check if Qemu supports Blob resources for this virtio device
	 * and we also allow the KMD to turn off this capability independently by a
	 * #define as well
	*/
	if (virtio_is_feature_enabled(m_u64HostFeatures, VIRTIO_GPU_F_RESOURCE_BLOB) &&
		USE_BLOB_RESOURCE)
	{
		m_bBlobSupported = TRUE;
	}
	else
	{
		m_bBlobSupported = FALSE;
	}

	return status;
}

void VioGpuAdapterLite::VioGpuAdapterLiteClose()
{
	PAGED_CODE();
	TRACING();

	if (IsHardwareInit())
	{
		SetHardwareInit(FALSE);

		DestroyCursor();
		for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
			DestroyFrameBufferObj(i, TRUE);
		}

		m_CtrlQueue.DisableInterrupt();
		m_CursorQueue.DisableInterrupt();
		virtio_device_reset(&m_VioDev);
		virtio_delete_queues(&m_VioDev);
		m_CtrlQueue.Close();
		m_CursorQueue.Close();
		virtio_device_shutdown(&m_VioDev);
		m_CurrentModeInfo.Flags.FrameBufferIsActive = FALSE;
		m_CurrentModeInfo.FrameBuffer.Ptr = NULL;
	}
}

NTSTATUS VioGpuAdapterLite::SetPowerState(DEVICE_POWER_STATE DevicePowerState)
{
	PAGED_CODE();
	TRACING();
	DBGPRINT("DevicePowerState = %d\n", DevicePowerState);

	switch (DevicePowerState)
	{
	case PowerDeviceUnspecified:
	case PowerDeviceD0: {
		VioGpuAdapterLiteInit();
	} break;
	case PowerDeviceD1:
	case PowerDeviceD2:
	case PowerDeviceD3: {
		VioGpuAdapterLiteClose();
	} break;
	}
	return STATUS_SUCCESS;
}

BOOLEAN VioGpuAdapterLite::AckFeature(UINT64 Feature)
{
	PAGED_CODE();

	if (virtio_is_feature_enabled(m_u64HostFeatures, Feature))
	{
		virtio_feature_enable(m_u64GuestFeatures, Feature);
		return TRUE;
	}
	return FALSE;
}

static EDID_V1 g_gpu_edid = {
	{0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ,0xFF, 0x00},
	{0x49, 0x14},
	{0x34, 0x12},
	{0x00, 0x00, 0x00, 0x00},
	{0xff, 0x1d},
	0x01, 0x04,
	{0xa3, 0x00, 0x00, 0x78, 0x22, 0xEE, 0x95,
	0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
	0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01},
	{0x6c, 0x20, 0x80, 0x30, 0x42, 0x00, 0x32,
	0x30, 0x40, 0xc0, 0x13, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x1e},
	{0x00, 0x00, 0x00, 0xFD, 0x00,
	0x32, 0x7D, 0x1E, 0xA0, 0x78, 0x01, 0x0A, 0x20,
	0x20 ,0x20, 0x20, 0x20, 0x20},
	{0x00, 0x00, 0x00, 0x10, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x10, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00},
	0x00,
	0x00,
};

NTSTATUS VioGpuAdapterLite::VirtIoDeviceInit()
{
	PAGED_CODE();
	TRACING();
	return virtio_device_initialize(
		&m_VioDev,
		&VioGpuSystemOps,
		this,
		m_PciResources.IsMSIEnabled());
}

PBYTE VioGpuAdapterLite::GetEdidData(UINT Id)
{
	PAGED_CODE();

	return m_bEDID ? m_screen[Id].m_EDIDs : (PBYTE)(&g_gpu_edid);//.data;
}

NTSTATUS VioGpuAdapterLite::HWInit(WDFCMRESLIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
	PAGED_CODE();

	NTSTATUS status = STATUS_SUCCESS;
	HANDLE   threadHandle = 0;
	TRACING();
	UINT size = 0;

	if (!pResList || !pDispInfo) {
		ERR("Invalid arguments!\n");
		return STATUS_UNSUCCESSFUL;
	}

	do
	{
		if (!m_PciResources.Init(this->m_pvDeviceContext, pResList))
		{
			ERR("Incomplete resources\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			break;
		}

		status = VioGpuAdapterLiteInit();
		if (!NT_SUCCESS(status))
		{
			ERR("Failed initialize adapter %x\n", status);
			VioGpuDbgBreak();
			break;
		}

		size = m_CtrlQueue.QueryAllocation() + m_CursorQueue.QueryAllocation();
		DBGPRINT("size %d\n", size);
		ASSERT(size);

		if (!m_GpuBuf.Init(size)) {
			ERR("Failed to initialize buffers\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			break;
		}

		m_CtrlQueue.SetGpuBuf(&m_GpuBuf);
		m_CursorQueue.SetGpuBuf(&m_GpuBuf);

		if (!m_Idr.Init(1)) {
			ERR("Failed to initialize id generator\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			break;
		}

	} while (0);
	//FIXME!!! exit if the block above failed

	status = PsCreateSystemThread(&threadHandle,
		(ACCESS_MASK)0,
		NULL,
		(HANDLE)0,
		NULL,
		VioGpuAdapterLite::ThreadWork,
		this);

	if (!NT_SUCCESS(status))
	{
		ERR("Failed to create system thread, status %x\n", status);
		VioGpuDbgBreak();
		return status;
	}
	ObReferenceObjectByHandle(threadHandle,
		THREAD_ALL_ACCESS,
		NULL,
		KernelMode,
		(PVOID*)(&m_pWorkThread),
		NULL);

	ZwClose(threadHandle);

	status = GetModeList(pDispInfo);
	if (!NT_SUCCESS(status))
	{
		ERR("GetModeList failed with %x\n", status);
		VioGpuDbgBreak();
	}

	PHYSICAL_ADDRESS fb_pa = m_PciResources.GetPciBar(0)->GetPA();
	UINT fb_size = (UINT)m_PciResources.GetPciBar(0)->GetSize();
	UINT req_size = pDispInfo->Pitch * pDispInfo->Height;
	req_size = max(req_size, fb_size);
	//FIXME!!! update and validate size properly
	req_size = max(0x800000, req_size);

	if (fb_pa.QuadPart == 0 || fb_size < req_size) {
		fb_pa.QuadPart = 0;
		fb_size = req_size;
	}

	for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
		if (!m_screen[i].m_FrameSegment.Init(req_size, NULL)) {
			ERR("Failed to allocate FB memory segment\n");
			status = STATUS_INSUFFICIENT_RESOURCES;
			VioGpuDbgBreak();
			return status;
		}
	}

	if (!m_CursorSegment.Init(POINTER_SIZE * POINTER_SIZE * 4, NULL))
	{
		ERR("failed to allocate Cursor memory segment\n");
		status = STATUS_INSUFFICIENT_RESOURCES;
		VioGpuDbgBreak();
		return status;
	}

	return status;
}

NTSTATUS VioGpuAdapterLite::HWClose(void)
{
	PAGED_CODE();
	TRACING();
	SetHardwareInit(FALSE);

	LARGE_INTEGER timeout = { 0 };
	timeout.QuadPart = Int32x32To64(1000, -10000);

	m_bStopWorkThread = TRUE;
	KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);

	if (KeWaitForSingleObject(m_pWorkThread,
		Executive,
		KernelMode,
		FALSE,
		&timeout) == STATUS_TIMEOUT) {
		ERR("---> Failed to exit the worker thread\n");
		VioGpuDbgBreak();
	}

	ObDereferenceObject(m_pWorkThread);

	for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
		m_screen[i].m_FrameSegment.Close();
	}
	m_CursorSegment.Close();

	return STATUS_SUCCESS;
}

BOOLEAN FindUpdateRect(
	_In_ ULONG             NumMoves,
	_In_ D3DKMT_MOVE_RECT* pMoves,
	_In_ ULONG             NumDirtyRects,
	_In_ PRECT             pDirtyRect,
	_In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
	_Out_ PRECT pUpdateRect)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(Rotation);
	BOOLEAN updated = FALSE;
	for (ULONG i = 0; i < NumMoves; i++)
	{
		PRECT  pRect = &pMoves[i].DestRect;
		if (!updated)
		{
			*pUpdateRect = *pRect;
			updated = TRUE;
		}
		else
		{
			pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
			pUpdateRect->left = min(pRect->left, pUpdateRect->left);
			pUpdateRect->right = max(pRect->right, pUpdateRect->right);
			pUpdateRect->top = min(pRect->top, pUpdateRect->top);
		}
	}
	for (ULONG i = 0; i < NumDirtyRects; i++)
	{
		PRECT  pRect = &pDirtyRect[i];
		if (!updated)
		{
			*pUpdateRect = *pRect;
			updated = TRUE;
		}
		else
		{
			pUpdateRect->bottom = max(pRect->bottom, pUpdateRect->bottom);
			pUpdateRect->left = min(pRect->left, pUpdateRect->left);
			pUpdateRect->right = max(pRect->right, pUpdateRect->right);
			pUpdateRect->top = min(pRect->top, pUpdateRect->top);
		}
	}
	if (Rotation == D3DKMDT_VPPR_ROTATE90 || Rotation == D3DKMDT_VPPR_ROTATE270)
	{
	}
	return updated;
}

NTSTATUS VioGpuAdapterLite::ExecutePresentDisplayZeroCopy(
	_In_ BYTE* SrcAddr,
	_In_ UINT               SrcBytesPerPixel,
	_In_ LONG               SrcPitch,
	_In_ UINT               SrcWidth,
	_In_ UINT               SrcHeight,
	_In_ UINT               ScreenNum)
{
	PAGED_CODE();
	TRACING();

	BLT_INFO SrcBltInfo = { 0 };
	BLT_INFO DstBltInfo = { 0 };
	RECT rect = { 0 };
	NTSTATUS status;

	DBGPRINT("SrcBytesPerPixel = %d Mode = %dx%d\n", SrcBytesPerPixel, SrcWidth, SrcHeight);

	CURRENT_MODE tempCurrentMode = { 0 };
	tempCurrentMode.DispInfo.Width = SrcWidth;
	tempCurrentMode.DispInfo.Height = SrcHeight;
	tempCurrentMode.DispInfo.Pitch = SrcPitch;
	tempCurrentMode.DispInfo.TargetId = ScreenNum;
	tempCurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
	tempCurrentMode.FrameBuffer.Ptr = SrcAddr;

	status = SetCurrentModeExt(&tempCurrentMode);

	DBGPRINT("offset = (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
		rect.left,
		rect.top,
		SrcWidth,
		SrcHeight,
		0,
		0,
		SrcWidth,
		SrcHeight);

	Close(ScreenNum);

	return status;
}

VOID VioGpuAdapterLite::BlackOutScreen(CURRENT_MODE* pCurrentMod)
{
	PAGED_CODE();
	TRACING();

	if (pCurrentMod->Flags.FrameBufferIsActive) {
		UINT ScreenHeight = pCurrentMod->DispInfo.Height;
		UINT ScreenPitch = pCurrentMod->DispInfo.Pitch;
		BYTE* pDst = (BYTE*)pCurrentMod->FrameBuffer.Ptr;

		UINT resid = 0;

		if (pDst)
		{
			RtlZeroMemory(pDst, ScreenHeight * ScreenPitch);
		}

		//FIXME!!! rotation

		resid = m_screen[pCurrentMod->DispInfo.TargetId].m_pFrameBuf->GetId();

		m_CtrlQueue.TransferToHost2D(resid, 0UL, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0, NULL);
		m_screen[pCurrentMod->DispInfo.TargetId].m_FlushCount++;
		m_CtrlQueue.ResFlush(resid, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0, pCurrentMod->DispInfo.TargetId,
			&m_screen[pCurrentMod->DispInfo.TargetId].m_FlushEvent);
	}
}

NTSTATUS VioGpuAdapterLite::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur)
{
	PAGED_CODE();

	TRACING();
	DBGPRINT("Flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n",
		pSetPointerShape->Flags.Value,
		pSetPointerShape->Pitch,
		pSetPointerShape->pPixels,
		pSetPointerShape->VidPnSourceId,
		pSetPointerShape->Width,
		pSetPointerShape->Height,
		pSetPointerShape->XHot,
		pSetPointerShape->YHot);

	DestroyCursor();
	if (CreateCursor(pSetPointerShape, pModeCur))
	{
		PGPU_UPDATE_CURSOR crsr;
		PGPU_VBUFFER vbuf;
		UINT ret = 0;
		crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
		RtlZeroMemory(crsr, sizeof(*crsr));

		crsr->hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
		crsr->resource_id = m_pCursorBuf->GetId();
		crsr->pos.x = 0;
		crsr->pos.y = 0;
		crsr->hot_x = pSetPointerShape->XHot;
		crsr->hot_y = pSetPointerShape->YHot;
		ret = m_CursorQueue.QueueCursor(vbuf);
		DBGPRINT("vbuf = %p, ret = %d\n", vbuf, ret);
		if (ret == 0) {
			return STATUS_SUCCESS;
		}
		VioGpuDbgBreak();
	}
	ERR("Failed to create cursor\n");
	VioGpuDbgBreak();
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS VioGpuAdapterLite::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_MODE* pModeCur)
{
	PAGED_CODE();
	if (m_pCursorBuf != NULL)
	{
		PGPU_UPDATE_CURSOR crsr;
		PGPU_VBUFFER vbuf;
		UINT ret = 0;
		crsr = (PGPU_UPDATE_CURSOR)m_CursorQueue.AllocCursor(&vbuf);
		RtlZeroMemory(crsr, sizeof(*crsr));

		crsr->hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
		crsr->resource_id = m_pCursorBuf->GetId();

		if (!pSetPointerPosition->Flags.Visible ||
			(UINT)pSetPointerPosition->X > pModeCur->SrcModeWidth ||
			(UINT)pSetPointerPosition->Y > pModeCur->SrcModeHeight ||
			pSetPointerPosition->X < 0 ||
			pSetPointerPosition->Y < 0) {
			DBGPRINT("(%d - %d) Visiable = %d Value = %x VidPnSourceId = %d\n",
				pSetPointerPosition->X,
				pSetPointerPosition->Y,
				pSetPointerPosition->Flags.Visible,
				pSetPointerPosition->Flags.Value,
				pSetPointerPosition->VidPnSourceId);
			crsr->pos.x = 0;
			crsr->pos.y = 0;
		}
		else {
			DBGPRINT("(%d - %d) Visiable = %d Value = %x VidPnSourceId = %d posX = %d, psY = %d\n",
				pSetPointerPosition->X,
				pSetPointerPosition->Y,
				pSetPointerPosition->Flags.Visible,
				pSetPointerPosition->Flags.Value,
				pSetPointerPosition->VidPnSourceId,
				pSetPointerPosition->X,
				pSetPointerPosition->Y);
			crsr->pos.x = pSetPointerPosition->X;
			crsr->pos.y = pSetPointerPosition->Y;
		}
		ret = m_CursorQueue.QueueCursor(vbuf);
		DBGPRINT("vbuf = %p, ret = %d\n", vbuf, ret);
		if (ret == 0) {
			return STATUS_SUCCESS;
		}
		VioGpuDbgBreak();
	}
	return STATUS_UNSUCCESSFUL;
}

BOOLEAN VioGpuAdapterLite::GetDisplayInfo(UINT32 screen_num)
{
	PAGED_CODE();
	TRACING();

	PGPU_VBUFFER vbuf = NULL;
	ULONG xres = 0;
	ULONG yres = 0;

	if (m_CtrlQueue.AskDisplayInfo(&vbuf, &m_screen[screen_num].m_DisplayInfoEvent)) {
		m_screen[screen_num].enabled = m_CtrlQueue.GetDisplayInfo(vbuf, screen_num, &xres, &yres);
		DBGPRINT("Screen %d status = %s\n", screen_num, (m_screen[screen_num].enabled) ? "Enabled" : "Disabled");
		m_CtrlQueue.ReleaseBuffer(vbuf);
		if (xres && yres) {
			DBGPRINT("(%dx%d)\n", xres, yres);
			m_screen[screen_num].SetCustomDisplay((USHORT)xres, (USHORT)yres);
		}
	}
	return TRUE;
}

void VioGpuAdapterLite::ProcessEdid(UINT32 screen_num)
{
	PAGED_CODE();

	if (virtio_is_feature_enabled(m_u64HostFeatures, VIRTIO_GPU_F_EDID)) {
		GetEdids(screen_num);
		AddEdidModes(screen_num);
	}
	else {
		return;
	}
}

BOOLEAN VioGpuAdapterLite::GetEdids(UINT32 screen_num)
{
	PAGED_CODE();

	TRACING();

	PGPU_VBUFFER vbuf = NULL;

	if (m_CtrlQueue.AskEdidInfo(&vbuf, screen_num, &m_screen[screen_num].m_EdidEvent) &&
		m_CtrlQueue.GetEdidInfo(vbuf, screen_num, m_screen[screen_num].m_EDIDs)) {
		m_bEDID = TRUE;
	}
	m_CtrlQueue.ReleaseBuffer(vbuf);

	return TRUE;
}

GPU_DISP_MODE gpu_disp_modes[16] =
{
	{640, 480},
	{800, 600},
	{1024, 768},
	{1920, 1080},
	{1920, 1200},
	{2560, 1600},
	{2560, 1440},
	{3840, 2160},
	{0, 0},
};

VOID VioGpuAdapterLite::CopyResolution(UINT32 screen_num, struct edid_info* edata)
{
	for (unsigned int i = 0; i < edata->mode_size; i++) {
		edata->mode_list[i].width = m_screen[screen_num].gpu_disp_mode_ext[i].XResolution;
		edata->mode_list[i].height = m_screen[screen_num].gpu_disp_mode_ext[i].YResolution;
		edata->mode_list[i].refreshrate = m_screen[screen_num].gpu_disp_mode_ext[i].refresh;
	}
}

void VioGpuAdapterLite::AddEdidModes(UINT32 screen_num)
{
	PAGED_CODE();
	TRACING();
	if (parse_edid_data(GetEdidData(screen_num), &m_screen[screen_num].mode_list) != 0) {
		for (unsigned int i = 0; i < QEMU_MODELIST_SIZE; i++) {
			m_screen[screen_num].gpu_disp_mode_ext[i].XResolution = (USHORT)qemu_modelist[i].x;
			m_screen[screen_num].gpu_disp_mode_ext[i].YResolution = (USHORT)qemu_modelist[i].y;
			m_screen[screen_num].gpu_disp_mode_ext[i].refresh = qemu_modelist[i].rr;
		}
	}
	else {
		for (unsigned int i = 0; i < m_screen[screen_num].mode_list.modelist_size; i++) {
			m_screen[screen_num].gpu_disp_mode_ext[i].XResolution = (USHORT)m_screen[screen_num].mode_list.modelist[i].width;
			m_screen[screen_num].gpu_disp_mode_ext[i].YResolution = (USHORT)m_screen[screen_num].mode_list.modelist[i].height;
			m_screen[screen_num].gpu_disp_mode_ext[i].refresh = m_screen[screen_num].mode_list.modelist[i].refresh_rate;
		}
	}
}


void ScreenInfo::SetVideoModeInfo(UINT Idx, PGPU_DISP_MODE_EXT pModeInfo)
{
	PAGED_CODE();

	PVIDEO_MODE_INFORMATION pMode = NULL;
	UINT bytes_pp = (VGPU_BPP + 7) / 8;

	pMode = &m_ModeInfo[Idx];
	pMode->Length = sizeof(VIDEO_MODE_INFORMATION);
	pMode->ModeIndex = Idx;
	pMode->VisScreenWidth = pModeInfo->XResolution;
	pMode->VisScreenHeight = pModeInfo->YResolution;
	pMode->ScreenStride = (pModeInfo->XResolution * bytes_pp + 3) & ~0x3;
}

void ScreenInfo::SetCustomDisplay(_In_ USHORT xres, _In_ USHORT yres)
{
	PAGED_CODE();

	GPU_DISP_MODE_EXT tmpModeInfo = { 0 };

	if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE) {
		WARNING("(%dx%d) less than (%dx%d)\n",
			xres, yres, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE);
	}
	tmpModeInfo.XResolution = max(MIN_WIDTH_SIZE, xres);
	tmpModeInfo.YResolution = max(MIN_HEIGHT_SIZE, yres);

	m_CustomMode = (USHORT)((m_CustomMode == m_ModeCount - 1) ? m_ModeCount - 2 : m_ModeCount - 1);

	DBGPRINT("%d (%dx%d)\n", m_CustomMode, tmpModeInfo.XResolution, tmpModeInfo.YResolution);

	SetVideoModeInfo(m_CustomMode, &tmpModeInfo);
}

NTSTATUS VioGpuAdapterLite::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
	PAGED_CODE();

	NTSTATUS Status = STATUS_SUCCESS;

	TRACING();

	for (UINT32 i = 0; i < m_u32NumScanouts; i++) {

		UINT ModeCount = 0;
		m_screen[i].Reset();

		ProcessEdid(i);
		while ((m_screen[i].gpu_disp_mode_ext[ModeCount].XResolution >= MIN_WIDTH_SIZE) &&
			(m_screen[i].gpu_disp_mode_ext[ModeCount].YResolution >= MIN_HEIGHT_SIZE)) ModeCount++;

		ModeCount += 2;
		m_screen[i].m_ModeInfo = new (PagedPool) VIDEO_MODE_INFORMATION[ModeCount];
		if (!m_screen[i].m_ModeInfo)
		{
			Status = STATUS_NO_MEMORY;
			ERR("VioGpuAdapterLite::GetModeList failed to allocate m_ModeInfo memory\n");
			return Status;
		}
		RtlZeroMemory(m_screen[i].m_ModeInfo, sizeof(VIDEO_MODE_INFORMATION) * ModeCount);

		m_screen[i].m_ModeNumbers = new (PagedPool) USHORT[ModeCount];
		if (!m_screen[i].m_ModeNumbers)
		{
			Status = STATUS_NO_MEMORY;
			ERR("VioGpuAdapterLite::GetModeList failed to allocate m_ModeNumbers memory\n");
			return Status;
		}
		RtlZeroMemory(m_screen[i].m_ModeNumbers, sizeof(USHORT) * ModeCount);
		m_screen[i].m_CurrentMode = 0;
		DBGPRINT("Screen = %d, m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", i, m_screen[i].m_ModeInfo, m_screen[i].m_ModeNumbers);

		pDispInfo->Height = max(pDispInfo->Height, MIN_HEIGHT_SIZE);
		pDispInfo->Width = max(pDispInfo->Width, MIN_WIDTH_SIZE);
		pDispInfo->ColorFormat = D3DDDIFMT_X8R8G8B8;
		pDispInfo->Pitch = (BPPFromPixelFormat(pDispInfo->ColorFormat) / BITS_PER_BYTE) * pDispInfo->Width;

		USHORT SuitableModeCount;
		USHORT CurrentMode;

		for (CurrentMode = 0, SuitableModeCount = 0;
			CurrentMode < ModeCount - 2;
			CurrentMode++)
		{

			PGPU_DISP_MODE_EXT tmpModeInfo = &m_screen[i].gpu_disp_mode_ext[CurrentMode];

			DBGPRINT("modes[%d] x_res = %d, y_res = %d\n",
				CurrentMode, tmpModeInfo->XResolution, tmpModeInfo->YResolution);

			if (tmpModeInfo->XResolution >= pDispInfo->Width &&
				tmpModeInfo->YResolution >= pDispInfo->Height)
			{
				m_screen[i].m_ModeNumbers[SuitableModeCount] = SuitableModeCount;
				m_screen[i].SetVideoModeInfo(SuitableModeCount, tmpModeInfo);
				if (tmpModeInfo->XResolution == NOM_WIDTH_SIZE &&
					tmpModeInfo->YResolution == NOM_HEIGHT_SIZE)
				{
					m_screen[i].m_CurrentMode = SuitableModeCount;
				}
				SuitableModeCount++;
			}
		}

		if (SuitableModeCount == 0)
		{
			ERR("No video modes supported\n");
			Status = STATUS_UNSUCCESSFUL;
		}

		m_screen[i].m_CustomMode = SuitableModeCount;
		for (CurrentMode = SuitableModeCount;
			CurrentMode < SuitableModeCount + 2;
			CurrentMode++)
		{
			m_screen[i].m_ModeNumbers[CurrentMode] = CurrentMode;
			memcpy(&m_screen[i].m_ModeInfo[CurrentMode], &m_screen[i].m_ModeInfo[m_screen[i].m_CurrentMode], sizeof(VIDEO_MODE_INFORMATION));
		}

		m_screen[i].m_ModeCount = SuitableModeCount + 2;
		DBGPRINT("ModeCount filtered %d\n", m_screen[i].m_ModeCount);

		GetDisplayInfo(i);

		for (ULONG idx = 0; idx < m_screen[i].GetModeCount(); idx++)
		{
			DBGPRINT("type %x, XRes = %d, YRes = %d\n",
				m_screen[i].m_ModeNumbers[idx],
				m_screen[i].m_ModeInfo[idx].VisScreenWidth,
				m_screen[i].m_ModeInfo[idx].VisScreenHeight);
		}
	}
	return Status;
}
PAGED_CODE_SEG_END

BOOLEAN VioGpuAdapterLite::InterruptRoutine(_In_  ULONG MessageNumber)
{
	DBGPRINT("MessageNumber = %d\n", MessageNumber);
	BOOLEAN serviced = TRUE;
	ULONG intReason = 0;
	PDEVICE_CONTEXT pDevcieContext = (PDEVICE_CONTEXT)this->m_pvDeviceContext;

	if (m_PciResources.IsMSIEnabled())
	{
		switch (MessageNumber) {
		case 0:
			intReason = ISR_REASON_CHANGE;
			break;
		case 1:
			intReason = ISR_REASON_DISPLAY;
			break;
		case 2:
			intReason = ISR_REASON_CURSOR;
			break;
		default:
			serviced = FALSE;
			ERR("Unknown Interrupt Reason MessageNumber%d\n", MessageNumber);
		}
	}
	else {
		UNREFERENCED_PARAMETER(MessageNumber);
		UCHAR  isrstat = virtio_read_isr_status(&m_VioDev);

		switch (isrstat) {
		case 1:
			intReason = (ISR_REASON_DISPLAY | ISR_REASON_CURSOR);
			break;
		case 3:
			intReason = ISR_REASON_CHANGE;
			break;
		default:
			serviced = FALSE;
			ERR("Unknown Interrupt Reason MessageNumber%d\n", isrstat);
		}
	}


	if (serviced) {
		InterlockedOr((PLONG)&m_PendingWorks, intReason);
		WdfInterruptQueueDpcForIsr(pDevcieContext->WdfInterrupt);
	}

	return serviced;
}

void VioGpuAdapterLite::ThreadWork(_In_ PVOID Context)
{
	VioGpuAdapterLite* pdev = reinterpret_cast<VioGpuAdapterLite*>(Context);
	pdev->ThreadWorkRoutine();
}

void VioGpuAdapterLite::ThreadWorkRoutine(void)
{
	KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

	for (;;)
	{
		KeWaitForSingleObject(&m_ConfigUpdateEvent,
			Executive,
			KernelMode,
			FALSE,
			NULL);

		if (m_bStopWorkThread) {
			PsTerminateSystemThread(STATUS_SUCCESS);
		}
		ConfigChanged();
	}
}

void VioGpuAdapterLite::ConfigChanged(void)
{
	TRACING();
	UINT32 events_read, events_clear = 0;
	virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read),
		&events_read, sizeof(m_u32NumScanouts));
	if (events_read & VIRTIO_GPU_EVENT_DISPLAY) {
		for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
			GetDisplayInfo(i);
		}
		if (hpd_event) {
			DBGPRINT("Sending Hot Plug event to UMD\n");
			KeSetEvent(hpd_event, IO_NO_INCREMENT, FALSE);
		}
		events_clear |= VIRTIO_GPU_EVENT_DISPLAY;
		virtio_set_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_clear),
			&events_clear, sizeof(m_u32NumScanouts));
		//        UpdateChildStatus(FALSE);
		//        ProcessEdid();
		// TODO: Enable following if needed
		//UpdateChildStatus(TRUE);
	}
}

VOID VioGpuAdapterLite::DpcRoutine(void)
{
	TRACING();
	PGPU_VBUFFER pvbuf = NULL;
	UINT len = 0;
	ULONG reason;
	while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) != 0)
	{
		if ((reason & ISR_REASON_DISPLAY)) {
			while ((pvbuf = m_CtrlQueue.DequeueBuffer(&len)) != NULL)
			{
				DBGPRINT("m_CtrlQueue pvbuf = %p len = %d\n", pvbuf, len);
				PGPU_CTRL_HDR pcmd = (PGPU_CTRL_HDR)pvbuf->buf;
				PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)pvbuf->resp_buf;
				PKEVENT evnt = pvbuf->event;
				if (evnt == NULL)
				{
					if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
					{
						DBGPRINT("type = %xlu flags = %lu fence_id = %llu ctx_id = %lu cmd_type = %lu\n",
							resp->type, resp->flags, resp->fence_id, resp->ctx_id, pcmd->type);
					}
					m_CtrlQueue.ReleaseBuffer(pvbuf);
					continue;
				}
				switch (pcmd->type)
				{
				case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
					if (m_screen[pcmd->fence_id].m_FlushCount > 0) {
						m_screen[pcmd->fence_id].m_FlushCount--;
						DBGPRINT("Screen id = %lld, m_FlushCount = %d\n", pcmd->fence_id, m_screen[pcmd->fence_id].m_FlushCount);
					}
					else {
						ERR("Screen is %d, Flush Count is %d\n", (int)pcmd->fence_id,
							m_screen[pcmd->fence_id].m_FlushCount);
					}
				case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
				case VIRTIO_GPU_CMD_GET_EDID:
				{
					ASSERT(evnt);
					KeSetEvent(evnt, IO_NO_INCREMENT, FALSE);
				}
				break;
				default:
					ERR("Unknown cmd type 0x%x\n", resp->type);
					break;
				}
			}
		}
		if ((reason & ISR_REASON_CURSOR)) {
			while ((pvbuf = m_CursorQueue.DequeueCursor(&len)) != NULL)
			{
				DBGPRINT("m_CursorQueue pvbuf = %p len = %u\n", pvbuf, len);
				m_CursorQueue.ReleaseBuffer(pvbuf);
			};
		}
		if (reason & ISR_REASON_CHANGE) {
			DBGPRINT("ConfigChanged\n");
			KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
		}
	}
}

VOID VioGpuAdapterLite::ResetDevice(VOID)
{
	TRACING();
}

UINT ColorFormat(UINT format)
{
	switch (format)
	{
	case D3DDDIFMT_A8R8G8B8:
		return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
	case D3DDDIFMT_X8R8G8B8:
		return VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
	case D3DDDIFMT_A8B8G8R8:
		return VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
	case D3DDDIFMT_X8B8G8R8:
		return VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
	}
	ERR("Unsupported color format %d\n", format);
	return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
}

PAGED_CODE_SEG_BEGIN
void VioGpuAdapterLite::CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode)
{
	UINT resid, format, size;
	VioGpuObj* obj;
	PAGED_CODE();
	TRACING();
	DBGPRINT("%d: %d, (%d x %d)\n", m_Id, pCurrentMode->DispInfo.TargetId,
		pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);
	ASSERT(m_screen[pCurrentMode->DispInfo.TargetId].m_pFrameBuf == NULL);
	size = pModeInfo->ScreenStride * pModeInfo->VisScreenHeight;
	format = ColorFormat(pCurrentMode->DispInfo.ColorFormat);
	DBGPRINT("(%d -> %d)\n", pCurrentMode->DispInfo.ColorFormat, format);
	resid = m_Idr.GetId();

	if (!m_bBlobSupported) {
		m_CtrlQueue.CreateResource(resid, format, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);
	}

	// Update the frame segment based on the current mode
	if (pCurrentMode->FrameBuffer.Ptr) {
		m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment.InitExt(size, pCurrentMode->FrameBuffer.Ptr);
	}
	else if (m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment.GetFbVAddr() && size > m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment.GetSize()) {
		m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment.Close();
		m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment.Init(size, NULL);
	}

	obj = new(NonPagedPoolNx) VioGpuObj();
	if (!obj->Init(size, &m_screen[pCurrentMode->DispInfo.TargetId].m_FrameSegment))
	{
		ERR("Failed to init obj size = %d\n", size);
		delete obj;
		return;
	}

	GpuObjectAttach(resid, obj, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);

	if (m_bBlobSupported)
	{
		m_CtrlQueue.SetScanoutBlob(pCurrentMode->DispInfo.TargetId, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, format, 0, 0);
	}
	else
	{
		m_CtrlQueue.SetScanout(pCurrentMode->DispInfo.TargetId, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
	}
	m_CtrlQueue.TransferToHost2D(resid, 0, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0, NULL);
	m_screen[pCurrentMode->DispInfo.TargetId].m_FlushCount++;
	DBGPRINT("Screen num = %d, flushcount = %d\n", pCurrentMode->DispInfo.TargetId, m_screen[pCurrentMode->DispInfo.TargetId].m_FlushCount);
	m_CtrlQueue.ResFlush(resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0, pCurrentMode->DispInfo.TargetId,
		&m_screen[pCurrentMode->DispInfo.TargetId].m_FlushEvent);
	m_screen[pCurrentMode->DispInfo.TargetId].m_pFrameBuf = obj;
	pCurrentMode->FrameBuffer.Ptr = obj->GetVirtualAddress();
	pCurrentMode->Flags.FrameBufferIsActive = TRUE;
}

void VioGpuAdapterLite::DestroyFrameBufferObj(UINT32 screen_num, BOOLEAN bReset)
{
	PAGED_CODE();
	TRACING();
	UINT resid = 0;

	if (m_screen[screen_num].m_pFrameBuf != NULL)
	{
		resid = (UINT)m_screen[screen_num].m_pFrameBuf->GetId();
		//if (bReset == TRUE) {
		//    m_CtrlQueue.SetScanout(0/*FIXME m_Id*/, resid, 1024, 768, 0, 0);
		//    m_CtrlQueue.TransferToHost2D(resid, 0, 1024, 768, 0, 0, NULL);
		//    m_CtrlQueue.ResFlush(resid, 1024, 768, 0, 0);
		//}
		//m_CtrlQueue.SetScanout(0/*FIXME m_Id*/, resid, 1024, 768, 0, 0);
		m_CtrlQueue.InvalBacking(resid);
		m_CtrlQueue.UnrefResource(resid);
		if (bReset == TRUE) {
			m_CtrlQueue.SetScanout(0/*FIXME m_Id*/, 0, 0, 0, 0, 0);
		}
		delete m_screen[screen_num].m_pFrameBuf;
		m_screen[screen_num].m_pFrameBuf = NULL;
		m_Idr.PutId(resid);
	}
}

BOOLEAN VioGpuAdapterLite::CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pCurrentMode)
{
	UINT resid, format, size;
	VioGpuObj* obj;
	PAGED_CODE();
	TRACING();
	UNREFERENCED_PARAMETER(pCurrentMode);
	DBGPRINT("%d: (%d x %d - %d) (%d + %d)\n", m_Id,
		pSetPointerShape->Width, pSetPointerShape->Height, pSetPointerShape->Pitch, pSetPointerShape->XHot, pSetPointerShape->YHot);
	ASSERT(m_pCursorBuf == NULL);
	size = POINTER_SIZE * POINTER_SIZE * 4;
	format = ColorFormat(pCurrentMode->DispInfo.ColorFormat);
	DBGPRINT("(%x -> %x)\n", pCurrentMode->DispInfo.ColorFormat, format);
	resid = (UINT)m_Idr.GetId();

	if (m_bBlobSupported)
	{
		m_CtrlQueue.CreateResource(resid, format, POINTER_SIZE, POINTER_SIZE);
	}

	obj = new(NonPagedPoolNx) VioGpuObj();
	if (!obj->Init(size, &m_CursorSegment))
	{
		VioGpuDbgBreak();
		ERR("Failed to init obj size = %d\n", size);
		delete obj;
		return FALSE;
	}
	if (!GpuObjectAttach(resid, obj, POINTER_SIZE, POINTER_SIZE))
	{
		VioGpuDbgBreak();
		ERR("Failed to attach gpu object\n");
		delete obj;
		return FALSE;
	}

	m_pCursorBuf = obj;

	RECT Rect;
	Rect.left = 0;
	Rect.top = 0;
	Rect.right = Rect.left + pSetPointerShape->Width;
	Rect.bottom = Rect.top + pSetPointerShape->Height;

	BLT_INFO DstBltInfo;
	DstBltInfo.pBits = m_pCursorBuf->GetVirtualAddress();
	DstBltInfo.Pitch = POINTER_SIZE * 4;
	DstBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
	DstBltInfo.Offset.x = 0;
	DstBltInfo.Offset.y = 0;
	DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
	DstBltInfo.Width = POINTER_SIZE;
	DstBltInfo.Height = POINTER_SIZE;

	BLT_INFO SrcBltInfo;
	SrcBltInfo.pBits = (PVOID)pSetPointerShape->pPixels;
	SrcBltInfo.Pitch = pSetPointerShape->Pitch;
	if (pSetPointerShape->Flags.Color) {
		SrcBltInfo.BitsPerPel = BPPFromPixelFormat(D3DDDIFMT_A8R8G8B8);
	}
	else {
		VioGpuDbgBreak();
		ERR("Invalid cursor color %d\n", pSetPointerShape->Flags.Value);
		return FALSE;
	}
	SrcBltInfo.Offset.x = 0;
	SrcBltInfo.Offset.y = 0;
	SrcBltInfo.Rotation = pCurrentMode->Rotation;
	SrcBltInfo.Width = pSetPointerShape->Width;
	SrcBltInfo.Height = pSetPointerShape->Height;

	BltBits(&DstBltInfo,
		&SrcBltInfo,
		1,
		&Rect);

	m_CtrlQueue.TransferToHost2D(resid, 0, pSetPointerShape->Width, pSetPointerShape->Height, 0, 0, NULL);
	return TRUE;
}

void VioGpuAdapterLite::DestroyCursor()
{
	PAGED_CODE();
	TRACING();
	if (m_pCursorBuf != NULL)
	{
		UINT id = (UINT)m_pCursorBuf->GetId();
		m_CtrlQueue.InvalBacking(id);
		m_CtrlQueue.UnrefResource(id);
		delete m_pCursorBuf;
		m_pCursorBuf = NULL;
		m_Idr.PutId(id);
	}
}

BOOLEAN VioGpuAdapterLite::GpuObjectAttach(UINT res_id, VioGpuObj* obj, ULONGLONG width, ULONGLONG height)
{
	PAGED_CODE();
	TRACING();
	PGPU_MEM_ENTRY ents = NULL;
	PSCATTER_GATHER_LIST sgl = NULL;
	UINT size = 0;
	sgl = obj->GetSGList();
	size = sizeof(GPU_MEM_ENTRY) * sgl->NumberOfElements;
	ents = reinterpret_cast<PGPU_MEM_ENTRY> (new (NonPagedPoolNx)  BYTE[size]);

	if (!ents)
	{
		ERR("cannot allocate memory %x bytes numberofentries = %d\n", size, sgl->NumberOfElements);
		return FALSE;
	}
	//FIXME
	RtlZeroMemory(ents, size);

	for (UINT i = 0; i < sgl->NumberOfElements; i++)
	{
		ents[i].addr = sgl->Elements[i].Address.QuadPart;
		ents[i].length = sgl->Elements[i].Length;
		ents[i].padding = 0;
	}

	if (m_bBlobSupported) {
		m_CtrlQueue.CreateResourceBlob(res_id, ents, sgl->NumberOfElements, width, height);
	}
	else {
		m_CtrlQueue.AttachBacking(res_id, ents, sgl->NumberOfElements);
	}

	obj->SetId(res_id);
	return TRUE;
}

VOID VioGpuAdapterLite::SetEvent(HANDLE event)
{
	NTSTATUS status;
	/* If the UMD has provided us with an event and we don't have it initialized already */
	if (event && !hpd_event) {
		status = ObReferenceObjectByHandle(event, SYNCHRONIZE | EVENT_MODIFY_STATE,
			*ExEventObjectType, UserMode, (PVOID*)&hpd_event, NULL);
		if (status != STATUS_SUCCESS) {
			ERR("Couldn't retrieve event from handle. Error is %d\n", status);
		}
	}
}

VOID VioGpuAdapterLite::FillPresentStatus(struct hp_info* info)
{
	for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
		info->screen_present[i] = m_screen[i].enabled;
	}
}

PAGED_CODE_SEG_END

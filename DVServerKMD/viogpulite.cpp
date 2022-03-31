/*
 * Copyright © 2021 Intel Corporation 
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
#include "edid.h"

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
VioGpuAdapterLite::VioGpuAdapterLite(_In_ PVOID pvDeviceContext) : IVioGpuAdapterLite(pvDeviceContext)
{
    PAGED_CODE();

    m_ModeInfo = NULL;
    m_ModeCount = 0;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_Id = g_InstanceId++;
    m_pFrameBuf = NULL;
    m_pCursorBuf = NULL;
    m_PendingWorks = 0;
    KeInitializeEvent(&m_ConfigUpdateEvent,
        SynchronizationEvent,
        FALSE);
    m_bStopWorkThread = FALSE;
    m_pWorkThread = NULL;
    m_bBlobSupported = FALSE;
}

VioGpuAdapterLite::~VioGpuAdapterLite(void)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s 0x%p\n", __FUNCTION__, this));
    DestroyCursor();
    DestroyFrameBufferObj(TRUE);
    VioGpuAdapterLiteClose();
    HWClose();
    delete[] m_ModeInfo;
    delete[] m_ModeNumbers;
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;
    m_CurrentMode = 0;
    m_CustomMode = 0;
    m_ModeCount = 0;
    m_Id = 0;
    g_InstanceId--;
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapterLite::SetCurrentMode(ULONG Mode, CURRENT_MODE* pCurrentMode)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("---> %s - %d: Mode = %d\n", __FUNCTION__, m_Id, Mode));
    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        if (Mode == m_ModeNumbers[idx])
        {
            if (pCurrentMode->Flags.FrameBufferIsActive) {
                DestroyFrameBufferObj(FALSE);
                pCurrentMode->Flags.FrameBufferIsActive = FALSE;
            }
            CreateFrameBufferObj(&m_ModeInfo[idx], pCurrentMode);
            DbgPrintAdopt(TRACE_LEVEL_ERROR, ("%s device %d: setting current mode %d (%d x %d)\n",
                __FUNCTION__, m_Id, Mode, m_ModeInfo[idx].VisScreenWidth,
                m_ModeInfo[idx].VisScreenHeight));
            return STATUS_SUCCESS;
        }
    }
    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s failed\n", __FUNCTION__));
    return STATUS_UNSUCCESSFUL;
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
    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("---> %s - %d: Mode = %d\n", __FUNCTION__, m_Id, Mode));

    if (!pCurrentMode)
        return status;

    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        if (!IsSameMode(&m_ModeInfo[idx], pCurrentMode))
            continue;

        DestroyFrameBufferObj(FALSE);
        CreateFrameBufferObj(&m_ModeInfo[idx], pCurrentMode);
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("%s device %d: setting current mode %d (%d x %d)\n",
            __FUNCTION__, m_Id, Mode, m_ModeInfo[idx].VisScreenWidth,
            m_ModeInfo[idx].VisScreenHeight));
        status = STATUS_SUCCESS;
        break;
    }

    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s failed\n", __FUNCTION__));
    return status;
}

NTSTATUS VioGpuAdapterLite::VioGpuAdapterLiteInit(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();
    NTSTATUS status = STATUS_SUCCESS;

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UNREFERENCED_PARAMETER(pDispInfo);
    if (IsHardwareInit()) {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Already Initialized\n"));
        VioGpuDbgBreak();
        return status;
    }
    status = VirtIoDeviceInit();
    if (!NT_SUCCESS(status)) {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Failed to initialize virtio device, error %x\n", status));
        VioGpuDbgBreak();
        return status;
    }

    m_u64HostFeatures = virtio_get_features(&m_VioDev);
    m_u64GuestFeatures = 0;
    do
    {
        struct virtqueue *vqs[2];
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
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s virtio_set_features failed with %x\n", __FUNCTION__, status));
            VioGpuDbgBreak();
            break;
        }

        status = virtio_find_queues(
            &m_VioDev,
            2,
            vqs);
        if (!NT_SUCCESS(status)) {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("virtio_find_queues failed with error %x\n", status));
            VioGpuDbgBreak();
            break;
        }

        if (!m_CtrlQueue.Init(&m_VioDev, vqs[0], 0) ||
            !m_CursorQueue.Init(&m_VioDev, vqs[1], 1)) {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Failed to initialize virtio queues\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, num_scanouts),
            &m_u32NumScanouts, sizeof(m_u32NumScanouts));

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


    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

    return status;
}

void VioGpuAdapterLite::VioGpuAdapterLiteClose()
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s\n", __FUNCTION__));

    if (IsHardwareInit())
    {
        SetHardwareInit(FALSE);
        m_CtrlQueue.DisableInterrupt();
        m_CursorQueue.DisableInterrupt();
        virtio_device_reset(&m_VioDev);
        virtio_delete_queues(&m_VioDev);
        m_CtrlQueue.Close();
        m_CursorQueue.Close();
        virtio_device_shutdown(&m_VioDev);
    }
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapterLite::SetPowerState(DEVICE_POWER_STATE DevicePowerState)
{
    PAGED_CODE();

    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s DevicePowerState = %d\n", __FUNCTION__, DevicePowerState));

    switch (DevicePowerState)
    {
    case PowerDeviceUnspecified:
    case PowerDeviceD0: {
        VioGpuAdapterLiteInit(&m_CurrentModeInfo.DispInfo);
    } break;
    case PowerDeviceD1:
    case PowerDeviceD2:
    case PowerDeviceD3: {
        DestroyFrameBufferObj(TRUE);
        VioGpuAdapterLiteClose();
        m_CurrentModeInfo.Flags.FrameBufferIsActive = FALSE;
        m_CurrentModeInfo.FrameBuffer.Ptr = NULL;
    } break;
    }
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s\n", __FUNCTION__));
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

    return virtio_device_initialize(
        &m_VioDev,
        &VioGpuSystemOps,
        this,
        m_PciResources.IsMSIEnabled());
}

PBYTE VioGpuAdapterLite::GetEdidData(UINT Id)
{
    PAGED_CODE();

    return m_bEDID ? m_EDIDs[Id] : (PBYTE)(&g_gpu_edid);//.data;
}

NTSTATUS VioGpuAdapterLite::HWInit(WDFCMRESLIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    HANDLE   threadHandle = 0;
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    UINT size = 0;

    if (!pResList || !pDispInfo) {
        DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s: Invalid arguments!\n", __FUNCTION__));
        return STATUS_UNSUCCESSFUL;
    }

    do
    {
        if (!m_PciResources.Init(this->m_pvDeviceContext, pResList))
        {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Incomplete resources\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        status = VioGpuAdapterLiteInit(pDispInfo);
        if (!NT_SUCCESS(status))
        {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s Failed initialize adapter %x\n", __FUNCTION__, status));
            VioGpuDbgBreak();
            break;
        }

        size = m_CtrlQueue.QueryAllocation() + m_CursorQueue.QueryAllocation();
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s size %d\n", __FUNCTION__, size));
        ASSERT(size);

        if (!m_GpuBuf.Init(size)) {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Failed to initialize buffers\n"));
            status = STATUS_INSUFFICIENT_RESOURCES;
            VioGpuDbgBreak();
            break;
        }

        m_CtrlQueue.SetGpuBuf(&m_GpuBuf);
        m_CursorQueue.SetGpuBuf(&m_GpuBuf);

        if (!m_Idr.Init(1)) {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("Failed to initialize id generator\n"));
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
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s failed to create system thread, status %x\n", __FUNCTION__, status));
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
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s GetModeList failed with %x\n", __FUNCTION__, status));
        VioGpuDbgBreak();
    }

    PHYSICAL_ADDRESS fb_pa = m_PciResources.GetPciBar(0)->GetPA();
    UINT fb_size = (UINT)m_PciResources.GetPciBar(0)->GetSize();
    UINT req_size = pDispInfo->Pitch * pDispInfo->Height;
    req_size = max(req_size, fb_size);
//FIXME!!! update and validate size properly
    req_size = max(0x800000, req_size);

    if (fb_pa.QuadPart != 0LL) {
        pDispInfo->PhysicAddress = fb_pa;
    }

    if (fb_pa.QuadPart == 0 || fb_size < req_size) {
        fb_pa.QuadPart = 0;
        fb_size = req_size;
    }

    if (!m_FrameSegment.Init(req_size, NULL))
    {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s failed to allocate FB memory segment\n", __FUNCTION__));
        status = STATUS_INSUFFICIENT_RESOURCES;
        VioGpuDbgBreak();
        return status;
    }

    if (!m_CursorSegment.Init(POINTER_SIZE * POINTER_SIZE * 4, NULL))
    {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s failed to allocate Cursor memory segment\n", __FUNCTION__));
        status = STATUS_INSUFFICIENT_RESOURCES;
        VioGpuDbgBreak();
        return status;
    }

    return status;
}

NTSTATUS VioGpuAdapterLite::HWClose(void)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
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
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> Failed to exit the worker thread\n"));
        VioGpuDbgBreak();
    }

    ObDereferenceObject(m_pWorkThread);

    m_FrameSegment.Close();
    m_CursorSegment.Close();

    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("<--- %s\n", __FUNCTION__));

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

NTSTATUS VioGpuAdapterLite::ExecutePresentDisplayOnly(
    _In_ BYTE*             DstAddr,
    _In_ UINT              DstBitPerPixel,
    _In_ BYTE*             SrcAddr,
    _In_ UINT              SrcBytesPerPixel,
    _In_ LONG              SrcPitch,
    _In_ ULONG             NumMoves,
    _In_ D3DKMT_MOVE_RECT* pMoves,
    _In_ ULONG             NumDirtyRects,
    _In_ RECT*             pDirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _In_ const CURRENT_MODE* pModeCur)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    BLT_INFO SrcBltInfo = { 0 };
    BLT_INFO DstBltInfo = { 0 };
    UINT resid = 0;
    RECT updrect = { 0 };
    ULONG offset = 0UL;

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n",
        SrcBytesPerPixel, DstBitPerPixel, pModeCur->SrcModeWidth, pModeCur->SrcModeHeight));

    DstBltInfo.pBits = DstAddr;
    DstBltInfo.Pitch = pModeCur->DispInfo.Pitch;
    DstBltInfo.BitsPerPel = DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Rotation;
    DstBltInfo.Width = pModeCur->SrcModeWidth;
    DstBltInfo.Height = pModeCur->SrcModeHeight;

    SrcBltInfo.pBits = SrcAddr;
    SrcBltInfo.Pitch = SrcPitch;
    SrcBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Rotation == D3DKMDT_VPPR_ROTATE90 ||
        Rotation == D3DKMDT_VPPR_ROTATE270)
    {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    }
    else
    {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    for (UINT i = 0; i < NumMoves; i++)
    {
        RECT*  pDestRect = &pMoves[i].DestRect;
        BltBits(&DstBltInfo,
            &SrcBltInfo,
            1,
            pDestRect);
    }

    for (UINT i = 0; i < NumDirtyRects; i++)
    {
        RECT*  pRect = &pDirtyRect[i];
        BltBits(&DstBltInfo,
            &SrcBltInfo,
            1,
            pRect);
    }
    if (!FindUpdateRect(NumMoves, pMoves, NumDirtyRects, pDirtyRect, Rotation, &updrect))
    {
        updrect.top = 0;
        updrect.left = 0;
        updrect.bottom = pModeCur->SrcModeHeight;
        updrect.right = pModeCur->SrcModeWidth;
        offset = 0UL;
    }
//FIXME!!! rotation
    offset = (updrect.top * pModeCur->DispInfo.Pitch) + (updrect.left * ((DstBitPerPixel + BITS_PER_BYTE - 1) / BITS_PER_BYTE));

    resid = m_pFrameBuf->GetId();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
        offset,
        updrect.left,
        updrect.top,
        updrect.right - updrect.left,
        updrect.bottom - updrect.top,
        0,
        0,
        pModeCur->SrcModeWidth,
        pModeCur->SrcModeHeight));

    m_CtrlQueue.TransferToHost2D(resid, offset, updrect.right - updrect.left, updrect.bottom - updrect.top, updrect.left, updrect.top, NULL);
    m_CtrlQueue.ResFlush(resid, updrect.right - updrect.left, updrect.bottom - updrect.top, updrect.left, updrect.top, &m_FlushEvent);

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapterLite::ExecutePresentDisplayFallback(
    _In_ BYTE*              SrcAddr,
    _In_ UINT               SrcBytesPerPixel,
    _In_ LONG               SrcPitch,
    _In_ UINT               SrcWidth,
    _In_ UINT               SrcHeight)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    BLT_INFO SrcBltInfo = { 0 };
    BLT_INFO DstBltInfo = { 0 };
    UINT resid = 0;
    RECT rect = { 0 };
    ULONG offset = 0;

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n",
        SrcBytesPerPixel, DstBitPerPixel, pModeCur->SrcModeWidth, pModeCur->SrcModeHeight));

    DstBltInfo.pBits = m_FrameSegment.GetFbVAddr();
    DstBltInfo.Pitch = SrcPitch;
    DstBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    DstBltInfo.Width = SrcWidth;
    DstBltInfo.Height = SrcHeight;

    SrcBltInfo.pBits = SrcAddr;
    SrcBltInfo.Pitch = SrcPitch;
    SrcBltInfo.BitsPerPel = SrcBytesPerPixel * BITS_PER_BYTE;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    SrcBltInfo.Width = DstBltInfo.Width;
    SrcBltInfo.Height = DstBltInfo.Height;

    rect.left = 0;
    rect.top = 0;
    rect.right = SrcWidth;
    rect.bottom = SrcHeight;

    BltBits(&DstBltInfo, &SrcBltInfo, 1, &rect);

    resid = m_pFrameBuf->GetId();

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
        offset,
        rect.left,
        rect.top,
        SrcWidth,
        SrcWidth,
        0,
        0,
        SrcWidth,
        SrcHeight));

    m_CtrlQueue.TransferToHost2D(resid, offset, SrcWidth, SrcHeight, rect.left, rect.top, NULL);
    m_CtrlQueue.ResFlush(resid, SrcWidth, SrcHeight, rect.left, rect.top, &m_FlushEvent);

    return STATUS_SUCCESS;
}

NTSTATUS VioGpuAdapterLite::ExecutePresentDisplayZeroCopy(
    _In_ BYTE*              SrcAddr,
    _In_ UINT               SrcBytesPerPixel,
    _In_ LONG               SrcPitch,
    _In_ UINT               SrcWidth,
    _In_ UINT               SrcHeight)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    BLT_INFO SrcBltInfo = { 0 };
    BLT_INFO DstBltInfo = { 0 };
    RECT rect = { 0 };

    UNREFERENCED_PARAMETER(SrcBytesPerPixel);
    UNREFERENCED_PARAMETER(SrcPitch);

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("SrcBytesPerPixel = %d DstBitPerPixel = %d (%dx%d)\n",
        SrcBytesPerPixel, DstBitPerPixel, pModeCur->SrcModeWidth, pModeCur->SrcModeHeight));

    CURRENT_MODE tempCurrentMode = { 0 };
    tempCurrentMode.DispInfo.Width = SrcWidth;
    tempCurrentMode.DispInfo.Height = SrcHeight;
    tempCurrentMode.DispInfo.Pitch = SrcPitch;
    tempCurrentMode.DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
    tempCurrentMode.FrameBuffer.Ptr = SrcAddr;

    SetCurrentModeExt(&tempCurrentMode);

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("offset = %lu (XxYxWxH) (%dx%dx%dx%d) vs (%dx%dx%dx%d)\n",
        offset,
        rect.left,
        rect.top,
        SrcWidth,
        SrcWidth,
        0,
        0,
        SrcWidth,
        SrcHeight));

    m_FrameSegment.Close();

    return STATUS_SUCCESS;
}

VOID VioGpuAdapterLite::BlackOutScreen(CURRENT_MODE* pCurrentMod)
{
    PAGED_CODE();

    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));

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

        resid = m_pFrameBuf->GetId();

        m_CtrlQueue.TransferToHost2D(resid, 0UL, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0, NULL);
        m_CtrlQueue.ResFlush(resid, pCurrentMod->DispInfo.Width, pCurrentMod->DispInfo.Height, 0, 0, &m_FlushEvent);
    }

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

NTSTATUS VioGpuAdapterLite::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur)
{
    PAGED_CODE();

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("<--> %s flag = %d pitch = %d, pixels = %p, id = %d, w = %d, h = %d, x = %d, y = %d\n", __FUNCTION__,
        pSetPointerShape->Flags.Value,
        pSetPointerShape->Pitch,
        pSetPointerShape->pPixels,
        pSetPointerShape->VidPnSourceId,
        pSetPointerShape->Width,
        pSetPointerShape->Height,
        pSetPointerShape->XHot,
        pSetPointerShape->YHot));

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
        DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
        if (ret == 0) {
            return STATUS_SUCCESS;
        }
        VioGpuDbgBreak();
    }
    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s Failed to create cursor\n", __FUNCTION__));
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
            DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d\n",
                __FUNCTION__,
                pSetPointerPosition->X,
                pSetPointerPosition->Y,
                pSetPointerPosition->Flags.Visible,
                pSetPointerPosition->Flags.Value,
                pSetPointerPosition->VidPnSourceId));
            crsr->pos.x = 0;
            crsr->pos.y = 0;
        }
        else {
            DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s (%d - %d) Visiable = %d Value = %x VidPnSourceId = %d posX = %d, psY = %d\n",
                __FUNCTION__,
                pSetPointerPosition->X,
                pSetPointerPosition->Y,
                pSetPointerPosition->Flags.Visible,
                pSetPointerPosition->Flags.Value,
                pSetPointerPosition->VidPnSourceId,
                pSetPointerPosition->X,
                pSetPointerPosition->Y));
            crsr->pos.x = pSetPointerPosition->X;
            crsr->pos.y = pSetPointerPosition->Y;
        }
        ret = m_CursorQueue.QueueCursor(vbuf);
        DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s vbuf = %p, ret = %d\n", __FUNCTION__, vbuf, ret));
        if (ret == 0) {
            return STATUS_SUCCESS;
        }
        VioGpuDbgBreak();
    }
    return STATUS_UNSUCCESSFUL;
}

BOOLEAN VioGpuAdapterLite::GetDisplayInfo(void)
{
    PAGED_CODE();

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf = NULL;
    ULONG xres = 0;
    ULONG yres = 0;

    for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
        if (m_CtrlQueue.AskDisplayInfo(&vbuf, &m_DisplayInfoEvent)) {
            m_CtrlQueue.GetDisplayInfo(vbuf, i, &xres, &yres);
            m_CtrlQueue.ReleaseBuffer(vbuf);
            if (xres && yres) {
                DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s (%dx%d)\n", __FUNCTION__, xres, yres));
                SetCustomDisplay((USHORT)xres, (USHORT)yres);
            }
        }
    }
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuAdapterLite::ProcessEdid(void)
{
    PAGED_CODE();

    if (virtio_is_feature_enabled(m_u64HostFeatures, VIRTIO_GPU_F_EDID)) {
        GetEdids();
        AddEdidModes();
    }
    else {
        return;
    }
}

BOOLEAN VioGpuAdapterLite::GetEdids(void)
{
    PAGED_CODE();

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    PGPU_VBUFFER vbuf = NULL;

    for (UINT32 i = 0; i < m_u32NumScanouts; i++) {
        if (m_CtrlQueue.AskEdidInfo(&vbuf, i, &m_EdidEvent) &&
            m_CtrlQueue.GetEdidInfo(vbuf, i, m_EDIDs[i])) {
            m_bEDID = TRUE;
        }
        m_CtrlQueue.ReleaseBuffer(vbuf);
    }

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
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

GPU_DISP_MODE_EXT gpu_disp_mode_ext[MAX_MODELIST_SIZE] = { 0 };
output_modelist mode_list = { 0 };

void VioGpuAdapterLite::AddEdidModes(void)
{
    PAGED_CODE();
    if (parse_edid_data(GetEdidData(0), &mode_list) != 0) {
        for (unsigned int i = 0; i < QEMU_MODELIST_SIZE; i++) {
            gpu_disp_mode_ext[i].XResolution = (USHORT)qemu_modelist[i].x;
            gpu_disp_mode_ext[i].YResolution = (USHORT)qemu_modelist[i].y;
            gpu_disp_mode_ext[i].refresh = qemu_modelist[i].rr;
        }
    }
    else {
        for (unsigned int i = 0; i < QEMU_MODELIST_SIZE; i++) {
            gpu_disp_mode_ext[i].XResolution = (USHORT)mode_list.modelist[i].width;
            gpu_disp_mode_ext[i].YResolution = (USHORT)mode_list.modelist[i].height;
            gpu_disp_mode_ext[i].refresh = mode_list.modelist[i].refresh_rate;
        }
    }
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}


void VioGpuAdapterLite::SetVideoModeInfo(UINT Idx, PGPU_DISP_MODE_EXT pModeInfo)
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

// TODO: Remove following if client changes not handled
#if 0
NTSTATUS VioGpuAdapterLite::UpdateChildStatus(BOOLEAN connect)
{
    PAGED_CODE();
    NTSTATUS           Status(STATUS_SUCCESS);
    DXGK_CHILD_STATUS  ChildStatus;
    PDXGKRNL_INTERFACE pDXGKInterface(m_pVioGpuDod->GetDxgkInterface());

    RtlZeroMemory(&ChildStatus, sizeof(ChildStatus));

    ChildStatus.Type = StatusConnection;
    ChildStatus.ChildUid = 0;
    ChildStatus.HotPlug.Connected = connect;
    Status = pDXGKInterface->DxgkCbIndicateChildStatus(pDXGKInterface->DeviceHandle, &ChildStatus);
    if (Status != STATUS_SUCCESS)
    {
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s DxgkCbIndicateChildStatus failed with status %x\n ", __FUNCTION__, Status));
    }
    return Status;
}
#endif

void VioGpuAdapterLite::SetCustomDisplay(_In_ USHORT xres, _In_ USHORT yres)
{
    PAGED_CODE();

    GPU_DISP_MODE_EXT tmpModeInfo = { 0 };

    if (xres < MIN_WIDTH_SIZE || yres < MIN_HEIGHT_SIZE) {
        DbgPrintAdopt(TRACE_LEVEL_WARNING, ("%s: (%dx%d) less than (%dx%d)\n", __FUNCTION__,
            xres, yres, MIN_WIDTH_SIZE, MIN_HEIGHT_SIZE));
    }
    tmpModeInfo.XResolution = max(MIN_WIDTH_SIZE, xres);
    tmpModeInfo.YResolution = max(MIN_HEIGHT_SIZE, yres);

    m_CustomMode = (USHORT)((m_CustomMode == m_ModeCount - 1) ? m_ModeCount - 2 : m_ModeCount - 1);

    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("%s - %d (%dx%d)\n", __FUNCTION__, m_CustomMode, tmpModeInfo.XResolution, tmpModeInfo.YResolution));

    SetVideoModeInfo(m_CustomMode, &tmpModeInfo);
}

NTSTATUS VioGpuAdapterLite::GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo)
{
    PAGED_CODE();

    NTSTATUS Status = STATUS_SUCCESS;

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));

    UINT ModeCount = 0;
    delete[] m_ModeInfo;
    delete[] m_ModeNumbers;
    m_ModeInfo = NULL;
    m_ModeNumbers = NULL;

    ProcessEdid();
    while ((gpu_disp_mode_ext[ModeCount].XResolution >= MIN_WIDTH_SIZE) &&
        (gpu_disp_mode_ext[ModeCount].YResolution >= MIN_HEIGHT_SIZE)) ModeCount++;

    ModeCount += 2;
    m_ModeInfo = new (PagedPool) VIDEO_MODE_INFORMATION[ModeCount];
    if (!m_ModeInfo)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("VioGpuAdapterLite::GetModeList failed to allocate m_ModeInfo memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeInfo, sizeof(VIDEO_MODE_INFORMATION) * ModeCount);

    m_ModeNumbers = new (PagedPool) USHORT[ModeCount];
    if (!m_ModeNumbers)
    {
        Status = STATUS_NO_MEMORY;
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("VioGpuAdapterLite::GetModeList failed to allocate m_ModeNumbers memory\n"));
        return Status;
    }
    RtlZeroMemory(m_ModeNumbers, sizeof(USHORT) * ModeCount);
    m_CurrentMode = 0;
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("m_ModeInfo = 0x%p, m_ModeNumbers = 0x%p\n", m_ModeInfo, m_ModeNumbers));

    pDispInfo->Height = max(pDispInfo->Height, MIN_HEIGHT_SIZE);
    pDispInfo->Width = max(pDispInfo->Width, MIN_WIDTH_SIZE);
    pDispInfo->ColorFormat = D3DDDIFMT_X8R8G8B8;
    pDispInfo->Pitch = (BPPFromPixelFormat(pDispInfo->ColorFormat) / BITS_PER_BYTE) * 	pDispInfo->Width;

    USHORT SuitableModeCount;
    USHORT CurrentMode;

    for (CurrentMode = 0, SuitableModeCount = 0;
        CurrentMode < ModeCount - 2;
        CurrentMode++)
    {

        PGPU_DISP_MODE_EXT tmpModeInfo = &gpu_disp_mode_ext[CurrentMode];

        DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("%s: modes[%d] x_res = %d, y_res = %d\n",
            __FUNCTION__, CurrentMode, tmpModeInfo->XResolution, tmpModeInfo->YResolution));

        if (tmpModeInfo->XResolution >= pDispInfo->Width &&
            tmpModeInfo->YResolution >= pDispInfo->Height)
        {
            m_ModeNumbers[SuitableModeCount] = SuitableModeCount;
            SetVideoModeInfo(SuitableModeCount, tmpModeInfo);
            if (tmpModeInfo->XResolution == NOM_WIDTH_SIZE &&
                tmpModeInfo->YResolution == NOM_HEIGHT_SIZE)
            {
                m_CurrentMode = SuitableModeCount;
            }
            SuitableModeCount++;
        }
    }

    if (SuitableModeCount == 0)
    {
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("No video modes supported\n"));
        Status = STATUS_UNSUCCESSFUL;
    }

    m_CustomMode = SuitableModeCount;
    for (CurrentMode = SuitableModeCount;
        CurrentMode < SuitableModeCount + 2;
        CurrentMode++)
    {
        m_ModeNumbers[CurrentMode] = CurrentMode;
        memcpy(&m_ModeInfo[CurrentMode], &m_ModeInfo[m_CurrentMode], sizeof(VIDEO_MODE_INFORMATION));
    }

    m_ModeCount = SuitableModeCount + 2;
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("ModeCount filtered %d\n", m_ModeCount));

    GetDisplayInfo();

    for (ULONG idx = 0; idx < GetModeCount(); idx++)
    {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("type %x, XRes = %d, YRes = %d\n",
            m_ModeNumbers[idx],
            m_ModeInfo[idx].VisScreenWidth,
            m_ModeInfo[idx].VisScreenHeight));
    }


    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return Status;
}
PAGED_CODE_SEG_END

BOOLEAN VioGpuAdapterLite::InterruptRoutine(_In_  ULONG MessageNumber)
{
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s MessageNumber = %d\n", __FUNCTION__, MessageNumber));
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
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s Unknown Interrupt Reason MessageNumber%d\n", __FUNCTION__, MessageNumber));
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
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s Unknown Interrupt Reason MessageNumber%d\n", __FUNCTION__, isrstat));
        }
    }


    if (serviced) {
        InterlockedOr((PLONG)&m_PendingWorks, intReason);
        WdfInterruptQueueDpcForIsr(pDevcieContext->WdfInterrupt);
    }

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));

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
    DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--> %s\n", __FUNCTION__));
    UINT32 events_read, events_clear = 0;
    virtio_get_config(&m_VioDev, FIELD_OFFSET(GPU_CONFIG, events_read),
        &events_read, sizeof(m_u32NumScanouts));
    if (events_read & VIRTIO_GPU_EVENT_DISPLAY) {
        GetDisplayInfo();
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
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_VBUFFER pvbuf = NULL;
    UINT len = 0;
    ULONG reason;
    while ((reason = InterlockedExchange((PLONG)&m_PendingWorks, 0)) != 0)
    {
        if ((reason & ISR_REASON_DISPLAY)) {
            while ((pvbuf = m_CtrlQueue.DequeueBuffer(&len)) != NULL)
            {
                DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s m_CtrlQueue pvbuf = %p len = %d\n", __FUNCTION__, pvbuf, len));
                PGPU_CTRL_HDR pcmd = (PGPU_CTRL_HDR)pvbuf->buf;
                PGPU_CTRL_HDR resp = (PGPU_CTRL_HDR)pvbuf->resp_buf;
                PKEVENT evnt = pvbuf->event;
                if (evnt == NULL)
                {
                    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA)
                    {
                        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s type = %xlu flags = %lu fence_id = %llu ctx_id = %lu cmd_type = %lu\n",
                            __FUNCTION__, resp->type, resp->flags, resp->fence_id, resp->ctx_id, pcmd->type));
                    }
                    m_CtrlQueue.ReleaseBuffer(pvbuf);
                    continue;
                }
                switch (pcmd->type)
                {
                case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
                case VIRTIO_GPU_CMD_GET_EDID:
                case VIRTIO_GPU_CMD_RESOURCE_FLUSH:
                {
                    ASSERT(evnt);
                    KeSetEvent(evnt, IO_NO_INCREMENT, FALSE);
                }
                break;
                default:
                    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s Unknown cmd type 0x%x\n", __FUNCTION__, resp->type));
                    break;
                }
            };
        }
        if ((reason & ISR_REASON_CURSOR)) {
            while ((pvbuf = m_CursorQueue.DequeueCursor(&len)) != NULL)
            {
                DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s m_CursorQueue pvbuf = %p len = %u\n", __FUNCTION__, pvbuf, len));
                m_CursorQueue.ReleaseBuffer(pvbuf);
            };
        }
        if (reason & ISR_REASON_CHANGE) {
            DbgPrintAdopt(TRACE_LEVEL_FATAL, ("---> %s ConfigChanged\n", __FUNCTION__));
            KeSetEvent(&m_ConfigUpdateEvent, IO_NO_INCREMENT, FALSE);
        }
    }
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

VOID VioGpuAdapterLite::ResetDevice(VOID)
{
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s\n", __FUNCTION__));
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
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
    DbgPrintAdopt(TRACE_LEVEL_ERROR, ("---> %s Unsupported color format %d\n", __FUNCTION__, format));
    return VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
}

PAGED_CODE_SEG_BEGIN
void VioGpuAdapterLite::CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode)
{
    UINT resid, format, size;
    VioGpuObj* obj;
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s - %d: (%d x %d)\n", __FUNCTION__, m_Id,
        pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight));
    ASSERT(m_pFrameBuf == NULL);
    size = pModeInfo->ScreenStride * pModeInfo->VisScreenHeight;
    format = ColorFormat(pCurrentMode->DispInfo.ColorFormat);
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s - (%d -> %d)\n", __FUNCTION__, pCurrentMode->DispInfo.ColorFormat, format));
    resid = m_Idr.GetId();

    if (!m_bBlobSupported) {
        m_CtrlQueue.CreateResource(resid, format, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);
    }

    // Update the frame segment based on the current mode
    if (pCurrentMode->FrameBuffer.Ptr) {
        m_FrameSegment.InitExt(size, pCurrentMode->FrameBuffer.Ptr);
    } else if (m_FrameSegment.GetFbVAddr() && size > m_FrameSegment.GetSize()) {
        m_FrameSegment.Close();
        m_FrameSegment.Init(size, NULL);
    }

    obj = new(NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_FrameSegment))
    {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return;
    }

    GpuObjectAttach(resid, obj, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight);

    if (m_bBlobSupported)
    {
        m_CtrlQueue.SetScanoutBlob(0/*FIXME m_Id*/, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, format, 0, 0);
    }
    else
    {
        m_CtrlQueue.SetScanout(0/*FIXME m_Id*/, resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0);
    }
    m_CtrlQueue.TransferToHost2D(resid, 0, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0, NULL);
    m_CtrlQueue.ResFlush(resid, pModeInfo->VisScreenWidth, pModeInfo->VisScreenHeight, 0, 0, &m_FlushEvent);
    m_pFrameBuf = obj;
    pCurrentMode->FrameBuffer.Ptr = obj->GetVirtualAddress();
    pCurrentMode->Flags.FrameBufferIsActive = TRUE;
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

void VioGpuAdapterLite::DestroyFrameBufferObj(BOOLEAN bReset)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    UINT resid = 0;

    if (m_pFrameBuf != NULL)
    {
        resid = (UINT)m_pFrameBuf->GetId();
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
        delete m_pFrameBuf;
        m_pFrameBuf = NULL;
        m_Idr.PutId(resid);
    }
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapterLite::CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pCurrentMode)
{
    UINT resid, format, size;
    VioGpuObj* obj;
    PAGED_CODE();
    UNREFERENCED_PARAMETER(pCurrentMode);
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s - %d: (%d x %d - %d) (%d + %d)\n", __FUNCTION__, m_Id,
        pSetPointerShape->Width, pSetPointerShape->Height, pSetPointerShape->Pitch, pSetPointerShape->XHot, pSetPointerShape->YHot));
    ASSERT(m_pCursorBuf == NULL);
    size = POINTER_SIZE * POINTER_SIZE * 4;
    format = ColorFormat(pCurrentMode->DispInfo.ColorFormat);
    DbgPrintAdopt(TRACE_LEVEL_INFORMATION, ("---> %s - (%x -> %x)\n", __FUNCTION__, pCurrentMode->DispInfo.ColorFormat, format));
    resid = (UINT)m_Idr.GetId();

    if (m_bBlobSupported)
    {
        m_CtrlQueue.CreateResource(resid, format, POINTER_SIZE, POINTER_SIZE);
    }

    obj = new(NonPagedPoolNx) VioGpuObj();
    if (!obj->Init(size, &m_CursorSegment))
    {
        VioGpuDbgBreak();
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s Failed to init obj size = %d\n", __FUNCTION__, size));
        delete obj;
        return FALSE;
    }
    if (!GpuObjectAttach(resid, obj, POINTER_SIZE, POINTER_SIZE))
    {
        VioGpuDbgBreak();
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s Failed to attach gpu object\n", __FUNCTION__));
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
        DbgPrintAdopt(TRACE_LEVEL_ERROR, ("<--- %s Invalid cursor color %d\n", __FUNCTION__, pSetPointerShape->Flags.Value));
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

    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

void VioGpuAdapterLite::DestroyCursor()
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    if (m_pCursorBuf != NULL)
    {
        UINT id = (UINT)m_pCursorBuf->GetId();
        m_CtrlQueue.InvalBacking(id);
        m_CtrlQueue.UnrefResource(id);
        delete m_pCursorBuf;
        m_pCursorBuf = NULL;
        m_Idr.PutId(id);
    }
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
}

BOOLEAN VioGpuAdapterLite::GpuObjectAttach(UINT res_id, VioGpuObj* obj, ULONGLONG width, ULONGLONG height)
{
    PAGED_CODE();
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("---> %s\n", __FUNCTION__));
    PGPU_MEM_ENTRY ents = NULL;
    PSCATTER_GATHER_LIST sgl = NULL;
    UINT size = 0;
    sgl = obj->GetSGList();
    size = sizeof(GPU_MEM_ENTRY) * sgl->NumberOfElements;
    ents = reinterpret_cast<PGPU_MEM_ENTRY> (new (NonPagedPoolNx)  BYTE[size]);

    if (!ents)
    {
        DbgPrintAdopt(TRACE_LEVEL_FATAL, ("<--- %s cannot allocate memory %x bytes numberofentries = %d\n", __FUNCTION__, size, sgl->NumberOfElements));
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
    DbgPrintAdopt(TRACE_LEVEL_VERBOSE, ("<--- %s\n", __FUNCTION__));
    return TRUE;
}

PAGED_CODE_SEG_END

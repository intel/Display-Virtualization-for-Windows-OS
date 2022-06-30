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

#pragma once

#include "helper.h"
#pragma pack(push)
#pragma pack(1)

typedef struct
{
    UINT DriverStarted : 1;
    UINT HardwareInit : 1;
    UINT PointerEnabled : 1;
    UINT VgaDevice : 1;
    UINT Unused : 28;
} DEVICE_STATUS_FLAG;

#pragma pack(pop)

typedef struct _CURRENT_MODE
{
    DXGK_DISPLAY_INFORMATION             DispInfo;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION  Rotation;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    UINT SrcModeWidth;
    UINT SrcModeHeight;
    struct _CURRENT_MODE_FLAGS
    {
        UINT SourceNotVisible : 1;
        UINT FullscreenPresent : 1;
        UINT FrameBufferIsActive : 1;
        UINT DoNotMapOrUnmap : 1;
        UINT IsInternal : 1;
        UINT Unused : 27;
    } Flags;

    PHYSICAL_ADDRESS ZeroedOutStart;
    PHYSICAL_ADDRESS ZeroedOutEnd;

    union
    {
        VOID*                            Ptr;
        ULONG64                          Force8Bytes;
    } FrameBuffer;
} CURRENT_MODE;

class IVioGpuAdapterLite {
public:
    IVioGpuAdapterLite(_In_ PVOID pvDevcieContext) { m_pvDeviceContext = pvDevcieContext; m_bEDID = FALSE; }
    virtual ~IVioGpuAdapterLite(void) { ; }
    virtual NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState) = 0;
    virtual NTSTATUS HWInit(WDFCMRESLIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
    virtual NTSTATUS HWClose(void) = 0;
    virtual BOOLEAN InterruptRoutine(_In_  ULONG MessageNumber) = 0;
    virtual VOID DpcRoutine(void) = 0;
    virtual VOID ResetDevice(void) = 0;

    virtual ULONG GetModeCount(void) = 0;
    PVIDEO_MODE_INFORMATION GetModeInfo(UINT idx) { return &m_ModeInfo[idx]; }
    USHORT GetModeNumber(USHORT idx) { return m_ModeNumbers[idx]; }
    USHORT GetCurrentModeIndex(void) { return m_CurrentMode; }
    VOID SetCurrentModeIndex(USHORT idx) { m_CurrentMode = idx; }
    virtual VOID BlackOutScreen(CURRENT_MODE* pCurrentMod) = 0;
    virtual NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur) = 0;
    virtual NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_MODE* pModeCur) = 0;
    ULONG GetInstanceId(void) { return m_Id; }
    PVOID GetVioGpu(void) { return m_pvDeviceContext; }
    virtual PBYTE GetEdidData(UINT Idx) = 0;
    virtual PHYSICAL_ADDRESS GetFrameBufferPA(void) = 0;
protected:
    virtual NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo) = 0;
protected:
    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentMode;
    USHORT m_CustomMode;
    ULONG  m_Id;
    BYTE m_EDIDs[MAX_CHILDREN][EDID_V1_BLOCK_SIZE];
    BOOLEAN m_bEDID;
public:
    PVOID m_pvDeviceContext;
};

class VioGpuAdapterLite :
    public IVioGpuAdapterLite
{
public:
    VioGpuAdapterLite(_In_ PVOID pvDeviceContext);
    ~VioGpuAdapterLite(void);
    NTSTATUS SetCurrentModeExt(CURRENT_MODE* pCurrentMode);
    ULONG GetModeCount(void) { return m_ModeCount; }
    NTSTATUS SetPowerState(DEVICE_POWER_STATE DevicePowerState);
    NTSTATUS HWInit(WDFCMRESLIST pResList, DXGK_DISPLAY_INFORMATION* pDispInfo);
    NTSTATUS HWClose(void);
    NTSTATUS ExecutePresentDisplayZeroCopy(_In_ BYTE* SrcAddr,
        _In_ UINT               SrcBytesPerPixel,
        _In_ LONG               SrcPitch,
        _In_ UINT               SrcWidth,
        _In_ UINT               SrcHeight);
    VOID BlackOutScreen(CURRENT_MODE* pCurrentMod);
    BOOLEAN InterruptRoutine(_In_  ULONG MessageNumber);
    VOID DpcRoutine(void);
    VOID ResetDevice(VOID);
    NTSTATUS SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pModeCur);
    NTSTATUS SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition, _In_ CONST CURRENT_MODE* pModeCur);
    CPciResources* GetPciResources(void) { return &m_PciResources; }
    BOOLEAN IsMSIEnabled() { return m_PciResources.IsMSIEnabled(); }
    PHYSICAL_ADDRESS GetFrameBufferPA(void) { return  m_PciResources.GetPciBar(0)->GetPA(); }
    NTSTATUS IoDeviceControl();
    BOOLEAN IsHardwareInit() const
    {
        return m_Flags.HardwareInit;
    }
private:

    void SetHardwareInit(BOOLEAN init)
    {
        m_Flags.HardwareInit = init;
    }

protected:
private:
    NTSTATUS VioGpuAdapterLiteInit(DXGK_DISPLAY_INFORMATION* pDispInfo);
    void SetVideoModeInfo(UINT Idx, PGPU_DISP_MODE_EXT pModeInfo);
    void VioGpuAdapterLiteClose(void);
    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION* pDispInfo);
    BOOLEAN AckFeature(UINT64 Feature);
    BOOLEAN GetDisplayInfo(void);
    void ProcessEdid(void);
    BOOLEAN GetEdids(void);
    void AddEdidModes(void);
    // Not needed by the DVServerKMD
    //NTSTATUS UpdateChildStatus(BOOLEAN connect);
    void SetCustomDisplay(_In_ USHORT xres,
        _In_ USHORT yres);
    void CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE* pCurrentMode);
    void DestroyFrameBufferObj(BOOLEAN bReset);
    BOOLEAN CreateCursor(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape, _In_ CONST CURRENT_MODE* pCurrentMode);
    void DestroyCursor(void);
    BOOLEAN GpuObjectAttach(UINT res_id, VioGpuObj* obj, ULONGLONG width, ULONGLONG height);
    void static ThreadWork(_In_ PVOID Context);
    void ThreadWorkRoutine(void);
    void ConfigChanged(void);
    NTSTATUS VirtIoDeviceInit(void);
    DEVICE_STATUS_FLAG m_Flags;

private:
    VirtIODevice m_VioDev;
    CPciResources m_PciResources;
    UINT64 m_u64HostFeatures;
    UINT64 m_u64GuestFeatures;
    UINT32 m_u32NumCapsets;
    UINT32 m_u32NumScanouts;
    CtrlQueue m_CtrlQueue;
    CrsrQueue m_CursorQueue;
    VioGpuBuf m_GpuBuf;
    VioGpuIdr m_Idr;
    VioGpuObj* m_pFrameBuf;
    VioGpuObj* m_pCursorBuf;
    VioGpuMemSegment m_CursorSegment;
    volatile ULONG m_PendingWorks;
    KEVENT m_ConfigUpdateEvent;
    KEVENT m_DisplayInfoEvent;
    KEVENT m_EdidEvent;
    KEVENT m_FlushEvent;
    PETHREAD m_pWorkThread;
    BOOLEAN m_bStopWorkThread;
    CURRENT_MODE m_CurrentModeInfo;
    BOOLEAN m_bBlobSupported;
public:
	VioGpuMemSegment m_FrameSegment;
	PBYTE GetEdidData(UINT Idx);
};


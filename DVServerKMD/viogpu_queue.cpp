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
#include "debug.h"
#include "baseobj.h"
#if !DBG
#include "viogpu_queue.tmh"
#endif

static BOOLEAN BuildSGElement(VirtIOBufferDescriptor* sg, PVOID buf, ULONG size)
{
    if (size != 0 && MmIsAddressValid(buf))
    {
        sg->length = min(size, PAGE_SIZE);
        sg->physAddr = MmGetPhysicalAddress(buf);
        return TRUE;
    }
    return FALSE;
}

VioGpuQueue::VioGpuQueue()
{
    m_pBuf = NULL;
    m_Index = (UINT)-1;
    m_pVIODevice = NULL;
    m_pVirtQueue = NULL;
    KeInitializeSpinLock(&m_SpinLock);
}

VioGpuQueue::~VioGpuQueue()
{
    Close();
}

void VioGpuQueue::Close(void)
{
    m_pVirtQueue = NULL;
}

BOOLEAN  VioGpuQueue::Init(
    _In_ VirtIODevice* pVIODevice,
    _In_ struct virtqueue* pVirtQueue,
    _In_ UINT index)
{
    TRACING();
    if ((pVIODevice == NULL) ||
        (pVirtQueue == NULL)) {
        return FALSE;
    }
    m_pVIODevice = pVIODevice;
    m_pVirtQueue = pVirtQueue;
    m_Index = index;
    EnableInterrupt();
    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
_IRQL_saves_global_(OldIrql, Irql)
_IRQL_raises_(DISPATCH_LEVEL)
void VioGpuQueue::Lock(KIRQL* Irql)
{
    KIRQL SavedIrql = KeGetCurrentIrql();

    if (SavedIrql < DISPATCH_LEVEL) {
        KeAcquireSpinLock(&m_SpinLock, &SavedIrql);
    }
    else if (SavedIrql == DISPATCH_LEVEL) {
        KeAcquireSpinLockAtDpcLevel(&m_SpinLock);
    }
    else {
        VioGpuDbgBreak();
    }
    *Irql = SavedIrql;
}

void VioGpuQueue::Unlock(KIRQL SavedIrql)
{
    if (SavedIrql < DISPATCH_LEVEL) {
        KeReleaseSpinLock(&m_SpinLock, SavedIrql);
    }
    else {
        KeReleaseSpinLockFromDpcLevel(&m_SpinLock);
    }
}

PAGED_CODE_SEG_BEGIN

UINT VioGpuQueue::QueryAllocation()
{
    PAGED_CODE();
    TRACING();

    USHORT NumEntries;
    ULONG RingSize, HeapSize;

    NTSTATUS status = virtio_query_queue_allocation(
        m_pVIODevice,
        m_Index,
        &NumEntries,
        &RingSize,
        &HeapSize);
    if (!NT_SUCCESS(status))
    {
        ERR("virtio_query_queue_allocation(%d) failed with error %x\n", m_Index, status);
        return 0;
    }

    return NumEntries;
}
PAGED_CODE_SEG_END

PAGED_CODE_SEG_BEGIN
PVOID CtrlQueue::AllocCmd(PGPU_VBUFFER* buf, int sz)
{
    PAGED_CODE();
    TRACING();

    PGPU_VBUFFER vbuf;
    vbuf = m_pBuf->GetBuf(sz, sizeof(GPU_CTRL_HDR), NULL);
    ASSERT(vbuf);
    *buf = vbuf;

    DBGPRINT("vbuf = %p\n", vbuf);
    return vbuf ? vbuf->buf : NULL;
}

PVOID CtrlQueue::AllocCmdResp(PGPU_VBUFFER* buf, int cmd_sz, PVOID resp_buf, int resp_sz)
{
    PAGED_CODE();
    TRACING();

    PGPU_VBUFFER vbuf;
    vbuf = m_pBuf->GetBuf(cmd_sz, resp_sz, resp_buf);
    ASSERT(vbuf);
    *buf = vbuf;

    return vbuf ? vbuf->buf : NULL;
}

BOOLEAN CtrlQueue::GetDisplayInfo(PGPU_VBUFFER buf, UINT id, PULONG xres, PULONG yres)
{
    PAGED_CODE();
    TRACING();

    PGPU_RESP_DISP_INFO resp = (PGPU_RESP_DISP_INFO)buf->resp_buf;
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO)
    {
        DBGPRINT("Type = %x: disabled\n", resp->hdr.type);
        return FALSE;
    }
    if (resp->pmodes[id].enabled) {
        DBGPRINT("output %d: %dx%d+%d+%d\n", id,
            resp->pmodes[id].r.width,
            resp->pmodes[id].r.height,
            resp->pmodes[id].r.x,
            resp->pmodes[id].r.y);
        *xres = resp->pmodes[id].r.width;
        *yres = resp->pmodes[id].r.height;
    }
    else {
        DBGPRINT("output %d: disabled\n", id);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN CtrlQueue::AskDisplayInfo(PGPU_VBUFFER* buf, KEVENT* event)
{
    PAGED_CODE();
    TRACING();

    PGPU_CTRL_HDR cmd;
    PGPU_VBUFFER vbuf;
    PGPU_RESP_DISP_INFO resp_buf;
    NTSTATUS status;

    resp_buf = reinterpret_cast<PGPU_RESP_DISP_INFO>
        (new (NonPagedPoolNx) BYTE[sizeof(GPU_RESP_DISP_INFO)]);

    if (!resp_buf)
    {
        ERR("Failed to allocate %d bytes\n", (int) sizeof(GPU_RESP_DISP_INFO));
        return FALSE;
    }

    cmd = (PGPU_CTRL_HDR)AllocCmdResp(&vbuf, sizeof(GPU_CTRL_HDR), resp_buf, sizeof(GPU_RESP_DISP_INFO));
    RtlZeroMemory(cmd, sizeof(GPU_CTRL_HDR));

    cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    KeInitializeEvent(event, NotificationEvent, FALSE);
    vbuf->event = event;

    LARGE_INTEGER timeout = { 0 };
    timeout.QuadPart = Int32x32To64(1000, -10000);

    DBGPRINT("QueueBuffer, type = %d\n", cmd->type);
    QueueBuffer(vbuf);
    status = KeWaitForSingleObject(event,
        Executive,
        KernelMode,
        FALSE,
        &timeout);

    if (status == STATUS_TIMEOUT) {
        DBGPRINT("Failed to ask display info due to timeout\n");
        VioGpuDbgBreak();
        //        return FALSE;
    }
    *buf = vbuf;

    return TRUE;
}

BOOLEAN CtrlQueue::AskEdidInfo(PGPU_VBUFFER* buf, UINT id, KEVENT* event)
{
    PAGED_CODE();
    TRACING();

    PGPU_CMD_GET_EDID cmd;
    PGPU_VBUFFER vbuf;
    PGPU_RESP_EDID resp_buf;
    NTSTATUS status;

    resp_buf = reinterpret_cast<PGPU_RESP_EDID>
        (new (NonPagedPoolNx) BYTE[sizeof(GPU_RESP_EDID)]);

    if (!resp_buf)
    {
        ERR("Failed to allocate %d bytes\n", (int) sizeof(GPU_RESP_EDID));
        return FALSE;
    }
    cmd = (PGPU_CMD_GET_EDID)AllocCmdResp(&vbuf, sizeof(GPU_CMD_GET_EDID), resp_buf, sizeof(GPU_RESP_EDID));
    RtlZeroMemory(cmd, sizeof(GPU_CMD_GET_EDID));

    cmd->hdr.type = VIRTIO_GPU_CMD_GET_EDID;
    cmd->scanout = id;

    KeInitializeEvent(event, NotificationEvent, FALSE);
    vbuf->event = event;

    LARGE_INTEGER timeout = { 0 };
    timeout.QuadPart = Int32x32To64(1000, -10000);

    DBGPRINT("QueueBuffer, type = %d, screen = %d\n", cmd->hdr.type, cmd->scanout);
    QueueBuffer(vbuf);

    status = KeWaitForSingleObject(event,
        Executive,
        KernelMode,
        FALSE,
        &timeout
    );

    if (status == STATUS_TIMEOUT) {
        DBGPRINT("Failed to get edid info due to timeout\n");
        VioGpuDbgBreak();
        //        return FALSE;
    }

    *buf = vbuf;
    return TRUE;
}

BOOLEAN CtrlQueue::GetEdidInfo(PGPU_VBUFFER buf, UINT id, PBYTE edid)
{
    PAGED_CODE();
    TRACING();

    PGPU_CMD_GET_EDID cmd = (PGPU_CMD_GET_EDID)buf->buf;
    PGPU_RESP_EDID resp = (PGPU_RESP_EDID)buf->resp_buf;

    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_EDID &&
        id >= MAX_SCAN_OUT)
    {
        DBGPRINT("Type = %x: disabled\n", resp->hdr.type);
        return FALSE;
    }
    if (cmd->scanout != id)
    {
        ERR("Invalid scanout = %x\n", cmd->scanout);
        return FALSE;
    }

    RtlCopyMemory(edid, resp->edid, EDID_V1_BLOCK_SIZE);
    return TRUE;
}

void CtrlQueue::CreateResource(UINT res_id, UINT format, UINT width, UINT height)
{
    PAGED_CODE();
    TRACING();

    PGPU_RES_CREATE_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_CREATE_2D)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = res_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

//FIXME!!! if 
    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}

void CtrlQueue::CreateResourceBlob(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents, ULONGLONG width, ULONGLONG height)
{
    PAGED_CODE();
    TRACING();

    PGPU_RES_CREATE_BLOB cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_CREATE_BLOB)AllocCmd(&vbuf, sizeof(*cmd));
    if ((vbuf == NULL)||(cmd == NULL)) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB;
    cmd->resource_id = res_id;
    cmd->blob_mem = VIRTIO_GPU_BLOB_MEM_GUEST;
    cmd->blob_flags = VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE;
    cmd->blob_id = 0;
    cmd->nr_entries = nents;
    /* TODO: Check if ROUND_TO_PAGES (round up) should be used or PAGE_ALIGN (round down) */
    cmd->size = ROUND_TO_PAGES(width * height * 4);

    vbuf->data_buf = ents;
    vbuf->data_size = sizeof(*ents) * nents;

    //FIXME!!! if
    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}


void CtrlQueue::UnrefResource(UINT res_id)
{
    PAGED_CODE();
    TRACING();

    PGPU_RES_UNREF cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_UNREF)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }

    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
    cmd->resource_id = res_id;

    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}

void CtrlQueue::InvalBacking(UINT res_id)
{
    PAGED_CODE();
    TRACING();

    PGPU_RES_DETACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_DETACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
    cmd->resource_id = res_id;

    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}

void CtrlQueue::SetScanout(UINT scan_id, UINT res_id, UINT width, UINT height, UINT x, UINT y)
{
    PAGED_CODE();
    TRACING();

    PGPU_SET_SCANOUT cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_SET_SCANOUT)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->resource_id = res_id;
    cmd->scanout_id = scan_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    //FIXME if 
    DBGPRINT("QueueBuffer, type = %d, screen = %d\n", cmd->hdr.type, scan_id);
    QueueBuffer(vbuf);
}

void CtrlQueue::SetScanoutBlob(UINT scan_id, UINT res_id, UINT width, UINT height, UINT format, UINT x, UINT y)
{
    PAGED_CODE();
    TRACING();

    PGPU_SET_SCANOUT_BLOB cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_SET_SCANOUT_BLOB)AllocCmd(&vbuf, sizeof(*cmd));
    if ((vbuf == NULL)||(cmd == NULL)) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT_BLOB;
    cmd->resource_id = res_id;
    cmd->scanout_id = scan_id;
    cmd->format = format;
    cmd->width = width;
    cmd->height = height;

    /* TODO: Check strides and offsets */
    for (int i = 0; i < 4; i++) {
        cmd->strides[i] = width * 4;
        cmd->offsets[i] = 0;
    }

    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    //FIXME if
    DBGPRINT("QueueBuffer, type = %d, screen = %d\n", cmd->hdr.type, scan_id);
    QueueBuffer(vbuf);
}

void CtrlQueue::ResFlush(UINT res_id, UINT width, UINT height, UINT x, UINT y, UINT screen_num, KEVENT* event)
{
    NTSTATUS status;

    PAGED_CODE();
    TRACING();

    PGPU_RES_FLUSH cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_FLUSH)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->hdr.fence_id = screen_num;
    cmd->resource_id = res_id;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    if (vbuf == NULL) {
        return;
    }

    KeInitializeEvent(event, NotificationEvent, FALSE);
    vbuf->event = event;

    DBGPRINT("QueueBuffer, type = %d, screen = %d\n", cmd->hdr.type, screen_num);
    QueueBuffer(vbuf);

    LARGE_INTEGER timeout = { 0 };
    timeout.QuadPart = Int32x32To64(1000, -1000);

    status = KeWaitForSingleObject(event,
        Executive,
        KernelMode,
        FALSE,
        &timeout);

    if (status == STATUS_TIMEOUT) {
        ERR("---> Timeout waiting for Resrouce Flush\n");
        VioGpuDbgBreak();
        //        return FALSE;
    }

    ReleaseBuffer(vbuf);
}

void CtrlQueue::TransferToHost2D(UINT res_id, ULONG offset, UINT width, UINT height, UINT x, UINT y, PUINT fence_id)
{
    PAGED_CODE();
    TRACING();
    PGPU_RES_TRANSF_TO_HOST_2D cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_TRANSF_TO_HOST_2D)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->resource_id = res_id;
    cmd->offset = offset;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->r.x = x;
    cmd->r.y = y;

    if (fence_id) {
        cmd->hdr.flags |= VIRTIO_GPU_FLAG_FENCE;
        cmd->hdr.fence_id = *fence_id;
    }

    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}

void CtrlQueue::AttachBacking(UINT res_id, PGPU_MEM_ENTRY ents, UINT nents)
{
    PAGED_CODE();
    TRACING();

    PGPU_RES_ATTACH_BACKING cmd;
    PGPU_VBUFFER vbuf;
    cmd = (PGPU_RES_ATTACH_BACKING)AllocCmd(&vbuf, sizeof(*cmd));
    if (!cmd) {
        ERR("Couldn't allocate %ld bytes of memory\n", sizeof(*cmd));
        return;
    }
    RtlZeroMemory(cmd, sizeof(*cmd));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = res_id;
    cmd->nr_entries = nents;

    vbuf->data_buf = ents;
    vbuf->data_size = sizeof(*ents) * nents;

    DBGPRINT("QueueBuffer, type = %d\n", cmd->hdr.type);
    QueueBuffer(vbuf);
}

PAGED_CODE_SEG_END

#define SGLIST_SIZE 64
UINT CtrlQueue::QueueBuffer(PGPU_VBUFFER buf)
{
    //    PAGED_CODE();
    TRACING();

    VirtIOBufferDescriptor  sg[SGLIST_SIZE];
    UINT sgleft = SGLIST_SIZE;
    UINT outcnt = 0, incnt = 0;
    UINT ret = 0;
    KIRQL SavedIrql;

    if (buf->size > PAGE_SIZE) {
        ERR("Size is too big %d\n", buf->size);
        return 0;
    }

    if (BuildSGElement(&sg[outcnt + incnt], (PVOID)buf->buf, buf->size))
    {
        outcnt++;
        sgleft--;
    }

    if (buf->data_size)
    {
        ULONG data_size = buf->data_size;
        PVOID data_buf = (PVOID)buf->data_buf;
        while (data_size)
        {
            if (BuildSGElement(&sg[outcnt + incnt], data_buf, data_size))
            {
                data_buf = (PVOID)((LONG_PTR)(data_buf)+PAGE_SIZE);
                data_size -= min(data_size, PAGE_SIZE);
                outcnt++;
                sgleft--;
                if (sgleft == 0) {
                    ERR("No more sgelenamt spots left %d\n", outcnt);
                    return 0;
                }
            }
        }
    }

    if (buf->resp_size > PAGE_SIZE) {
        ERR("resp_size is too big %d\n", buf->resp_size);
        return 0;
    }

    if (buf->resp_size && (sgleft > 0))
    {
        if (BuildSGElement(&sg[outcnt + incnt], (PVOID)buf->resp_buf, buf->resp_size))
        {
            incnt++;
            sgleft--;
        }
    }

    DBGPRINT("sgleft %d\n", sgleft);

    Lock(&SavedIrql);
    ret = AddBuf(&sg[0], outcnt, incnt, buf, NULL, 0);
    Unlock(SavedIrql);

    Kick();
    DBGPRINT("ret = %d\n", ret);
    return ret;
}

PGPU_VBUFFER CtrlQueue::DequeueBuffer(_Out_ UINT* len)
{
    TRACING();

    PGPU_VBUFFER buf = NULL;
    KIRQL SavedIrql;
    Lock(&SavedIrql);
    buf = (PGPU_VBUFFER)GetBuf(len);
    Unlock(SavedIrql);
    if (buf == NULL)
    {
        *len = 0;
    }
    return buf;
}


void VioGpuQueue::ReleaseBuffer(PGPU_VBUFFER buf)
{
    TRACING();
    m_pBuf->FreeBuf(buf);
}


BOOLEAN VioGpuBuf::Init(_In_ UINT cnt)
{
    KIRQL                   OldIrql;
    TRACING();

    InitializeListHead(&m_FreeBufs);
    InitializeListHead(&m_InUseBufs);
    KeInitializeSpinLock(&m_SpinLock);

    for (UINT i = 0; i < cnt; ++i) {
        PGPU_VBUFFER pvbuf = reinterpret_cast<PGPU_VBUFFER>
            (new (NonPagedPoolNx) BYTE[VBUFFER_SIZE]);
        //FIXME
        RtlZeroMemory(pvbuf, VBUFFER_SIZE);
        if (pvbuf)
        {
            KeAcquireSpinLock(&m_SpinLock, &OldIrql);
            InsertTailList(&m_FreeBufs, &pvbuf->list_entry);
            ++m_uCount;
            KeReleaseSpinLock(&m_SpinLock, OldIrql);
        }
    }
    ASSERT(m_uCount == cnt);

    return (m_uCount > 0);
}

void VioGpuBuf::Close(void)
{
    KIRQL                   OldIrql;

    TRACING();

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);
    while (!IsListEmpty(&m_InUseBufs))
    {
        LIST_ENTRY* pListItem = RemoveHeadList(&m_InUseBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pvbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pvbuf);
            ASSERT(pvbuf->resp_size <= MAX_INLINE_RESP_SIZE);

            delete[] reinterpret_cast<PBYTE>(pvbuf);
            --m_uCount;
        }
    }

    while (!IsListEmpty(&m_FreeBufs))
    {
        LIST_ENTRY* pListItem = RemoveHeadList(&m_FreeBufs);
        if (pListItem)
        {
            PGPU_VBUFFER pbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
            ASSERT(pbuf);

            if (pbuf->resp_buf && pbuf->resp_size > MAX_INLINE_RESP_SIZE)
            {
                delete[] reinterpret_cast<PBYTE>(pbuf->resp_buf);
                pbuf->resp_buf = NULL;
                pbuf->resp_size = 0;
            }

            if (pbuf->data_buf && pbuf->data_size)
            {
                delete[] reinterpret_cast<PBYTE>(pbuf->data_buf);
                pbuf->data_buf = NULL;
                pbuf->data_size = 0;
            }

            delete[] reinterpret_cast<PBYTE>(pbuf);
            --m_uCount;
        }
    }
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    ASSERT(m_uCount == 0);
}

PGPU_VBUFFER VioGpuBuf::GetBuf(
    _In_ int size,
    _In_ int resp_size,
    _In_ void *resp_buf)
{

    TRACING();

    PGPU_VBUFFER pbuf = NULL;
    PLIST_ENTRY pListItem = NULL;
    KIRQL                   OldIrql;

    KeAcquireSpinLock(&m_SpinLock, &OldIrql);

    if (!IsListEmpty(&m_FreeBufs))
    {
        pListItem = RemoveHeadList(&m_FreeBufs);
        pbuf = CONTAINING_RECORD(pListItem, GPU_VBUFFER, list_entry);
        if (!pbuf)
            return NULL;

        memset(pbuf, 0, VBUFFER_SIZE);
        pbuf->buf = (char *)((ULONG_PTR)pbuf + sizeof(*pbuf));
        pbuf->size = size;

        pbuf->resp_size = resp_size;
        if (resp_size <= MAX_INLINE_RESP_SIZE)
        {
            pbuf->resp_buf = (char *)((ULONG_PTR)pbuf->buf + size);
        }
        else
        {
            pbuf->resp_buf = (char *)resp_buf;
        }

        InsertTailList(&m_InUseBufs, &pbuf->list_entry);
    }
    else
    {
        ERR("Cannot allocate buffer\n");
        VioGpuDbgBreak();
    }
    KeReleaseSpinLock(&m_SpinLock, OldIrql);

    DBGPRINT("buf = %p\n", pbuf);
    return pbuf;
}

void VioGpuBuf::FreeBuf(
    _In_ PGPU_VBUFFER pbuf)
{
    KIRQL                   OldIrql;
    TRACING();
    DBGPRINT("buf = %p\n", pbuf);
    KeAcquireSpinLock(&m_SpinLock, &OldIrql);

    if (!IsListEmpty(&m_InUseBufs))
    {
        PLIST_ENTRY leCurrent = m_InUseBufs.Flink;
        PGPU_VBUFFER pvbuf = CONTAINING_RECORD(leCurrent, GPU_VBUFFER, list_entry);
        while (leCurrent && pvbuf)
        {
            if (pvbuf == pbuf)
            {
                RemoveEntryList(leCurrent);
                pvbuf = NULL;
                break;
            }

            leCurrent = leCurrent->Flink;
            if (leCurrent) {
                pvbuf = CONTAINING_RECORD(leCurrent, GPU_VBUFFER, list_entry);
            }
        }
    }
    if (pbuf->resp_buf && pbuf->resp_size > MAX_INLINE_RESP_SIZE)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf->resp_buf);
        pbuf->resp_buf = NULL;
        pbuf->resp_size = 0;
    }

    if (pbuf->data_buf && pbuf->data_size)
    {
        delete[] reinterpret_cast<PBYTE>(pbuf->data_buf);
        pbuf->data_buf = NULL;
        pbuf->data_size = 0;
    }

    InsertTailList(&m_FreeBufs, &pbuf->list_entry);
    KeReleaseSpinLock(&m_SpinLock, OldIrql);
}

PAGED_CODE_SEG_BEGIN
VioGpuBuf::VioGpuBuf()
{
    PAGED_CODE();
    TRACING();

    m_uCount = 0;
}

VioGpuBuf::~VioGpuBuf()
{
    PAGED_CODE();
    TRACING();

    DBGPRINT("0x%p\n", this);

    Close();
}

VioGpuMemSegment::VioGpuMemSegment(void)
{
    PAGED_CODE();
    TRACING();

    m_pSGList = NULL;
    m_pVAddr = NULL;
    m_pMdl = NULL;
    m_bSystemMemory = FALSE;
    m_bUserMemory = FALSE;
    m_bMapped = FALSE;
}

VioGpuMemSegment::~VioGpuMemSegment(void)
{
    PAGED_CODE();
    TRACING();

    Close();
}

BOOLEAN VioGpuMemSegment::Init(_In_ UINT size, _In_ PPHYSICAL_ADDRESS pPAddr)
{
    PAGED_CODE();
    TRACING();

    ASSERT(size);
    PVOID buf = NULL;
    UINT pages = BYTES_TO_PAGES(size);
    UINT sglsize = sizeof(SCATTER_GATHER_LIST) + (sizeof(SCATTER_GATHER_ELEMENT) * pages);
    size = pages * PAGE_SIZE;

    if (pPAddr == NULL) {
        m_pVAddr = new (NonPagedPoolNx) BYTE[size];
        RtlZeroMemory(m_pVAddr, size);

        if (!m_pVAddr)
        {
            ERR("Insufficient resources to allocate %x bytes\n", size);
            return FALSE;
        }
        m_bSystemMemory = TRUE;
    }
    else if (pPAddr->QuadPart) {
        NTSTATUS Status = MapFrameBuffer(*pPAddr, size, &m_pVAddr);
        if (!NT_SUCCESS(Status)) {
            ERR("MapFrameBuffer failed with Status: 0x%X\n", Status);
            return FALSE;
        }
        m_bMapped = TRUE;
    }
    else {
        ERR("Invalid address\n");
        return FALSE;
    }

    m_pMdl = IoAllocateMdl(m_pVAddr, size, FALSE, FALSE, NULL);
    if (!m_pMdl)
    {
        ERR("Insufficient resources to allocate MDLs\n");
        return FALSE;
    }
    if (m_bSystemMemory == TRUE) {
        __try
        {
            MmProbeAndLockPages(m_pMdl, KernelMode, IoWriteAccess);
        }
#pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ERR("Failed to lock pages with error %x\n", GetExceptionCode());
            IoFreeMdl(m_pMdl);
            return FALSE;
        }
    }
    m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
    m_pSGList->NumberOfElements = 0;
    m_pSGList->Reserved = 0;
    //       m_pSAddr = reinterpret_cast<BYTE*>
    //    (MmGetSystemAddressForMdlSafe(m_pMdl, NormalPagePriority | MdlMappingNoExecute));

    RtlZeroMemory(m_pSGList, sglsize);
    buf = PAGE_ALIGN(m_pVAddr);

    for (UINT i = 0; i < pages; ++i)
    {
        PHYSICAL_ADDRESS pa = { 0 };
        ASSERT(MmIsAddressValid(buf));
        pa = MmGetPhysicalAddress(buf);
        if (pa.QuadPart == 0LL)
        {
            ERR("Invalid PA buf = %p element %d\n", buf, i);
            break;
        }
        m_pSGList->Elements[i].Address = pa;
        m_pSGList->Elements[i].Length = PAGE_SIZE;
        buf = (PVOID)((LONG_PTR)(buf)+PAGE_SIZE);
        m_pSGList->NumberOfElements++;
    }
    m_Size = size;
    return TRUE;
}

BOOLEAN VioGpuMemSegment::InitExt(_In_ UINT size, _In_ PVOID pUserAddr)
{
    PAGED_CODE();
    TRACING();

    ASSERT(size);
    PVOID buf = NULL;
    UINT pages = BYTES_TO_PAGES(size);
    UINT sglsize = sizeof(SCATTER_GATHER_LIST) + (sizeof(SCATTER_GATHER_ELEMENT) * pages);
    size = pages * PAGE_SIZE;

    if (size > 0 && pUserAddr != NULL) {
        m_pVAddr = pUserAddr;
        m_bUserMemory = TRUE;
        m_pMdl = IoAllocateMdl(m_pVAddr, size, FALSE, FALSE, NULL);

        if (m_pMdl)
        {
            __try
            {
                MmProbeAndLockPages(m_pMdl, KernelMode, IoWriteAccess);
            }
#pragma prefast(suppress: __WARNING_EXCEPTIONEXECUTEHANDLER, "try/except is only able to protect against user-mode errors and these are the only errors we try to catch here");
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                ERR("Failed to lock pages with error %x\n", GetExceptionCode());
                IoFreeMdl(m_pMdl);
                return FALSE;
            }

            m_pSGList = reinterpret_cast<PSCATTER_GATHER_LIST>(new (NonPagedPoolNx) BYTE[sglsize]);
            m_pSGList->NumberOfElements = 0;
            m_pSGList->Reserved = 0;
            RtlZeroMemory(m_pSGList, sglsize);
            buf = PAGE_ALIGN(m_pVAddr);

            for (UINT i = 0; i < pages; ++i)
            {
                PHYSICAL_ADDRESS pa = { 0 };
                ASSERT(MmIsAddressValid(buf));
                pa = MmGetPhysicalAddress(buf);
                if (pa.QuadPart == 0LL)
                {
                    ERR("Invalid PA buf = %p element %d\n", buf, i);
                    break;
                }
                m_pSGList->Elements[i].Address = pa;
                m_pSGList->Elements[i].Length = PAGE_SIZE;
                buf = (PVOID)((LONG_PTR)(buf)+PAGE_SIZE);
                m_pSGList->NumberOfElements++;
            }

            m_Size = size;
            return TRUE;
        }
        else {
            ERR("Insufficient resources to allocate MDLs\n");
            return FALSE;
        }
    }
    else {
        ERR("Invalid parameter encountered!\n");
        return FALSE;
    }
}

PHYSICAL_ADDRESS VioGpuMemSegment::GetPhysicalAddress(void)
{
    PAGED_CODE();
    TRACING();

    PHYSICAL_ADDRESS pa = { 0 };
    if (m_pVAddr && MmIsAddressValid(m_pVAddr))
    {
        pa = MmGetPhysicalAddress(m_pVAddr);
    }
    return pa;
}

void VioGpuMemSegment::Close(void)
{
    PAGED_CODE();
    TRACING();

    if (m_pMdl)
    {
        if (m_bSystemMemory || m_bUserMemory) {
            MmUnlockPages(m_pMdl);
        }
        IoFreeMdl(m_pMdl);
        m_pMdl = NULL;
    }

    if (!m_bUserMemory) {
        if (m_bSystemMemory) {
            delete[] m_pVAddr;
        }
        else {
            UnmapFrameBuffer(m_pVAddr, (ULONG)m_Size);
            m_bMapped = FALSE;
        }
    }

    m_pVAddr = NULL;

    delete[] reinterpret_cast<PBYTE>(m_pSGList);
    m_pSGList = NULL;
}


VioGpuObj::VioGpuObj(void)
{
    PAGED_CODE();
    TRACING();

    m_uiHwRes = 0;
    m_pSegment = NULL;
}

VioGpuObj::~VioGpuObj(void)
{
    PAGED_CODE();
    TRACING();
}

BOOLEAN VioGpuObj::Init(_In_ UINT size, VioGpuMemSegment *pSegment)
{
    PAGED_CODE();
    TRACING();

    DBGPRINT("requested size = %d\n", size);

    ASSERT(size);
    ASSERT(pSegment);
    UINT pages = BYTES_TO_PAGES(size);
    size = pages * PAGE_SIZE;
    if (size > pSegment->GetSize())
    {
        ERR("segment size too small = %Iu (%u)\n", m_pSegment->GetSize(), size);
        return FALSE;
    }
    m_pSegment = pSegment;
    m_Size = size;
    return TRUE;
}

PVOID CrsrQueue::AllocCursor(PGPU_VBUFFER* buf)
{
    PAGED_CODE();
    TRACING();

    PGPU_VBUFFER vbuf;
    vbuf = m_pBuf->GetBuf(sizeof(GPU_UPDATE_CURSOR), 0, NULL);
    ASSERT(vbuf);
    *buf = vbuf;

    DBGPRINT("vbuf = %p\n", vbuf);
    return vbuf ? vbuf->buf : NULL;
}

PAGED_CODE_SEG_END

UINT CrsrQueue::QueueCursor(PGPU_VBUFFER buf)
{
    //    PAGED_CODE();
    TRACING();

    UINT res = 0;
    KIRQL SavedIrql;

    VirtIOBufferDescriptor  sg[1];
    int outcnt = 0;
    UINT ret = 0;

    ASSERT(buf->size <= PAGE_SIZE);
    if (BuildSGElement(&sg[outcnt], (PVOID)buf->buf, buf->size))
    {
        outcnt++;
    }

    ASSERT(outcnt);
    Lock(&SavedIrql);
    ret = AddBuf(&sg[0], outcnt, 0, buf, NULL, 0);
    Unlock(SavedIrql);
    Kick();

    DBGPRINT("vbuf = %p outcnt = %d, ret = %d\n", buf, outcnt, ret);
    return res;
}

PGPU_VBUFFER CrsrQueue::DequeueCursor(_Out_ UINT* len)
{
    TRACING();

    PGPU_VBUFFER buf = NULL;
    KIRQL SavedIrql;
    Lock(&SavedIrql);
    buf = (PGPU_VBUFFER)GetBuf(len);
    Unlock(SavedIrql);
    if (buf == NULL)
    {
        *len = 0;
    }
    DBGPRINT("buf %p len = %u\n", buf, *len);
    return buf;
}

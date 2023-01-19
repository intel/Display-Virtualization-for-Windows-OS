/*
 * Virtio PCI driver
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Windows porting - Yan Vugenfirer <yvugenfi@redhat.com>
 *  WDDM porting - Vadim Rozenfeld <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
 /**********************************************************************
  * Copyright (c) 2012-2020 Red Hat, Inc.
  *
  * This work is licensed under the terms of the GNU GPL, version 2.  See
  * the COPYING file in the top-level directory.
  *
 **********************************************************************/
#include "driver.h"
#include "helper.h"
#include "viogpu.h"
#include "viogpulite.h"
#include "Trace.h"
#include "viogpu_pci.tmh"
#if !DBG
#include "viogpu_pci.tmh"
#endif

u32 ReadVirtIODeviceRegister(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_ULONG((PULONG)(ulRegister));
    }
    else {
        return READ_PORT_ULONG((PULONG)(ulRegister));
    }
}

void WriteVirtIODeviceRegister(ULONG_PTR ulRegister, u32 ulValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_ULONG((PULONG)(ulRegister), (ULONG)(ulValue));
    }
    else {
        WRITE_PORT_ULONG((PULONG)(ulRegister), (ULONG)(ulValue));
    }
}

u8 ReadVirtIODeviceByte(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_UCHAR((PUCHAR)(ulRegister));
    }
    else {
        return READ_PORT_UCHAR((PUCHAR)(ulRegister));
    }
}

void WriteVirtIODeviceByte(ULONG_PTR ulRegister, u8 bValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_UCHAR((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
    else {
        WRITE_PORT_UCHAR((PUCHAR)(ulRegister), (UCHAR)(bValue));
    }
}

u16 ReadVirtIODeviceWord(ULONG_PTR ulRegister)
{
    if (ulRegister & ~PORT_MASK) {
        return READ_REGISTER_USHORT((PUSHORT)(ulRegister));
    }
    else {
        return READ_PORT_USHORT((PUSHORT)(ulRegister));
    }
}

void WriteVirtIODeviceWord(ULONG_PTR ulRegister, u16 wValue)
{
    if (ulRegister & ~PORT_MASK) {
        WRITE_REGISTER_USHORT((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
    else {
        WRITE_PORT_USHORT((PUSHORT)(ulRegister), (USHORT)(wValue));
    }
}

void *mem_alloc_contiguous_pages(void *context, size_t size)
{
    PHYSICAL_ADDRESS HighestAcceptable;
    PVOID ptr = NULL;

    UNREFERENCED_PARAMETER(context);

    HighestAcceptable.QuadPart = 0xFFFFFFFFFF;
    ptr = MmAllocateContiguousMemory(size, HighestAcceptable);
    if (ptr) {
        RtlZeroMemory(ptr, size);
    }
    else {
        ERR("Ran out of memory in alloc_pages_exact(%Id)\n", size);
    }
    return ptr;
}

void mem_free_contiguous_pages(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);
    if (virt) {
        MmFreeContiguousMemory(virt);
    }
}

ULONGLONG mem_get_physical_address(void *context, void *virt)
{
    UNREFERENCED_PARAMETER(context);

    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(virt);
    return pa.QuadPart;
}

void *mem_alloc_nonpaged_block(void *context, size_t size)
{
    UNREFERENCED_PARAMETER(context);
    PVOID ptr = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        size,
        VIOGPUTAG);
    if (ptr) {
        RtlZeroMemory(ptr, size);
    }
    else {
        ERR("Ran out of memory in alloc_pages_exact(%Id)\n", size);
    }
    return ptr;
}

void mem_free_nonpaged_block(void *context, void *addr)
{
    UNREFERENCED_PARAMETER(context);
    if (addr) {
        ExFreePoolWithTag(
            addr,
            VIOGPUTAG);
    }
}

PAGED_CODE_SEG_BEGIN
static int PCIReadConfig(
    VioGpuAdapterLite* pdev,
    int where,
    void *buffer,
    size_t length)
{
    PAGED_CODE();

    ULONG BytesRead = 0;
    PDEVICE_CONTEXT pContext = (PDEVICE_CONTEXT)pdev->m_pvDeviceContext;
    BUS_INTERFACE_STANDARD* pBusInterface = &pContext->BusInterface;

    BytesRead = pBusInterface->GetBusData(
        pBusInterface->Context,
        PCI_WHICHSPACE_CONFIG,
        buffer,
        where,
        (ULONG)length
        );

    if (BytesRead != length)
    {
        ERR("read %d bytes at %d\n", BytesRead, where);
        return -1;
    }
    return 0;
}

static int pci_read_config_byte(void *context, int where, u8 *bVal)
{
    PAGED_CODE();
    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    return PCIReadConfig(pdev, where, bVal, sizeof(*bVal));
}

int pci_read_config_word(void *context, int where, u16 *wVal)
{
    PAGED_CODE();
    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    return PCIReadConfig(pdev, where, wVal, sizeof(*wVal));
}

int pci_read_config_dword(void *context, int where, u32 *dwVal)
{
    PAGED_CODE();
    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    return PCIReadConfig(pdev, where, dwVal, sizeof(*dwVal));
}
PAGED_CODE_SEG_END

size_t pci_get_resource_len(void *context, int bar)
{
    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    return pdev->GetPciResources()->GetBarSize(bar);
}

void *pci_map_address_range(void *context, int bar, size_t offset, size_t maxlen)
{
    UNREFERENCED_PARAMETER(maxlen);

    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    return pdev->GetPciResources()->GetMappedAddress(bar, (ULONG)offset);
}

u16 vdev_get_msix_vector(void *context, int queue)
{
    VioGpuAdapterLite* pdev = static_cast<VioGpuAdapterLite*>(context);
    u16 vector = VIRTIO_MSI_NO_VECTOR;

    if (queue >= 0) {
        /* queue interrupt */
        if (pdev->IsMSIEnabled()) {
            vector = (u16)(queue + 1);
        }
    }
    else {
        vector = VIRTIO_GPU_MSIX_CONFIG_VECTOR;
    }
    return vector;
}

void vdev_sleep(void *context, unsigned int msecs)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    UNREFERENCED_PARAMETER(context);

    if (KeGetCurrentIrql() <= APC_LEVEL) {
        LARGE_INTEGER delay;
        delay.QuadPart = Int32x32To64(msecs, -10000);
        status = KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }

    if (!NT_SUCCESS(status)) {
        KeStallExecutionProcessor(1000 * msecs);
    }
}


VirtIOSystemOps VioGpuSystemOps = {
    ReadVirtIODeviceByte,
    ReadVirtIODeviceWord,
    ReadVirtIODeviceRegister,
    WriteVirtIODeviceByte,
    WriteVirtIODeviceWord,
    WriteVirtIODeviceRegister,
    mem_alloc_contiguous_pages,
    mem_free_contiguous_pages,
    mem_get_physical_address,
    mem_alloc_nonpaged_block,
    mem_free_nonpaged_block,
    pci_read_config_byte,
    pci_read_config_word,
    pci_read_config_dword,
    pci_get_resource_len,
    pci_map_address_range,
    vdev_get_msix_vector,
    vdev_sleep,
};


PVOID CPciBar::GetVA(PDXGKRNL_INTERFACE pDxgkInterface)
{
    NTSTATUS Status;
    if (m_BaseVA == nullptr)
    {
        if (m_bPortSpace)
        {
            if (m_bIoMapped)
            {
                Status = pDxgkInterface->DxgkCbMapMemory(pDxgkInterface->DeviceHandle,
                    m_BasePA,
                    m_uSize,
                    TRUE,
                    FALSE,
                    MmNonCached,
                    &m_BaseVA
                );
                if (Status == STATUS_SUCCESS)
                {
                    DBGPRINT("mapped port BAR at %x\n", m_BasePA.LowPart);
                }
                else
                {
                    m_BaseVA = nullptr;
                    ERR("DxgkCbMapMemor (CmResourceTypePort) failed with status 0x%X\n", Status);
                }
            }
            else
            {
                m_BaseVA = (PUCHAR)(ULONG_PTR)m_BasePA.QuadPart;
            }
        }
        else
        {
            m_BaseVA = MmMapIoSpace(
                m_BasePA,
                m_uSize,
                MmNonCached);

            if (m_BaseVA)
            {
                DBGPRINT("mapped memory BAR at %I64x\n", m_BasePA.QuadPart);
            }
            else
            {
                m_BaseVA = nullptr;
                ERR("failed to map memory BAR at %I64x\n", m_BasePA.QuadPart);
            }
        }
    }
    return m_BaseVA;
}

void CPciBar::Unmap(void)
{
    if (m_BaseVA != nullptr)
    {
        if (!m_bIoMapped)
        {
            MmUnmapIoSpace(m_BaseVA, m_uSize);
        }
    }
    m_BaseVA = nullptr;
}

bool CPciResources::Init(PVOID pvDeviceContext, PVOID pResList)
{
    PCI_COMMON_HEADER pci_config = { 0 };
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pResDescriptor = 0;
    ULONG BytesRead = 0, count;
    bool interrupt_found = false;
    int bar = -1;
    PDEVICE_CONTEXT pDeviceContext = (PDEVICE_CONTEXT)pvDeviceContext;
    BUS_INTERFACE_STANDARD* pBusInterface = NULL;

    if (!pDeviceContext)
        return false;

    this->m_pvDeviceContext = pvDeviceContext;
    pBusInterface = &pDeviceContext->BusInterface;

    TRACING();

    BytesRead = pBusInterface->GetBusData(
        pBusInterface->Context,
        PCI_WHICHSPACE_CONFIG,
        &pci_config,
        0,
        sizeof(PCI_COMMON_HEADER)
        );
        
    DBGPRINT("BytesRead = %d\n", BytesRead);

    if (BytesRead != sizeof(pci_config)) {
        ERR("could not read PCI config space\n");
        return false;
    }

    count = WdfCmResourceListGetCount((WDFCMRESLIST)pResList);
    DBGPRINT("ListCount = %d\n", count);

    for (ULONG i = 0; i < count; i++) {

        pResDescriptor = WdfCmResourceListGetDescriptor((WDFCMRESLIST)pResList, i);

        if (!pResDescriptor) {
            ERR("Could not get resource descriptor.\n");
            return false;
        }

        switch (pResDescriptor->Type)
        {
            case CmResourceTypePort: 
            {
                DBGPRINT("CmResourceTypePort\n");
                break;
            }
            break;
            case CmResourceTypeInterrupt:
            {
                m_InterruptFlags = pResDescriptor->Flags;
                if (IsMSIEnabled())
                {
                    DBGPRINT("Found MSI Interrupt vector %d, level %d, affinity 0x%X, flags %X\n",
                        pResDescriptor->u.MessageInterrupt.Translated.Vector,
                        pResDescriptor->u.MessageInterrupt.Translated.Level,
                        (ULONG)pResDescriptor->u.MessageInterrupt.Translated.Affinity,
                        pResDescriptor->Flags);
                }
                else
                {
                    DBGPRINT("Found Interrupt vector %d, level %d, affinity 0x%X, flags %X\n",
                        pResDescriptor->u.Interrupt.Vector,
                        pResDescriptor->u.Interrupt.Level,
                        (ULONG)pResDescriptor->u.Interrupt.Affinity,
                        pResDescriptor->Flags);
                }

                interrupt_found = true;
            }
            break;
            case CmResourceTypeMemory:
            {
                PHYSICAL_ADDRESS Start = pResDescriptor->u.Port.Start;
                ULONG len = pResDescriptor->u.Port.Length;
                bar = virtio_get_bar_index(&pci_config, Start);
                DBGPRINT("Found IO memory at %08I64X(%d) bar %d\n", Start.QuadPart, len, bar);
                if (bar < 0)
                {
                    break;
                }
                m_Bars[bar] = CPciBar(Start, len, false, true);
            }
            break;
            case CmResourceTypeDma:
                DBGPRINT("Dma\n");
                break;
            case CmResourceTypeDeviceSpecific:
                DBGPRINT("Device Specific\n");
                break;
            case CmResourceTypeBusNumber:
                DBGPRINT("Bus number\n");
                break;
            default:
                DBGPRINT("Unsupported descriptor type = %d\n", pResDescriptor->Type);
                break;
        }
    }

    if (bar < 0 || !interrupt_found)
    {
        ERR("Resource enumeration failed\n");
        return false;
    }

    return true;
}

PVOID CPciResources::GetMappedAddress(UINT bar, ULONG uOffset)
{
    PVOID BaseVA = nullptr;
    ASSERT(bar < PCI_TYPE0_ADDRESSES);

    if (uOffset < m_Bars[bar].GetSize())
    {
        BaseVA = m_Bars[bar].GetVA(m_pDxgkInterface);
    }
    if (BaseVA != nullptr)
    {
        if (m_Bars[bar].IsPortSpace())
        {
            // use physical address for port I/O
            return (PUCHAR)(ULONG_PTR)m_Bars[bar].GetPA().LowPart + uOffset;
        }
        else
        {
            // use virtual address for memory I/O
            return (PUCHAR)BaseVA + uOffset;
        }
    }
    else
    {
        ERR("Failed to map BAR %d, offset %x\n", bar, uOffset);
        return nullptr;
    }
}

PAGED_CODE_SEG_BEGIN
NTSTATUS
MapFrameBuffer(
    _In_                       PHYSICAL_ADDRESS    PhysicalAddress,
    _In_                       ULONG               Length,
    _Outptr_result_bytebuffer_(Length) VOID**      VirtualAddress)
{
    PAGED_CODE();

    if ((PhysicalAddress.QuadPart == (ULONGLONG)0) ||
        (Length == 0) ||
        (VirtualAddress == NULL))
    {
        ERR("One of PhysicalAddress.QuadPart (0x%I64x), Length (%lu), VirtualAddress (%p) is NULL or 0\n",
            PhysicalAddress.QuadPart, Length, VirtualAddress);
        return STATUS_INVALID_PARAMETER;
    }

    *VirtualAddress = MmMapIoSpace(PhysicalAddress,
        Length,
        MmWriteCombined);
    if (*VirtualAddress == NULL)
    {

        *VirtualAddress = MmMapIoSpace(PhysicalAddress,
            Length,
            MmNonCached);
        if (*VirtualAddress == NULL)
        {
            ERR("MmMapIoSpace returned a NULL buffer when trying to allocate %lu bytes", Length);
            return STATUS_NO_MEMORY;
        }
    }

    ERR("PhysicalAddress.QuadPart (0x%I64x), Length (%lu), VirtualAddress (%p)\n",
        PhysicalAddress.QuadPart, Length, VirtualAddress);
    return STATUS_SUCCESS;
}

NTSTATUS
UnmapFrameBuffer(
    _In_reads_bytes_(Length) VOID* VirtualAddress,
    _In_                ULONG Length)
{
    PAGED_CODE();

    DBGPRINT("Length (%lu), VirtualAddress (%p)\n", Length, VirtualAddress);
    if ((VirtualAddress == NULL) && (Length == 0))
    {
        return STATUS_SUCCESS;
    }
    else if ((VirtualAddress == NULL) || (Length == 0))
    {
        ERR("Only one of Length (%lu), VirtualAddress (%p) is NULL or 0\n",
            Length, VirtualAddress);
        return STATUS_INVALID_PARAMETER;
    }

    MmUnmapIoSpace(VirtualAddress, Length);

    return STATUS_SUCCESS;
}
PAGED_CODE_SEG_END

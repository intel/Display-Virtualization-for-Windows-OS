#include "baseobj.h"

_When_((PoolType& NonPagedPoolMustSucceed) != 0,
	__drv_reportError("Must succeed pool allocations are forbidden. "
		"Allocation failures cause a system crash"))
	void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{
	Size = (Size != 0) ? Size : 1;

	ULONG Flags = 0;
	switch (PoolType) {
	case NonPagedPool:
		Flags = POOL_FLAG_NON_PAGED;
		break;
	case PagedPool:
		Flags = POOL_FLAG_PAGED;
		break;
	case NonPagedPoolNx:
		Flags = POOL_FLAG_NON_PAGED | POOL_FLAG_CACHE_ALIGNED;
		break;
	default:
		// Handle other pool types or set a default flag
		Flags = POOL_FLAG_NON_PAGED;
		break;
	}

	void* pObject = ExAllocatePool2(Flags, Size, VIOGPUTAG);

	if (pObject != NULL)
	{
#if DBG
		RtlFillMemory(pObject, Size, 0xCD);
#else
		RtlZeroMemory(pObject, Size);
#endif // DBG
	}
	return pObject;
}

_When_((PoolType& NonPagedPoolMustSucceed) != 0,
	__drv_reportError("Must succeed pool allocations are forbidden. "
		"Allocation failures cause a system crash"))
	void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{

	Size = (Size != 0) ? Size : 1;

	ULONG Flags = 0;
	switch (PoolType) {
	case NonPagedPool:
		Flags = POOL_FLAG_NON_PAGED;
		break;
	case PagedPool:
		Flags = POOL_FLAG_PAGED;
		break;
	case NonPagedPoolNx:
		Flags = POOL_FLAG_NON_PAGED | POOL_FLAG_CACHE_ALIGNED;
		break;
	default:
		// Handle other pool types or set a default flag
		Flags = POOL_FLAG_NON_PAGED;
		break;
	}

	void* pObject = ExAllocatePool2(Flags, Size, VIOGPUTAG);

	if (pObject != NULL)
	{
#if DBG
		RtlFillMemory(pObject, Size, 0xCD);
#else
		RtlZeroMemory(pObject, Size);
#endif
	}
	return pObject;
}

void __cdecl operator delete(void* pObject)
{

	if (pObject != NULL)
	{
		ExFreePoolWithTag(pObject, VIOGPUTAG);
	}
}

void __cdecl operator delete[](void* pObject)
{

	if (pObject != NULL)
	{
		ExFreePoolWithTag(pObject, VIOGPUTAG);
	}
}

void __cdecl operator delete(void* pObject, size_t Size)
{

	UNREFERENCED_PARAMETER(Size);
	::operator delete (pObject);
}

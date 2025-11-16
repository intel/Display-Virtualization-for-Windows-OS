/*++

Module Name:

	public.h

Abstract:

	This module contains the common declarations shared by driver
	and user applications.

Environment:

	user and kernel

--*/
#ifndef __PUBLIC_H__
#define __PUBLIC_H__
//
// Define an Interface Guid so that apps can find the device and talk to it.
//

DEFINE_GUID(GUID_DEVINTERFACE_DVServerKMD,
	0x1c514918, 0xa855, 0x460a, 0x97, 0xda, 0xed, 0x69, 0x1d, 0xd5, 0x63, 0xcf);
// {1c514918-a855-460a-97da-ed691dd563cf}

#define MAX_SCAN_OUT               4
#define MODE_LIST_MAX_SIZE         32
#define IOCTL_DVSERVER_FRAME_DATA			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_CURSOR_DATA			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_GET_EDID_DATA		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_SET_MODE				CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_TEST_IMAGE			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_GET_TOTAL_SCREENS	CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_HP_EVENT				CTL_CODE(FILE_DEVICE_UNKNOWN, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DVSERVER_CURSOR_POS			CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct FrameMetaData
{
	unsigned int width;
	unsigned int height;
	unsigned int format;
	unsigned int pitch;
	unsigned int stride;
	UINT16	bitrate;
	void* addr;
	unsigned short	refresh_rate;
	unsigned int screen_num;

}FrameMetaData;

typedef struct CursorData
{
	UINT32	screen_num;
	INT		cursor_x;
	INT		cursor_y;
	bool	iscursorvisible;
	UINT32	cursor_version;
	UINT16	cursor_type;
	UINT32  width;
	UINT32  height;
	UINT32  pitch;
	UINT32 color_format;
	void*	data;
	UINT32	x_hot;
	UINT32	y_hot;
}CursorData;

struct mode_info
{
	unsigned int width;
	unsigned int height;
	double refreshrate;
};

struct edid_info
{
	unsigned char edid_data[256];
	unsigned int mode_size;
	unsigned int screen_num;
	struct mode_info mode_list[MODE_LIST_MAX_SIZE];
};

struct screen_info
{
	unsigned int total_screens;
};

struct hp_info
{
	HANDLE event;
	bool screen_present[MAX_SCAN_OUT];
};

struct KMDF_IOCTL_Response
{
	UINT16 retval;
};

#endif // __PUBLIC_H__

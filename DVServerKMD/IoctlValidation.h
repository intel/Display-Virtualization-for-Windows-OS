/*===========================================================================
; IoctlVaidation.h
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: BSD-3-Clause
;--------------------------------------------------------------------------*/
#pragma once

#include <ntifs.h>
#include <wdf.h>
#include "Public.h"
#include "edid.h"

// Validation types for IOCTL operations
enum IOCTL_VALIDATE_TYPE
{
	VALIDATE_FRAME,
	VALIDATE_CURSOR,
	VALIDATE_CURSOR_POSITION,
	VALIDATE_EDID,
	VALIDATE_TOTAL_SCREENS,
	VALIDATE_HP_EVENT
};

// Returns true if input and output buffers overlap
bool BuffersOverlap(const void *inputBuffer, size_t inputBufferLength, const void *outputBuffer, size_t outputBufferLength);

// Validates IOCTL input/output data based on type
NTSTATUS ValidateIoctl(const void *data, const WDFREQUEST Request, IOCTL_VALIDATE_TYPE type);

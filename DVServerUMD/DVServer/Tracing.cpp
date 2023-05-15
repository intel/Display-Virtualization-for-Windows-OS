/*===========================================================================
; Tracing.cpp
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file defines tracing definitions.
;--------------------------------------------------------------------------*/

#include "Driver.h"
#include "Trace.h"
#include "tracing.tmh"
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <Windows.h>

/*******************************************************************************
*
* Description
*
* tracer is a constructor member function of the class tracer invoked to mark the entry of the function.
*
* Parameters
*   func_name - Function name.
*
*
******************************************************************************/
tracer::tracer(const char* func_name)
{
	FuncTrace(">>> %s\n", func_name);
	m_func_name = (char*)func_name;
}
/*******************************************************************************
*
* Description
*
* ~tracer is a destructor member function of the class tracer invoked to mark the exit of the function.
*
* Parameters
*   func_name - Function name.
*
*
******************************************************************************/
tracer::~tracer()
{
	FuncTrace("<<< %s\n", m_func_name);
}
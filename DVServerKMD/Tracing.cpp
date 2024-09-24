/*++
*
* Copyright (C) 2021 Intel Corporation
* SPDX-License-Identifier: BSD-3-Clause

Module Name:

	Tracing.cpp

Abstract:

	This file contains the function tracing entry and exits.

Environment:

	Kernel-mode Driver Framework

--*/
#include "Driver.h"
#include "Trace.h"
#include "tracing.tmh"


/*******************************************************************************
*
* Description
*
* tracer is a constructor member function of the class tracer called to mark the entry of the function.
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
* tracer is a destructor member function of the class tracer called to mark exit of the function.
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

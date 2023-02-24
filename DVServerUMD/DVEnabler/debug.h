/*===========================================================================
; debug.h
;----------------------------------------------------------------------------
; Copyright (C) 2021 Intel Corporation
; SPDX-License-Identifier: MS-PL
;
; File Description:
;   This file declares and defines the devserverumd logging macros
;--------------------------------------------------------------------------*/
#ifndef _DEBUG_H
#define _DEBUG_H

enum {
	ERR,
	INFO,
	DBUG,
	TRACE,
};

/*
 * Set it to 0 or higher with 0 being lowest number of messages
 * 0 = Only errors show up
 * 1 = Errors + Info messages
 * 2 = Errors + Info messages + Debug messages
 * 3 = Errors + Info messages + Debug messages + Trace calls
 */

#if(__DEBUG)
	static int dbg_lvl = DBUG;
#else
	static int dbg_lvl = ERR;
#endif

static char module_name[80] = { '\0' };


#define _NOFUNC_PRINT(prefix, fmt, ...)  \
	if(1) {\
		char temp_str123[500], temp_str456[500], temp_str789[500]; \
		sprintf_s(temp_str123, "%s %s ", module_name, prefix); \
		sprintf_s(temp_str456, fmt, ##__VA_ARGS__); \
		sprintf_s(temp_str789, "%s%s", temp_str123, temp_str456); \
		OutputDebugStringA(temp_str789); \
		fflush(stdout); \
	}

#define _PRINT(prefix, fmt, ...)  \
	if(1) {\
		char temp_str123[500], temp_str456[500], temp_str789[500]; \
		sprintf_s(temp_str123, "%s %s [%s: %d] ", module_name, prefix, __FUNCTION__, __LINE__); \
		sprintf_s(temp_str456, fmt, ##__VA_ARGS__); \
		sprintf_s(temp_str789, "%s%s", temp_str123, temp_str456); \
		OutputDebugStringA(temp_str789); \
		fflush(stdout); \
	}

#define PRINT(fmt, ...)    _PRINT("", fmt, ##__VA_ARGS__)
#define ERR(fmt, ...)      _PRINT("[Error] ", fmt, ##__VA_ARGS__)
#define WARNING(fmt, ...)  _PRINT("[Warning] ", fmt, ##__VA_ARGS__)
#define DBGPRINT(fmt, ...) if(dbg_lvl >= DBUG) _PRINT("[DBG] ", fmt, ##__VA_ARGS__)
#define INFO(fmt, ...)     if(dbg_lvl >= INFO) _PRINT("[Info] ", fmt, ##__VA_ARGS__)
#define TRACE(fmt, ...)    if(dbg_lvl >= TRACE) _NOFUNC_PRINT("", fmt, ##__VA_ARGS__)

#ifdef __cplusplus
#define TRACING()  tracer trace(__FUNCTION__);
#else
#define TRACING()
#endif

#ifdef __cplusplus
class tracer {
private:
	char* m_func_name;
public:
	tracer(const char* func_name)
	{
		TRACE(">>> %s\n", func_name);
		m_func_name = (char*)func_name;
	}
	~tracer() { TRACE("<<< %s\n", m_func_name); }
};
#endif

inline int set_dbg_lvl(int lvl)
{
	int ret = 1;
	if (lvl >= ERR && lvl <= TRACE) {
		dbg_lvl = lvl;
		ret = 0;
	}
	return ret;
}

inline void set_module_name(const char *m)
{
	strcpy_s(module_name, 80, m);
}

#endif


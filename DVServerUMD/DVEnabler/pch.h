// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"
#include <vector>

/* DVENABLER Error Codes */
#define DVENABLER_SUCCESS        0
#define DVENABLER_FAILURE        -1

#define HOTPLUG_EVENT			L"Global\\HOTPLUG_EVENT"
#define DVE_EVENT				L"Global\\DVE_EVENT"
#define DISP_INFO				L"Global\\DISP_INFO"
#define DELAY_TIME				50
int dvenabler_init();
struct disp_info {
	int disp_count;
	HANDLE mutex;
	BOOL exit_dvenabler;
};
int GetDisplayCount(disp_info* pdinfo);
#endif //PCH_H

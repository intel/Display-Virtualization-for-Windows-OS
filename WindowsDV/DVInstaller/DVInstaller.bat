REM -------------------------------------
REM * Copyright © 2020 Intel Corporation
REM SPDX-License-Identifier: MIT
REM -------------------------------------

@echo off
echo Start driver installations...
reg add "HKLM\SOFTWARE\Intel\KMD\Guc" /v GuCEnableNewInterface /t REG_DWORD /d 0x1 /f
reg add "HKLM\SOFTWARE\Intel\KMD\SRIOV" /v VFSkipRelayService /t REG_DWORD /d 0x1 /f 
reg add "HKLM\SOFTWARE\Intel\KMD\SRIOV" /v EnableFTRPAAC /t REG_DWORD /d 0x1 /f
pnputil.exe /add-driver .\GraphicsDriver\iigd_dch.inf /install
pnputil.exe /i /a .\IVSHMEM\ivshmem.inf
devcon.exe install .\DVServer\DVServer.inf Root\DVServer
echo Done driver installations...
echo Reboot the system...
shutdown /r
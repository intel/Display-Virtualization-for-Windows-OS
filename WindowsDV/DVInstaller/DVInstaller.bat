REM -------------------------------------
REM * Copyright © 2020 Intel Corporation
REM SPDX-License-Identifier: MIT
REM -------------------------------------

@echo off
echo Start driver installations...
pnputil.exe /add-driver .\GraphicsDriver\Graphics\iigd_dch.inf /install
pnputil.exe /i /a .\IVSHMEM\ivshmem.inf
devcon.exe install .\DVServer\DVServer.inf Root\DVServer
echo Done driver installations...
echo Reboot the system...
shutdown /r

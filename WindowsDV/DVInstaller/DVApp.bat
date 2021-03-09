REM -------------------------------------
REM * Copyright © 2020 Intel Corporation
REM SPDX-License-Identifier: MIT
REM -------------------------------------

@echo off
echo Start enabling the DVClient
DVEnabler.exe
timeout 10
looking-glass-host.exe
timeout 10


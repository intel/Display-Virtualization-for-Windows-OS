#===========================================================================
#DVInstaller.ps1
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

Write-Host "Start Windows GFX installation..."
pnputil.exe /add-driver .\GraphicsDriver\Graphics\iigd_dch.inf /install

Write-Host "Start DVServerKMD installation..."
pnputil.exe /add-driver .\DVServerKMD\DVServerKMD.inf /install

Write-Host "Rebooting Windows VM in 10 secs..."
Timeout /T 10
Restart-Computer

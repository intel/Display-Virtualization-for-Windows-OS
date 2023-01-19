#===========================================================================
#DVInstaller.ps1
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

#check if file present
function is_present($filepath)
{
	$isavailable = Test-Path $filepath
	return $isavailable
}

#check if all the required binaries are present or not
function check_executables()
{
	if ((is_present("DVServer\dvserver.cat") -eq $true) -and
		(is_present("DVServer\DVServer.dll") -eq $true) -and
		(is_present("DVServer\DVServer.inf") -eq $true) -and
		(is_present("DVServer\dvserverkmd.cat") -eq $true) -and
		(is_present("DVServer\DVServerKMD.inf") -eq $true) -and
		(is_present("DVServer\DVServerKMD.sys") -eq $true) -and
		(is_present("DVEnabler.exe") -eq $true) -and
        (is_present("GraphicsDriver\Graphics\iigd_dch.inf") -eq $true)){
            Write-Host "Setup files present"
			return "SUCCESS"
	}
	Write-Host "Setup files don't exist.. Exiting.."
	return "FAIL"
}

##Main##
$ret = check_executables
if ($ret -eq "FAIL") {
	Exit
}
else {
	Write-Host "Start Windows GFX Driver installation..."
	pnputil.exe /add-driver .\GraphicsDriver\Graphics\iigd_dch.inf /install

	Write-Host "Start Zerocopy Driver installation..."
	pnputil.exe /add-driver .\DVServer\DVServerKMD.inf /install

	Write-Host "Checking DVServer loaded successfully..."
	while($true){
		$count= (Get-Process WUDFHost | select -ExpandProperty modules | group -Property FileName | select name | Select-String -Pattern 'dvserver.dll' -AllMatches).matches.count
		if ($count -eq 1) {
			break
		}
		else{
			continue
		}
	}
	
	Write-Host "Running DVEnabler..."
	& ".\DVEnabler.exe"
	if ($LASTEXITCODE -eq 0) {
		Write-Host "DVEnabler Success. DVServerUMD has taken over MSBDA!"
	} else {
		Write-Host "DVEnabler failed. DVServerUMD has not taken over MSBDA!"
	}

	Write-Host "Rebooting Windows VM..."
	Restart-Computer
}
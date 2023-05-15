#===========================================================================
#DVInstaller.ps1
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

#check if file present
$global:maxcount = 30
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
		(is_present("DVEnabler.dll") -eq $true) -and
        (is_present("GraphicsDriver\Graphics\iigd_dch.inf") -eq $true)){
            Write-Host "Setup files present"
			return "SUCCESS"
	}
	Write-Host "Setup files don't exist.. Exiting.."
	return "FAIL"
}

function start_dvenabler()
{
	#before removing the existing dvserver.dll, check if the dll is running as service
	#if it is running, then kill the service and then remove the dll.
	$status = tasklist /m | findstr "DVEnabler.dll"
	if ($status) {
		Write-Host "Kill the process before replacing the DVEnabler.dll"
		taskkill /F /FI "MODULES eq DVEnabler.dll" /T
	}

	Remove-Item -path C:\Windows\System32\DVEnabler.dll -ErrorAction SilentlyContinue
	$count = 0

	rundll32.exe DVEnabler.dll,dvenabler_init
	# Before Rebooting the system, we need to make sure that DVEnabler.dll has been kick started
	# So adding a loop with a counter of maxcount = 30, To check if DVEnabler has started
	# If DVEnabler doesnt get started before 30 iteration, we will exit the script.
	while($count -lt $maxcount){
		$dve = tasklist /m | findstr "DVEnabler.dll"
		if ($dve) {
			Write-Host "DVEnabler started as service ."
			break
		} else {
			Write-Host "Starting DVEnabler... Please wait..."
			$count ++
			continue
		}
	}
	if (!$dve) {
		Write-Host "unable to start DVEnabler... returning failure"
		return "FAIL"
	}

	#During system login, DVEnabler service will look for this DLL in system32 path,
	#This will load the previously saved display topology.
	#Copying DVEnabler.DLL to system32 is required as part of the installation
	cp DVEnabler.dll C:\Windows\System32

	#regiter a task to start the dvenabler.dll as a service during every user logon
	unregister-scheduledtask -TaskName "DVEnabler" -confirm:$false -ErrorAction SilentlyContinue
		$ac = New-ScheduledTaskAction -Execute "rundll32.exe"  -Argument "C:\Windows\System32\DVEnabler.dll,dvenabler_init"
		$tr = New-ScheduledTaskTrigger -AtLogOn
		$pr = New-ScheduledTaskPrincipal  -Groupid  "INTERACTIVE"
	Register-ScheduledTask -TaskName "DVEnabler" -Trigger $tr -TaskPath "\Microsoft\Windows\DVEnabler" -Action $ac -Principal $pr

	return "SUCCESS"
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
	$count = 0
	# Before starting DVEnabler.dll we should make sure that DVServer UMD has started or not
	# So adding a loop with a counter of maxcount = 30, To check if DVServer UMD has started
	# if DVserver UMD doesnt get started before 30 iteration, we will exit the script
	while($count -lt $maxcount){
		$dvs= tasklist /m | findstr "dvserver.dll"
		if ($dvs) {
			Write-Host "DVServer loaded successfully..."
			break
		} else {
			Write-Host "DVServer loading...Please wait..."
			$count ++
			continue
		}
	}
	if (!$dvs) {
		Write-Host "Failed to load DVServer UMD... Exiting"
		Exit
	}

	#call start_dvnabler to run the dvenabler.sll as a service
	$dve = start_dvenabler
	if ($dve -eq "SUCCESS") {
		Write-Host "Dvnabler succusfully started...."
		Write-Host "Rebooting Windows VM..."
		Restart-Computer
	} else {
		Write-Host "Fail to start Dvnabler.... Exiting!"
		Exit
	}

}
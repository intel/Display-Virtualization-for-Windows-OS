#===========================================================================
#DVInstaller.ps1
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

#check if file present
$global:maxcount = 10
function is_present($filepath)
{
	$isavailable = Test-Path $filepath
	return $isavailable
}

function log_file()
{
	$originalLogFile = "DvServer_log.txt"
	$incrementalLogFile = $originalLogFile
	$counter = 1

	while (Test-Path $incrementalLogFile) {
		$incrementalLogFile = "{0}-{1}.txt" -f $originalLogFile.Replace('.txt',''), $counter
		$counter++
	}

return $incrementalLogFile

}

#check if all the required binaries are present or not
function check_executables()
{
	$files = "DVServer\dvserver.cat", "DVServer\DVServer.dll", "DVServer\DVServer.inf", "DVServer\dvserverkmd.cat", "DVServer\DVServerKMD.inf", "DVServer\DVServerKMD.sys", "DVEnabler.dll"

	# ArrayList to store the names of missing files
	$missingFiles = New-Object System.Collections.ArrayList

	foreach ($file in $files) {
		if (-not (is_present $file)) {
			[void]$missingFiles.Add($file)
		}
	}

	# Output the result
	if ($missingFiles.Count -eq 0) {
		Write-Host "Setup files present"
	}
	else {
		Write-Host -ForegroundColor Red "The following files needed for installation are missing:"
		Write-Host -ForegroundColor Yellow ($missingFiles -join "`n")
		return "FAIL"
	}
}

function dvserver_cleanup()
{
		if(pnputil.exe /enum-drivers | findstr /i dvserverkmd.inf) {
			$driverPackage = Get-WmiObject -Class Win32_PnPSignedDriver | Where-Object { $_.DeviceName -eq "DVServerKMD Device" }
			if ($driverPackage) {
			$publishedName = $driverPackage.InfName
			pnputil.exe /delete-driver "$publishedName" /force /uninstall
			}
		}

		Write-Host -ForegroundColor Red "Driver Installation failed.. "
		Write-Host -ForegroundColor Red "Please Reboot the system and Run the Installation Script Again."
		Stop-Transcript

		Exit
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
	Write-Host "Starting DVEnabler... Please wait..."
	# Before Rebooting the system, we need to make sure that DVEnabler.dll has been kick started
	# So adding a loop with a counter of maxcount = 10, To check if DVEnabler has started
	# If DVEnabler doesn’t  get started before 10 iteration, we will exit the script.
	while($count -lt $maxcount){
		$dve = tasklist /m | findstr "DVEnabler.dll"
		if ($dve) {
			Write-Host "DVEnabler started as service ."
			break
		} else {
			$count ++
			Write-Host -NoNewline " $($count)/$maxcount Done `r"; sleep 1
			continue
		}
	}
	if (!$dve) {
		Write-Host -ForegroundColor Red "unable to start DVEnabler... returning failure"
		return "FAIL"
	}

	#During system login, DVEnabler service will look for this DLL in system32 path,
	#This will load the previously saved display topology.
	#Copying DVEnabler.DLL to system32 is required as part of the installation
	cp DVEnabler.dll C:\Windows\System32

	return "SUCCESS"
}

function Register_ScheduledTask()
{
	$TASK_SESSION_UNLOCK = 8 #TASK_SESSION_STATE_CHANGE_TYPE.TASK_SESSION_UNLOCK (taskschd.h)
	$TASK_SESSION_LOCK = 7  #TASK_SESSION_STATE_CHANGE_TYPE.TASK_SESSION_LOCK (taskschd.h)

	$stateChangeTrigger = Get-CimClass `
		-Namespace ROOT\Microsoft\Windows\TaskScheduler `
		-ClassName MSFT_TaskSessionStateChangeTrigger

	$onUnlockTrigger = New-CimInstance ` -CimClass $stateChangeTrigger `
		-Property @{ StateChange = $TASK_SESSION_UNLOCK } ` -ClientOnly

	$onLockTrigger = New-CimInstance ` -CimClass $stateChangeTrigger `
		-Property @{ StateChange = $TASK_SESSION_LOCK } ` -ClientOnly

	unregister-scheduledtask -TaskName "DVEnabler" -confirm:$false -ErrorAction SilentlyContinue
	unregister-scheduledtask -TaskName "StopDVEnabler" -confirm:$false -ErrorAction SilentlyContinue

	try {
		#Register a task to start the dvenabler.dll as a service during every user logon or user unlock
			$ac = New-ScheduledTaskAction -Execute "rundll32.exe"  -Argument "C:\Windows\System32\DVEnabler.dll,dvenabler_init"
			$tr = New-ScheduledTaskTrigger -AtLogOn
			# Use the SID for INTERACTIVE group
			$interactiveSID = "S-1-5-4"
			$pr = New-ScheduledTaskPrincipal  -Groupid  $interactiveSID -RunLevel Highest
			$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -ExecutionTimeLimit 0 -MultipleInstances Queue
		Register-ScheduledTask -TaskName "DVEnabler" -Trigger @($tr, $onUnlockTrigger) -TaskPath "\Microsoft\Windows\DVEnabler" -Action $ac -Principal $pr -Settings $settings -ErrorAction Stop

		#Register a task to Stop the dvenabler.dll during every user lock
			$ac = New-ScheduledTaskAction -Execute "powershell.exe"  -Argument "-ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -Command `"Stop-ScheduledTask -TaskName '\Microsoft\Windows\DVEnabler\DvEnabler'`""
			$pr = New-ScheduledTaskPrincipal  -Groupid  $interactiveSID -RunLevel Highest
			$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -ExecutionTimeLimit 0 -MultipleInstances Queue
		Register-ScheduledTask -TaskName "StopDVEnabler" -Trigger $onLockTrigger -TaskPath "\Microsoft\Windows\DVEnabler" -Action $ac -Principal $pr -Settings $settings -ErrorAction Stop
	} catch {
		Write-Host -ForegroundColor Red "An error occurred in Register-ScheduledTask: $($_.Exception.Message)"
		return "FAIL"
	}
	return "SUCCESS"

}

##Main##
$logfile = log_file
Start-Transcript -Path $logfile -Append

$ret = check_executables
if ($ret -eq "FAIL") {
	Stop-Transcript
	Exit
}
else {
	# Do a refresh before starting the installation.
	pnputil /scan-devices

	if ((is_present("DVServer.cer") -eq $true) -and
	(is_present("DVServerKMD.cer") -eq $true)){
		Write-Host "Start DVServer Certificate installation..."
		certutil -addstore root DVServer.cer
		certutil -addstore root DVServerKMD.cer
	}

	Write-Host "Start Zerocopy Driver installation..."
	pnputil.exe /add-driver .\DVServer\DVServerKMD.inf /install
	$count = 0
	# Before starting DVEnabler.dll we should make sure that DVServer UMD has started or not
	# So adding a loop with a counter of maxcount = 10, To check if DVServer UMD has started
	# if DVserver UMD doesn’t  get started before 10 iteration, we will exit the script
	Write-Host "DVServer loading...Please wait..."
	while($count -lt $maxcount){
		$dvs= tasklist /m | findstr "dvserver.dll"
		if ($dvs) {
			Write-Host "DVServer loaded successfully..."
			break
		} else {
			$count ++
			 Write-Host -NoNewline " $($count)/$maxcount Done `r"; sleep 1
			continue
		}
	}

	# Reset the count
	$count = 0

	if (!$dvs) {
		Write-Host "Trying to install DVServer UMD..."
		pnputil.exe /add-driver .\DVServer\DVServer.inf /install
		while($count -lt $maxcount){
			$dvs= tasklist /m | findstr "dvserver.dll"
			if ($dvs) {
				Write-Host "DVServer loaded successfully..."
				break
			} else {
				$count ++
				Write-Host -NoNewline " $($count)/$maxcount Done `r"; sleep 1
				continue
			}
		}

	}

	if (!$dvs) {
		Write-Host -ForegroundColor Red "Failed to load DVServer UMD..."
		dvserver_cleanup
	}

	#call start_dvenabler to run the dvenabler.dll as a service
	$dve = start_dvenabler
	if ($dve -eq "SUCCESS") {
		Write-Host "Dvenabler successfully  started...."
	} else {
		Write-Host -ForegroundColor Red "Failed to start DVEnabler...."

		#Before cleanup, make IDD as primary monitor , so that the user can see the PowerShell window

		# Get the second display device name
		$secondDisplay = (Get-WmiObject -Namespace "root\CIMV2" -Class Win32_VideoController | Select-Object -Skip 1 -First 1).Name

		# Set the second display as the primary display using DisplaySwitch.exe
		DisplaySwitch.exe /internal
		DisplaySwitch.exe /external:$secondDisplay

		# Restart the Explorer process to apply the changes
		Stop-Process -Name "explorer" -Force

		dvserver_cleanup
	}
	#Resgister the DVEnabler task during workstation lock and unlock
	$status = Register_ScheduledTask
	if ( $status -eq "FAIL") {
		Write-Host -ForegroundColor Red "Failed to register the scheduled task."
		Write-Host -ForegroundColor Red "Note: MULTI MONITOR and HOTPLUG features won't work until the task is successfully registered."
		Exit
	}
	Write-Host -ForegroundColor Green "INSTALLATION COMPLETED SUCCESSFULLY"
	Write-Host "Rebooting Windows VM..."
	Stop-Transcript
	Restart-Computer

}
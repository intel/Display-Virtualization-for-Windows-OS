#===========================================================================
#DVApp.ps1
#----------------------------------------------------------------------------
#* Copyright © 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

param ($param1)

#This API will validate the input command line params
function validate_input_params($param1)
{
	if (($param1 -eq "setup") -or ($param1 -eq "run")) {
		Write-Host "Input parameters are valid, continuing..."
	}
	else {
		Write-Host "Input parameters are invalid, try > DVApp.ps1 run (or) DVApp.ps1 setup"
		Write-Host "Exiting from powershell script..."
		return "FAIL"
	}
	return "SUCCESS"
}

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
		(is_present("DVEnabler.exe") -eq $true) -and 
		(is_present("DVServerUMD_Node.exe") -eq $true)){
			return "SUCCESS"
	}
	Write-Host "Setup files doesn't exist.. Exiting.."
	return "FAIL"
}

#This API will create the IDD display node and turn off MSFT display path
function create_node()
{
	if(Get-Process | Where Name -eq DVServerUMD_Node) {
		Write-Host "DVServerUMD Process is already running..." 
		return "FAIL"
	}

	Write-Host "Creating DVServer UMD node"
	Start-Process ".\DVServerUMD_Node.exe" -WindowStyle Hidden

	Start-Sleep -s 3
	Write-Host "Turning off MSFT Path"

	& ".\DVEnabler.exe"
	return "SUCCESS"
}

#This API will execute the setup/run commands
function execute_command($param1)
{
	if ( $param1 -eq "setup") {
		Write-Host "Installing new DVServer UMD driver into the Kernel PnP tree..."
		pnputil.exe /add-driver DVServer\DVServer.inf /install
	
		$ret = create_node
		if ($ret -eq "FAIL") {
			return "FAIL"
		}
		else {
			return "SUCCESS"
		}
	}

	if ( $param1 -eq "run") { 
		$ret = create_node
		if ($ret -eq "FAIL") {
			return "FAIL"
		}
		else {
			return "SUCCESS"
		}
	}
}


##Main##
$inputval = $param1

$ret = validate_input_params($inputval)
if ($ret -eq "FAIL") {
	Exit
}

$ret = check_executables
if ($ret -eq "FAIL") {
	Exit
}

$ret = execute_command($inputval)
if ($ret -eq "FAIL") {
	Exit
}

Write-Host "Exiting Powershell Script Successfully..."
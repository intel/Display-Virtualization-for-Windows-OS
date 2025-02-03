/*=============================================================================
; dvutil.cpp
;------------------------------------------------------------------------------
; Copyright (C) 2023 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   Implements the utility functions required for installer function
;-----------------------------------------------------------------------------*/

#include "DVInstaller.h"
#include "Trace.h"
#include "dvutil.tmh"

/*******************************************************************************
*
* Description
*
* dvGetInstalledOemInfFileName - Retrieves the Device passed OEM file name
*
* Parameters
*   deviceDesc - Device descrption of the driver.
*   infFileName - OEM INF file name from drive store.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
*******************************************************************************/
BOOL  dvGetInstalledOemInfFileName(const wchar_t* deviceDesc, WCHAR* infFileName) {

	BOOL result = FALSE;
	DWORD index = 0, size = 0;
	DEVPROPTYPE propType;
	wchar_t infpath[MAX_PATH];
	ULONG bufferSize = sizeof(infpath);
	wchar_t deviceName[MAX_PATH];
	HDEVINFO devInfoSet;
	SP_DEVINFO_DATA devInfo;

	TRACING();
	devInfoSet = SetupDiGetClassDevs(NULL, nullptr, nullptr, DIGCF_ALLCLASSES);
	if (devInfoSet == INVALID_HANDLE_VALUE) {
		ERR("failed in SetupDiGetClassDevs,\n");
		return result;
	}
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	for (index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfo); index++) {
		// Build a list of driver info items
		if (!SetupDiGetDeviceRegistryProperty(devInfoSet, &devInfo, SPDRP_DEVICEDESC,
			NULL, (PBYTE)deviceName, sizeof(deviceName), &size)) {
			continue;
		}
		if (wcsstr(deviceName, deviceDesc)) {
			DBGPRINT("\nDevice is found");
			if (SetupDiGetDeviceProperty(devInfoSet, &devInfo, &DEVPKEY_Device_DriverInfPath,
				&propType, (PBYTE)infFileName, bufferSize, &bufferSize, 0)) {
				DBGPRINT("\nDevice INF found");
				result = TRUE;
				break;
			}
		}
	}
	SetupDiDestroyDeviceInfoList(devInfoSet);
	return result;
}

/*******************************************************************************
*
* Description
*
* dvDeviceScan - Does the device scan.
*
* Parameters
*
* Return val
* BOOL - TRUE on success, FALSE on failure
******************************************************************************/
BOOL dvDeviceScan(void)
{
	HMACHINE machineHandle = NULL;
	DEVINST devRoot;

	TRACING();
	if (CM_Locate_DevNode_Ex(&devRoot, NULL, CM_LOCATE_DEVNODE_NORMAL, machineHandle) != CR_SUCCESS) {
		ERR("Failed to get device instance handle");
		return FALSE;
	}
	if (CM_Reenumerate_DevNode_Ex(devRoot, CM_REENUMERATE_NORMAL, machineHandle) != CR_SUCCESS) {
		ERR("Failed to enumerate the devices identified by device node");
		return FALSE;
	}
	DBGPRINT("Device Scan is Done");

	return TRUE;
}

/*******************************************************************************
*
* Description
*
* dvFileExists - Checks if file is present in the path of executable.
*
*
* Parameters
*   file - file name.
*   filePath - full path to the file
*
* Return val
* BOOL - TRUE on success, FALSE on failure
******************************************************************************/
BOOL dvFileExists(const std::wstring& file, std::wstring& filePath)
{
	wchar_t buffer[MAX_PATH];
	DWORD length;

	TRACING();
	length = GetModuleFileName(NULL, buffer, MAX_PATH);
	if (!(length != 0 && length < MAX_PATH)) {
		return FALSE;
	}
	std::wstring executablepath(buffer);
	std::wstring filepath = executablepath.substr(0, executablepath.find_last_of(L"\\") + 1);
	filePath = filepath + file;

	DWORD fileAttributes = GetFileAttributes(filePath.c_str());
	return (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes != FILE_ATTRIBUTE_DIRECTORY));
}

/*******************************************************************************
*
* Description
*
* dvAddCertificate - Adds the certifcate file to ROOT system store.
*
*
* Parameters
*   certFile - FULL path to the certificate file.
*
* Return val
* BOOL - TRUE on success, FALSE on failure
******************************************************************************/
BOOL dvAddCertificate(const std::wstring& certFile)
{
	HANDLE hKeyFile;
	PCCERT_CONTEXT  certContext;
	HCERTSTORE hCertStore;
	DWORD dwFileSize;
	DWORD bytesRead = 0;
	BOOL result = FALSE;

	TRACING();
	hKeyFile = CreateFile(certFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hKeyFile) {
		dwFileSize = GetFileSize(hKeyFile, NULL);
		if (dwFileSize) {
			std::vector<BYTE> certBuffer(dwFileSize);
			if (ReadFile(hKeyFile, certBuffer.data(), dwFileSize, &bytesRead, NULL)) {
				hCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, NULL, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"ROOT");
				if (hCertStore) {
					certContext = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certBuffer.data(), bytesRead);
					if (certContext) {
						if (CertAddCertificateContextToStore(hCertStore, certContext, CERT_STORE_ADD_REPLACE_EXISTING, nullptr)) {
							DBGPRINT("certificate added successfully to the root store");
							result = TRUE;
						}
						else {
							ERR("CertAddCertificateContextToStore:failed to add certificate");
						}
						CertFreeCertificateContext(certContext);
					}
					else {
						ERR("failed to get context");
					}
					if (!CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG)) {
						ERR("failed to close  store\n");
					}
				}
			}
		}
		CloseHandle(hKeyFile);
	}
	return result;
}

/*******************************************************************************
*
* Description
*
* dvUpdateCertifcates - Installs DVServer.cer and DVServerKMD.cer certificates.
*                       if no certificates files are present this API shall return
*                       EXIT_OK, since this is MSFT/intel signed driver.
*
* Parameters
*   NONE
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
******************************************************************************/
int dvUpdateCertifcates(void)
{
	std::wstring certFilePath;
	int result = EXIT_OK;

	TRACING();
	try {
		std::vector<std::wstring> certfiles = { L"DVServer.cer",L"DVServerKMD.cer" };
		for (const std::wstring& certfile : certfiles) {
			if (dvFileExists(certfile, certFilePath)) {
				if (!dvAddCertificate(certFilePath)) {
					DBGPRINT("\nFailed to Certificates ");
					result = EXIT_FAIL;
				}
				else {
					DBGPRINT("\nCertificates added successfully");
				}
			}
			else {
				DBGPRINT("\n No Certificate files found");
			}
		}
	}
	catch (const std::exception& e) {
		DBGPRINT("Exception in string allocation");
	}
	return result;
}

/*******************************************************************************
*
* Description
*
* dvGetInfPath - Retrieves infPath (full path) of file (infFile) by merging
*               current drive and directory.
*
* Parameters
*   infFile - INF filename.
*   infPath - full path of the INF.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
******************************************************************************/
int dvGetInfPath(TCHAR* infFile, TCHAR* infPath)
{
	DWORD res;

	TRACING();
	res = GetFullPathName(infFile, MAX_PATH, infPath, NULL);
	if ((res >= MAX_PATH) || (res == 0)) {
		ERR("\nLong file \n");
		return EXIT_FAIL;
	}
	if (GetFileAttributes(infPath) == (DWORD)(-1)) {
		ERR("infPath doesn't exist");
		return EXIT_FAIL;
	}
	return EXIT_OK;
}

/*******************************************************************************
*
* Description
*
* dvLoadUnloadNewdevLib Loads the module into the address space .
*
* Parameters
*   newdevMod - handle to the newdev.dll module.
*   isLoad -    specify's newdev.dll module to be loaded or unloaded.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
******************************************************************************/
int dvLoadUnloadNewdevLib(HMODULE* newdevMod, BOOL isLoad)
{
	TRACING();
	if (!newdevMod) {
		return EXIT_FAIL;
	}
	if (isLoad) {
		*newdevMod = LoadLibraryEx(TEXT("newdev.dll"), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!*newdevMod) {
			DWORD dwError = GetLastError();
			ERR("\nFailed to load NewdevLib. Error code: %lu\n", dwError);
			return EXIT_FAIL;
		}
	}
	else {
		if (*newdevMod) {
			if (!FreeLibrary(*newdevMod)) {
				ERR("\nFailed to unload NewdevLib \n");
				return EXIT_FAIL;
			}
		}
		else {
			ERR("\n*newdevMod is empty \n");
		}
	}
	return EXIT_OK;
}

/*******************************************************************************
*
* Description
*
* dvKillProcessByModuleName: Enumerates all the modules process owns to check if
*                      moduleName is present and kills the process if it owns.
*
* Parameters
*   processID - PID of the process.
*   moduleNameToKill - specifies the module name to be found and terminated.
*
* Return val
* none.
*
*******************************************************************************/
void dvKillProcessByModuleName(DWORD processID, std::wstring moduleNameToKill)
{
	HANDLE hProcess;
	TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");
	TCHAR modulePath[MAX_PATH];
	std::wstring dllname = std::move(moduleNameToKill);
	HMODULE hModules[1024];
	DWORD moduleSize, moduleCount, j;

	TRACING();
	// Get a handle to the process.
	hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
		| PROCESS_TERMINATE,
		FALSE, processID);
	if (NULL != hProcess) {
		if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &moduleSize)) {
			moduleCount = moduleSize / sizeof(HMODULE);
			for (j = 0; j < moduleCount; j++) {
				if (GetModuleFileNameEx(hProcess, hModules[j], modulePath, sizeof(modulePath) / sizeof(TCHAR))) {
					try {
						std::wstring moduleName = modulePath;
						size_t pos = moduleName.find_last_of(L"\\");
						if (pos != std::wstring::npos) {
							moduleName = moduleName.substr(pos + 1);
							if (moduleName == dllname) {
								if (TerminateProcess(hProcess, 0)) {
									DBGPRINT(" Terminated");
								}
								else {
									ERR("failed to terminate process ");
								}
								break;
							}
						}
					}
					catch (const std::exception& e)
					{
						DBGPRINT("an exception occurred while terminating the process");
					}
				}
			}
		}
		CloseHandle(hProcess);
	}
}

/*******************************************************************************
*
* Description
*
* dvKillDll: Enumerates all the process in the system and kills the specified
*            module.
*
* Parameters
*   moduleName - Name of the module to be terminated.
*
*
******************************************************************************/
void dvKillDll(std::wstring moduleName)
{
	DWORD process[1024], ProcessSize, numProcesses;
	unsigned int i;

	TRACING();
	if (EnumProcesses(process, sizeof(process), &ProcessSize)) {
		numProcesses = ProcessSize / sizeof(DWORD);
		for (i = 0; i < numProcesses; i++) {
			if (process[i] != 0) {
				try {
					dvKillProcessByModuleName(process[i], moduleName);
				}
				catch (const std::bad_alloc& e) {
					DBGPRINT("exception occurred while try to terminate");
				}
			}
		}
	}
}

/*******************************************************************************
*
* Description
*
* dvGetInstalledOemInfFile: retrieves OEM INF file path from the driver store.
*
* Parameters
*   infFullPath - Installed Driver OEM INF file with full path from drive store.
*
* Return val
* int - 0 = EXIT_OK, 1 = EXIT_FAIL
*
******************************************************************************/
int  dvGetInstalledOemInfFile(WCHAR* infFullPath) {

	int result = EXIT_FAIL;
	DWORD index = 0, size = 0;
	wchar_t filter_device_desc[] = DVSERVERKMD_DESC;
	DEVPROPTYPE propType;
	wchar_t infpath[MAX_PATH];
	ULONG bufferSize = sizeof(infpath);
	wchar_t deviceName[MAX_PATH];
	HDEVINFO devInfoSet;
	SP_DEVINFO_DATA devInfo;

	TRACING();
	devInfoSet = SetupDiGetClassDevs(&ClassGuid, nullptr, nullptr, DIGCF_PRESENT);
	if (devInfoSet == INVALID_HANDLE_VALUE) {
		return result;
	}
	devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
	for (index = 0; SetupDiEnumDeviceInfo(devInfoSet, index, &devInfo); index++) {
		// Build a list of driver info items
		if (!SetupDiGetDeviceRegistryProperty(
			devInfoSet, &devInfo, SPDRP_DEVICEDESC, NULL, (PBYTE)deviceName, sizeof(deviceName), &size)) {
			continue;
		}
		if (wcsstr(deviceName, filter_device_desc)) {
			DBGPRINT("DVServerKMD Device is found,\n");
			if (SetupDiGetDeviceProperty(devInfoSet, &devInfo, &DEVPKEY_Device_DriverInfPath, &propType, (PBYTE)infpath, bufferSize, &bufferSize, 0)) {
				DBGPRINT("DVServerKMD Device OEM file found");
				if (SetupGetInfDriverStoreLocation(infpath, nullptr, nullptr, infFullPath, MAX_PATH, nullptr)) {
					DBGPRINT("DVServerKMD Device OEM file path");
					result = EXIT_OK;
					break;
				}
			}
		}
	}
	SetupDiDestroyDeviceInfoList(devInfoSet);
	return result;
}
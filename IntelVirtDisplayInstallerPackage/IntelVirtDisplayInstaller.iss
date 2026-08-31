;===========================================================================
;IntelVirtDisplayInstaller.iss
;----------------------------------------------------------------------------
;Copyright (C) 2023 Intel Corporation
;SPDX-License-Identifier: MIT
;--------------------------------------------------------------------------*/

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{769FF432-2F41-473A-B54D-183036E27F01}
AppName=IntelVirtDisplay
AppVersion={#MyAppVersion}
VersionInfoVersion={#MyVersionInfoVersion}
VersionInfoCopyright=Copyright (C) 2021 Intel Corporation
AppPublisher=intel
CreateAppDir=no
AppPublisherURL="https://www.intel.com/
LicenseFile=LICENSE
OutputBaseFilename=IntelVirtDisplayInstallerPackage
Compression=lzma
SolidCompression=yes
WizardStyle=modern
AlwaysRestart=yes

Uninstallable=yes
CreateUninstallRegKey=yes

UninstallFilesDir={app}

ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

;TO-DO list
MissingRunOnceIdsWarning=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]

Source: "{#IntelVirtDisplay}\IntelVirtDisplay.cer"; DestDir: "{tmp}"; Flags: skipifsourcedoesntexist deleteafterinstall
Source: "{#IntelVirtDisplay}\IntelVirtDisplayKMD.cer"; DestDir: "{tmp}"; Flags: skipifsourcedoesntexist deleteafterinstall 
Source: "{#IntelVirtDisplay}\IntelVirtDisplayEnabler.dll"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayUMD}\IntelVirtDisplay.cat"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayUMD}\IntelVirtDisplay.dll"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayUMD}\IntelVirtDisplay.inf"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayKMD}\IntelVirtDisplaykmd.cat"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayKMD}\IntelVirtDisplayKMD.inf"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayKMD}\IntelVirtDisplayKMD.sys"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#IntelVirtDisplayInstaller}\IntelVirtDisplayInstaller.exe"; DestDir: "{app}\DV"; Flags: uninsneveruninstall
Source: "{#IntelVirtDisplayInstaller}\IntelVirtDisplayInstaller.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Run]
Filename: "taskkill.exe"; Parameters: "/F /FI ""MODULES eq DVEnabler.dll"" /T"; Flags: runhidden;
Filename: "taskkill.exe"; Parameters: "/F /FI ""MODULES eq DVServer.dll"" /T"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""DVEnabler"" /F"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""StopDVEnabler"" /F"; Flags: runhidden;
Filename: "{sys}\cmd.exe"; Parameters: "/C del /F /Q ""{sys}\DVEnabler.dll"""; Flags: runhidden;
Filename: "{tmp}\IntelVirtDisplayInstaller.exe"; Parameters:{code:GetInstallParameter}; Flags:shellexec runasoriginaluser waituntilterminated; AfterInstall: DeleteTempFilesRestart

[UninstallRun]
Filename: "taskkill.exe"; Parameters: "/F /FI ""MODULES eq IntelVirtDisplayEnabler.dll"" /T"; Flags: runhidden;
Filename: "taskkill.exe"; Parameters: "/F /FI ""MODULES eq DVEnabler.dll"" /T"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""\Microsoft\Windows\IntelVirtDisplayEnabler\IntelVirtDisplayEnabler"" /F"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""\Microsoft\Windows\IntelVirtDisplayEnabler\StopIntelVirtDisplayEnabler"" /F"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""\Microsoft\Windows\DVEnabler\DVEnabler"" /F"; Flags: runhidden;
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""\Microsoft\Windows\DVEnabler\StopDVEnabler"" /F"; Flags: runhidden;
Filename: "{sys}\cmd.exe"; Parameters: "/C del /F /Q ""{sys}\IntelVirtDisplayEnabler.dll"""; Flags: runhidden;
Filename: "{sys}\cmd.exe"; Parameters: "/C del /F /Q ""{sys}\DVEnabler.dll"""; Flags: runhidden;
Filename: "{app}\DV\IntelVirtDisplayInstaller.exe"; Parameters:{code:GetUninstallParameter1}; Flags:shellexec waituntilterminated;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\DV"

[Code]
var
pageOption : TInputOptionWizardPage;
Installoption: Integer;
InstalledVersion: Integer;
ForceClose: Boolean;
IsLegacyInstall: Boolean;

function GetSourceDir(Param: String): String;
begin
  Result := ExpandConstant('{Param:dynamicfile}');
end;

function GetInstallParameter(Default:String): String;
var
cmdline: String;
begin
  cmdline := '1';
  if Installoption = 3 then
    cmdline := '3';
  if Installoption = 4 then
    cmdline := '4';
  Result := cmdline;
end;

function GetUninstallParameter1(Default:String): String;
var
cmdline: String;
begin
  cmdline := '2';
  
  Result := cmdline;
end;
procedure DeleteTempFiles;
begin
    DelTree(ExpandConstant('{tmp}'), True, True, True)
end;

procedure DeleteTempFilesRestart;
begin
    DeleteTempFiles
end;

Procedure CreateScript;
var
  BatchScriptPath: String;
  BatchScript: TStringList;
 begin
  BatchScriptpath := ExpandConstant('{tmp}\RunMainExe.bat');
  BatchScript := TstringList.Create;
  BatchScript.Add(ExpandConstant('"{tmp}\integrateexe.exe"'));
  BatchScript.Add('if errorlevel 0 goto :DeleteTempFiles');
  BatchScript.Add('goto :End');
  BatchScript.Add(':DeleteTempFiles');
  BatchScript.Add(ExpandConstant(' rmdir /s /q "{tmp}"'));
  BatchScript.Add(':End');
  BatchScript.SaveToFile(BatchScriptpath)
  BatchScript.Free
 end;

 function InitializeSetup: Boolean;
 begin
    CreateScript;
    Result :=True
 end;
 
function UninstallNeedRestart(): Boolean;
begin
  Result := True;
end;


procedure InitializeWizard();
var

  major,minor,patch,build: Integer;
  VersionString: String;
  InstalledDriverVersion: String;
  subkeynames: TArrayOfString;
  checkregkeypath, findkmdregkeyname, kmdregkeyvalue, kmddriverversion: String;
  kmdregkeyrootpath : array[1..2] of String;
  i, j: Integer;
  
begin
  kmdregkeyrootpath[1] := 'SYSTEM\CurrentControlSet\Control\Class\{4d36e97d-e325-11ce-bfc1-08002be10318}';
  kmdregkeyrootpath[2] := 'SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}';
  findkmdregkeyname := 'DriverDesc';
  i := 0;
  j := 0;
  InstalledVersion := 0;
  IsLegacyInstall := False;
  
  // Get the device registry details info for IntelVirtDisplayKMD or legacy DVServerKMD
  for j := 1 to GetArrayLength(kmdregkeyrootpath) do begin
    if (InstalledVersion = 0) then begin
      if (RegGetSubkeyNames(HKEY_LOCAL_MACHINE, kmdregkeyrootpath[j], subkeynames)) then begin
          for i := 0 to GetArrayLength(subkeynames)-1 do begin    
              checkregkeypath := '';
              checkregkeypath := kmdregkeyrootpath[j] + '\' + subkeynames[i];
              Log(checkregkeypath);          
              
              // retrieve Driver key matching "IntelVirtDisplayKMD Device" or legacy "DVServerKMD Device"
              RegQueryStringValue(HKEY_LOCAL_MACHINE, checkregkeypath, findkmdregkeyname, kmdregkeyvalue)
              if (kmdregkeyvalue = 'IntelVirtDisplayKMD Device') or (kmdregkeyvalue = 'DVServerKMD Device') then begin
                  if (kmdregkeyvalue = 'DVServerKMD Device') then
                      IsLegacyInstall := True;
                  
                  if (RegQueryStringValue(HKEY_LOCAL_MACHINE, checkregkeypath, 'DriverVersion', VersionString)) then begin
                    Log(VersionString);
                    Log('RegKey Found!!!!! Exiting loop!!!!');
                    VersionString := VersionString + ' ';
                    InstalledVersion := 1;
                    break;
                  end
                  else begin
                    Log('KMD DriverVersion NOT Found! Exiting!!!');
                    InstalledVersion := 0;
                  end;
              end
              else begin
              InstalledVersion := 0;
              end; 
          end;//end of loop
      end
      else begin
        Log('IntelVirtDisplayKMD Registry NOT Found!');
          InstalledVersion := 0;
      end;
    end;
  end; // end of main loop
  
  Installoption := 1;
  if InstalledVersion = 1 then
  begin
    pageOption := CreateInputOptionPage(
      wpLicense,
      'Driver installed Page',
      'Select an option',
      VersionString + 'is installed please choose for clean install',
      True, False
    );
    pageOption.Add('update');
    pageOption.Add('uninstall');
    InstalledDriverVersion := VersionString;
  end;
end;

function GetUninstallExe(): String;
var
  unInstExePath: String;
  unInstallExeString: String;
begin
  unInstExePath := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1');
  unInstallExeString := '';
  if not RegQueryStringValue(HKLM, unInstExePath, 'UninstallString', unInstallExeString) then
    RegQueryStringValue(HKCU, unInstExePath, 'UninstallString', unInstallExeString);
  Result := unInstallExeString;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  Confirm:= not ForceClose;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  unInstallExeString: String;
  iResultCode: Integer;
begin
  Result:=True;
  if InstalledVersion = 1 then
    if CurPageID = pageOption.ID then
    begin
      if pageOption.Values[0] then
      begin
        if IsLegacyInstall then
          Installoption := 4
        else
          Installoption := 3;
      end;
      if pageOption.Values[1] then
      begin
       unInstallExeString := GetUninstallExe();
       if unInstallExeString <> '' then
       begin
        unInstallExeString := RemoveQuotes(unInstallExeString);
        if Exec(unInstallExeString, '/SILENT' ,'', SW_HIDE, ewWaitUntilTerminated, iResultCode) then
        begin
          ForceClose:= True;
          WizardForm.Close;
          Installoption := 4;
        end;
       end;
       end;
    end;
end;

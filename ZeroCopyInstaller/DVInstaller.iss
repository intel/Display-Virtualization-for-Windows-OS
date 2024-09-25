;===========================================================================
;DVInstaller.iss
;----------------------------------------------------------------------------
;Copyright (C) 2023 Intel Corporation
;SPDX-License-Identifier: MIT
;--------------------------------------------------------------------------*/

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{769FF432-2F41-473A-B54D-183036E27F01}
AppName=ZeroCopy
AppVersion=1.0
AppPublisher=intel
CreateAppDir=no
AppPublisherURL="https://www.intel.com/
LicenseFile=LICENSE
OutputBaseFilename=ZeroCopyInstaller
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

Source: "{#DvServer}\DVServer.cer"; DestDir: "{tmp}"; Flags: skipifsourcedoesntexist deleteafterinstall
Source: "{#DvServer}\DVServerKMD.cer"; DestDir: "{tmp}"; Flags: skipifsourcedoesntexist deleteafterinstall 
Source: "{#DvServer}\DVEnabler.dll"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerUMD}\dvserver.cat"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerUMD}\DVServer.dll"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerUMD}\DVServer.inf"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerKMD}\dvserverkmd.cat"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerKMD}\DVServerKMD.inf"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvServerKMD}\DVServerKMD.sys"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvInstaller}\DVInstaller.exe"; DestDir: "{app}\DV"; Flags: uninsneveruninstall
Source: "{#DvInstaller}\DVInstaller.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
Source: "{#DvInstallerlib}\DVInstallerLib.dll"; DestDir: "{tmp}"; Flags: dontcopy 

[Run]

Filename: "{tmp}\DVInstaller.exe"; Parameters:{code:GetInstallParameter}; Flags:shellexec runasoriginaluser waituntilterminated; AfterInstall: DeleteTempFilesRestart

[UninstallRun]
Filename: "{app}\DV\DVInstaller.exe"; Parameters:{code:GetUninstallParameter1};  Flags:shellexec waituntilterminated;
[UninstallDelete]
Type: filesandordirs; Name: "{app}\DV"

[Code]
var
pageOption : TInputOptionWizardPage;
Installoption: Integer;
InstalledVersion: Integer;
ForceClose: Boolean;


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
 

function GetStringfromRegister(var major,minor,patch,build: Integer): Integer;
external 'GetStringfromRegister@files:DVInstallerLib.dll cdecl';

function UninstallNeedRestart(): Boolean;
begin
  Result := True;
end;


procedure InitializeWizard();
var

  major,minor,patch,build: Integer;
  VersionString: String;
  InstalledDriverVersion: String;
  
begin
  InstalledVersion :=0;
  InstalledVersion := GetStringfromRegister(major,minor,patch,build);
  VersionString := Format('%d.%d.%d.%d ',[major,minor,patch,build]);
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
         Installoption := 3;
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

#===========================================================================
#Readme.txt
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

-----------------------------------------------------------
#####  Windows VM QEMU CMD Example #####
-----------------------------------------------------------
sudo qemu-system-x86_64 -m 4096 -enable-kvm -cpu host -smp cores=4,threads=2,sockets=1 -drive file=<WindowsOS.img>.img,format=qcow2,cache=none -device vfio-pci,host=0000:00:02.2 -device e1000,netdev=net0,mac=DE:AD:BE:EF:1C:00 -netdev tap,id=net0 -device virtio-vga,max_outputs=4,blob=true -display gtk,gl=on,full-screen=<on/off>,connectors.0 = <display-port-name>, connectors.1=<display-port-name>, connectors.2=<display-port-name>, connectors.3=<display-port-name>, show-fps=on -object memory-backend-memfd,id=mem1,hugetlb=on,size=4096M -machine memory-backend=mem1

Note:
use the below command to get the display port name for that particular board
“cat /sys/kernel/debug/dri/0/i915_display_info”

-----------------------------------------------------------
##### Pre-requisites #####
-----------------------------------------------------------
1) Use the above QEMU cmd and boot to Windows VM
2) Copy the Signed Zero Copy Binaries and Windows GFX driver to the VM
3) If we are building driver manually from Visual Studio, then copy DVServer.cer & DVServerKMD.cer along with other drivers
4) To install manual drivers, system requires "bcdedit /set testsigning on" & reboot before the installation
5) Driver update may fail when switching from manual test signed driver to production driver. To fix this, we need to disable "Prioritize all digitally signed drivers equally during the driver ranking and selection process" in Group Policy (Path: Computer Configuration - Administrative Templates - System - Device Installation)

-----------------------------------------------------------
#####  Configuring 0Copy  #####
-----------------------------------------------------------
1) Run this command in Powershell (admin mode). DVInstaller Script will be signed, follow below step before running the DVInstaller
		> Set-ExecutionPolicy -ExecutionPolicy AllSigned -Scope CurrentUser
		> This will prompt user to allow access, Press “Y/Yes” to continue
2) Create a folder with name "GraphicsDriver\Graphics" in the location where we have "DVInstaller.ps1" and extract the GFX driver zip into that folder
3) Make sure ZC files copied in respective folder structure -
	3.1) DVServer\dvserverkmd.cat, DVServer\DVServerKMD.inf, DVServer\DVServerKMD.sys
	3.2) DVServer\dvserver.cat, DVServer\DVServer.dll, DVServer\DVServer.inf
	3.3) DVEnabler.dll
4) GFX Installation and ZC Installation
	4.1) Open powershell in admin mode and goto ZeroCopy binary folder
	4.2) Run : ".\DVInstaller.ps1" - This will install GFX driver, ZC driver and start the DVEnabler as a windows service
	4.3) Auto reboot will happen
5) After successfully rebooting, check below driver entry inside device manager. There should not be a yellow bang on any of the below drivers.
	5.1) Display Adapter --> GFX driver
	5.2) System -->DVServerKMD driver
	5.3) Display Adapter -->DVServerUMD Device
6) After subsequent reboot, ZC will be kicked in automatically (No need to run any other script for ZC)
7) To update the Graphics/ZC drivers, Just repeat steps 2-5

------------------------------------------------------------------
#####  Installation of VIOSerial and QGA for S4 support  #####
------------------------------------------------------------------
1)  Download & extract “virtio-win-0.1.221.iso” from https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-0.1.221-1/virtio-win.iso
2)  We need to install 2 components, vioserial & qemu-guest-agent
    2.1) Open Powershell in admin mode and goto root folder of extracted ISO
	2.2) Installing VIOSerial: pnputil.exe /add-driver .\vioserial\w10\amd64\vioser.inf /install
	2.3) Installing qemu-guest agent: Start-Process .\guest-agent\qemu-ga-x86_64.msi
3) After successful installtion check the componets are functioning properly or not
	3.1) In services.msc, we need to check if QEMU Guest Agent in running state
	3.2) Open Device Manager --> under system devices --> VirtIO Serial Driver should showup

-----------------------------------------------------------
#####  DVServer Unit Level Testing (ULT) Application  #####
-----------------------------------------------------------
- DVServerULT will imitate DVServerUMD which will talk to DVServerKMD through realtime IOCTL & run below tests.
- Basically, it will help to isolate any complex issues on the DVServerKMD
- ULT is an user mode application, this will test the VirtIO GPU pipeline. The ULT will directly interact with KMD via IoCTL's and send the solid color image frame address. 
- Apart from this, it can get the supported QEMU EDID and displays all the supported modes

IMPORTANT NOTE : This is a parallel UMD component (like DVServerUMD), so we should never attempt to run this ULT when the DVServerUMD is streaming the display frame buffer address

Steps to avoid DVServerUMD:
1. Install ZC with above method
2. Go to Device manager -->Display Adapter -->DVServerUMD Device & disable
3. Screen freeze will be expected & try to reboot the VM from QEMU windows
4. After reboot, ZC will be disabled
5. Now we can run DVServerULT.exe as mentioned below.

How to Use:
[CMD] .\DVServerULT.exe <testcase name>
Entering the tescase number as a command line arguement is optional. If no arguement is provided, the list of test cases will be displayed.
edidinfo --> Will display the QEMU supported resolutions and EDID info
solidframe --> this will perform "Static Frame Test". From list of resolutions, select the resolution for which the static color frame is to be generated and displayed on QEMU
multicolor --> this will perform "Multicolor Frames Test". From list of resolutions, select the resolution for which the multi color frame to be generated and displayed on QEMU
multipattern --> this will perform "Multicolor Multiresolution Frames Test". For all the resolutions, multicolor frames are to be generated displayed in a continuous loop on QEMU

-----------------------------------------------------------
#####  Steps to capture and decode the ETL trace  #####
-----------------------------------------------------------

Step-1
Create 3 folders and copy the contents as described below
1.bin /*copy tracelog,tracefmt,tracepdb from the kit or can run directly from the windows kit/pplatform folder*/
2.pdb /*copy DVServer and DVServerkmd*/
3.tracing /* create guid.txt*/

Add the below content to guid.txt
5E6BE9AC-16AC-40C9-BBC1-A7D39E3F463F;DVEnablerGuid
351BC0B2-53AB-4C14-8851-3B80F878BADC;DVserverUMDGuid
DB7C7BAE-6D56-4DF0-8807-48F2FB30E3D1;DVserverKMDGuid

Step-2
Run the below command from the PDB file directory
..\bin\tracepdb.exe *

Step-3
cd tracing and run the below 2 commands.
command 1:
		..\bin\tracelog -rt -enable -start MyTrace -f test.etl -guid guid.txt -flag 0xffff -level 5
command 2:
		..\bin\tracefmt -rt MyTrace -p <TMF path> -pdb <PDB_PATH> -display -o realtime.txt
		<TMF path> and <PDB_PATH> can be same( as well different) and corresponds to the path in steps 2

Step-4
Run below command once trace capture is done
bin\tracelog.exe -stop Mytrace

Step-5
open realtime.txt from tracing dir which contains the traces.
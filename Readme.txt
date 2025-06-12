DVServer is a display virtualization driver designed for Windows VMs. The required environment for this solution to function end-to-end includes a Linux host with QEMU hypervisor and a Windows VM. This driver consists of four components.
 
1. DVServerUMD:
This driver leverages Microsoft IDD (Indirect Display Driver) as its foundational technology to deliver a virtual display for Windows VMs. It retrieves the frame buffer from the OS swapchain buffer and shares the frame buffer address with DVServerKMD. Communication between DVServerUMD and DVServerKMD is facilitated through IOCTLs.
 
2. DVServerKMD:
This driver employs the VirtIO protocol for communication with the host QEMU hypervisor. The frame buffer address obtained from DVServerUMD is forwarded to the host QEMU hypervisor, which subsequently renders the frames on the host.
 
3. DVEnabler:
DVEnabler helps disable the MSBDA monitor, allowing the Intel graphics driver to attach to the IDD monitor for executing workloads on the GPU.
 
4. DVInstaller:
DVInstaller, built on the Inno Setup framework, facilitates the installation of our DVServer driver. It supports both GUI and command-line installation methods.
 
Key Feature Supported by the driver:
Win10/11 support, Multi monitor, Hot Plug Detect, EDID Management

-----------------------------------------------------------
#####  Windows VM QEMU CMD Example #####
-----------------------------------------------------------
1) QEMU command without "connectors" parameter (no association with external physical display)
Command: sudo qemu-system-x86_64 -m 4096 -enable-kvm -cpu host -smp cores=4,threads=2,sockets=1 -drive file=<WindowsOS.img>.img,format=qcow2,cache=none -device vfio-pci,host=0000:00:02.2 -device e1000,netdev=net0,mac=DE:AD:BE:EF:1C:00 -netdev tap,id=net0 -device virtio-vga,max_outputs=4,blob=true -display gtk,gl=on,full-screen=<on/off>, show-fps=on -object memory-backend-memfd,id=mem1,hugetlb=on,size=4096M -machine memory-backend=mem1

2) Associate QEMU with External Physical Display: For this we need to use below QEMU patches with "connectors" parameter enabled
QEMU Patches: https://github.com/intel/iotg-yocto-ese-main/tree/master/recipes-devtools/qemu/qemu
Command: sudo qemu-system-x86_64 -m 4096 -enable-kvm -cpu host -smp cores=4,threads=2,sockets=1 -drive file=<WindowsOS.img>.img,format=qcow2,cache=none -device vfio-pci,host=0000:00:02.2 -device e1000,netdev=net0,mac=DE:AD:BE:EF:1C:00 -netdev tap,id=net0 -device virtio-vga,max_outputs=4,blob=true -display gtk,gl=on,full-screen=<on/off>,connectors.0 = <display-port-name>, connectors.1=<display-port-name>, connectors.2=<display-port-name>, connectors.3=<display-port-name>, show-fps=on, hw-cursor=on -object memory-backend-memfd,id=mem1,hugetlb=on,size=4096M -machine memory-backend=mem1

Note:
use the below command to get the display port name for that particular board (for "connectors" parameter)
“cat /sys/kernel/debug/dri/0/i915_display_info”

"hw-cursor=on" added to support HW cursor feature in Windows guest

-----------------------------------------------------------
##### Pre-requisites #####
-----------------------------------------------------------
1) Use the above QEMU cmd and boot to Windows VM
2) Copy the Signed Zero Copy Binaries to the VM
3) If we are building driver manually from Visual Studio, then copy DVServer.cer & DVServerKMD.cer along with other drivers
4) To install manual drivers, system requires "bcdedit /set testsigning on" & reboot before the installation
5) Driver update may fail when switching from manual test signed driver to production driver. To fix this, we need to disable "Prioritize all digitally signed drivers equally during the driver ranking and selection process" in Group Policy (Path: Computer Configuration - Administrative Templates - System - Device Installation)

-----------------------------------------------------------
#####  Configuring 0Copy  #####
-----------------------------------------------------------
1) Run this command in Powershell (admin mode). DVInstaller Script will be signed, follow below step before running the DVInstaller
		> Set-ExecutionPolicy -ExecutionPolicy AllSigned -Scope CurrentUser
		> This will prompt user to allow access, Press “Y/Yes” to continue
2) Make sure ZC files copied in respective folder structure -
	2.1) DVServer\dvserverkmd.cat, DVServer\DVServerKMD.inf, DVServer\DVServerKMD.sys
	2.2) DVServer\dvserver.cat, DVServer\DVServer.dll, DVServer\DVServer.inf
	2.3) DVEnabler.dll
3) ZC Installation
	3.1) Open powershell in admin mode and goto ZeroCopy binary folder
	3.2) Run : ".\DVInstaller.ps1" - This will install ZC driver and start the DVEnabler as a windows service
	3.3) Auto reboot will happen
4) After successfully rebooting, check below driver entry inside device manager. There should not be a yellow bang on any of the below drivers.
	4.1) System -->DVServerKMD driver
	4.2) Display Adapter -->DVServerUMD Device
5) After subsequent reboot, ZC will be kicked in automatically (No need to run any other script for ZC)
6) To update the ZC drivers, Just repeat steps 2-5

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

---------------------------------------------------------------------------
#####  Steps to install Drivers using DVInstaller in commandline  #####
---------------------------------------------------------------------------

1. Go to the zerocopy installer directory.
2. Run below specified command and select yes in UAC prompt

With restart: Installs the driver and restarts the system.
Command: ZeroCopyInstaller.exe /VERYSILENT /SUPPRESSMSGBOXES

Without restart: Installs the driver without any restart of the system.
Command: ZeroCopyInstaller.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART

/VERYSILENT: Runs silently without displaying windows.
/SUPPRESSMSGBOXES: Supress message boxes any displayed.
/NORESTART: Avoid the restart of the system after installer.

----------------------------------------------------------------
#####  Steps to install Drivers using GUI DVInstaller  #####
----------------------------------------------------------------

1. Go to the zerocopy installer directory.
2. Run ZeroCopyInstaller.exe and select yes in UAC prompt.

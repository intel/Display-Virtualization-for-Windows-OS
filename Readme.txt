#===========================================================================
#Readme.txt
#----------------------------------------------------------------------------
#* Copyright © 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

-----------------------------------------------------------
#####  Windows VM QEMU CMD Example #####
-----------------------------------------------------------
sudo qemu-system-x86_64 -m 4096 -enable-kvm -cpu host -smp cores=4,threads=2,sockets=1 -drive file=<WindowsOS.img>.img,format=qcow2,cache=none -usb -usbdevice tablet -device vfio-pci,host=0000:00:02.2 -device e1000,netdev=net0,mac=DE:AD:BE:EF:1C:00 -netdev tap,id=net0 -device virtio-vga,blob=true -display gtk,gl=on,show-fps=on -object memory-backend-memfd,id=mem1,hugetlb=on,size=4096M -machine memory-backend=mem1

-----------------------------------------------------------
##### Common Steps #####
-----------------------------------------------------------
1) Use the above QEMU cmd and boot to Windows VM
2) Copy the Zero Copy Binaries and Windows GFX driver to the VM

-----------------------------------------------------------
#####  Using "Signed" 0Copy Binaries (DVServerKMD)  #####
-----------------------------------------------------------
1) Run these commands
	1.1) Open Powershell / CMD prompt in admin mode and run the below command
		>powershell Set-ExecutionPolicy RemoteSigned
	1.2) Reboot the windows VM

2) Create a folder with name "GraphicsDriver\Graphics" in the location where we have "DVInstaller.ps1" and extract the GFX driver zip into that folder
3) GFX Installation and ZC DVServerKMD Installation 
	3.1) Open powershell in admin mode and goto ZeroCopy binary folder 
	3.2) run : ".\DVInstaller.ps1" this will install both GFX driver and DVServerKMD
4) Reboot the Windows VM
5) After successful reboot, check the device manager "Display Adapter --> GFX driver" and "System -->DVServerKMD driver" is loaded properly or not

-----------------------------------------------------------
#####  Using "Unsigned" 0Copy Binaries (DVServerKMD) #####
-----------------------------------------------------------
1) Run these commands
	1.1) Open Powershell / CMD prompt in admin mode and run the below commands
		> bcdedit /set testsigning on
		> powershell Set-ExecutionPolicy RemoteSigned
	1.2) Reboot the windows VM

2) GFX Installation : Open Device Manager --> Display Adapter --> Select the "MSBDA" which is having the YellowBang (PCI ID 0.2.0) --> this is our SRIOV GFX VF device and install the GFX driver via "Have Disk" method > Choose DCH driver in the driver selection during installation
3) After a successful installation ensure the GFX driver is loaded properly with the correct version number
4) Zero Copy KMD Installation : Open Device Manager --> Display Adapter --> Select the other "MSBDA" which will have the PCI ID as 0.4.0 --> go to DVServerKMD folder and install this via "Have Disk" method
5) After a successful installation an entry will be created under system device with a name "DVServerKMD Device"
6) Reboot the Windows VM

-----------------------------------------------------------
#####  Configuring DVServerUMD  #####
-----------------------------------------------------------
(1) DVServerUMD : It's a user mode driver which provides indirect display. It will request DVServerKMD via IOCTL's for QEMU display edid info. Depending upon resolution set in the indrect display, the windows graphics driver will generate frame buffers. The Swapchain in the UMD driver will receive the frame buffer address from GFX driver, which will be further sent to the KMD driver for scanning out the contents on the QEMU display.

(2) DVServerUMD_Node: This will create software node entry in device manager & DVServerUMD driver loads on to that software device

(3) DVEnabler.exe: This will cut off Microsoft Basic Display Adpater Path; So that Intel GPU can be stiched with Indirect display and will be used for workload processing on GPU

(4) Installation: 
	(4.1)1st time installation of DVServerUMD:
	[Pre-requisites] 
		- DVServer\dvserver.cat, DVServer\DVServer.dll, DVServer\DVServer.inf, DVEnabler.exe and DVServerUMD_Node.exe
		- Open Powershell in admin mode
	[CMD] : .\DVApp.ps1 setup

	(4.2) Run DVServerUMD after every reboot / shutdown:
	[Pre-requisites] 
		- DVEnabler.exe and DVServerUMD_Node.exe
		- Open Powershell in admin mode
	[CMD] : .\DVApp.ps1 run

	(4.3) Subsequent installation of DVServerUMD: 
		- If there are any changes in DVServerUMD driver, copy the updated binaries to DVServer folder
	[Pre-requisites] 
		- DVServer\dvserver.cat, DVServer\DVServer.dll, DVServer\DVServer.inf, DVEnabler.exe and DVServerUMD_Node.exe
		- Open Powershell in admin mode
	[CMD] : .\DVApp.ps1 setup
	
-----------------------------------------------------------	
#####  Setting / Executing DVServerUMD  #####
-----------------------------------------------------------
1) Open powershell in admin mode and goto ZeroCopy binary folder 
2) 1st time installation of DVServerUMD execute -->  ".\DVApp.ps1 setup"
3) It will popup with the driver installation window, click install.
4) After successful installation of UMD open Device Manager --> Display Adapter ? we should see "DVServerUMD Device"
5) Run your test cases and use cases
6) We need to run DVServerUMD script for every reboot / shutdown --> ".\DVApp.ps1 run"
7) If there are any changes in DVServerUMD driver, copy the updated binaries to DVServer folder  --> ".\DVApp.ps1 setup"

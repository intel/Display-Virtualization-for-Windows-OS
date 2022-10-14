#===========================================================================
#Readme.txt
#----------------------------------------------------------------------------
#Copyright (C) 2021 Intel Corporation
#SPDX-License-Identifier: MIT
#--------------------------------------------------------------------------*/

-----------------------------------------------------------
#####  Windows VM QEMU CMD Example #####
-----------------------------------------------------------
sudo qemu-system-x86_64 -m 4096 -enable-kvm -cpu host -smp cores=4,threads=2,sockets=1 -drive file=<WindowsOS.img>.img,format=qcow2,cache=none -device vfio-pci,host=0000:00:02.2 -device e1000,netdev=net0,mac=DE:AD:BE:EF:1C:00 -netdev tap,id=net0 -device virtio-vga,max_outputs=<no. of display, between 1-4>,blob=true -display gtk,gl=on,full-screen=<on/off>,monitor.1=0,monitor.0=1,show-fps=on -object memory-backend-memfd,id=mem1,hugetlb=on,size=4096M -machine memory-backend=mem1

Note:
full-screen=on,monitor.1=0,monitor.0=1
Secondary display to monitor 0 & Primary display to monitor 1

-----------------------------------------------------------
##### Pre-requisites #####
-----------------------------------------------------------
1) Use the above QEMU cmd and boot to Windows VM
2) Copy the Signed Zero Copy Binaries and Windows GFX driver to the VM

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
	3.3) DVEnabler.exe
4) GFX Installation and ZC Installation
	4.1) Open powershell in admin mode and goto ZeroCopy binary folder
	4.2) Run : ".\DVInstaller.ps1" - This will install both GFX driver and ZC driver
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

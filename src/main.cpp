/*
 * Copyright (C) 2016-2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <unistd.h>
#include <stdio.h>
#include <mocha/mocha.h>
#include <coreinit/filesystem.h>
#include <coreinit/filesystem_fsa.h>
#include <coreinit/launch.h>
#include <coreinit/screen.h>
#include <coreinit/foreground.h>
#include <coreinit/thread.h>
#include <coreinit/memory.h>
#include <sysapp/launch.h>
#include <whb/log.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <proc_ui/procui.h>
#include <iostream>
#include <vector>
#include <set>
#include <string>
static const char *sdCardVolPath = "/vol/external01";
static const char *tikPath = "/vol/system/rights/ticket/apps";
static const char *oddTikVolPath = "/vol/storage_odd_tickets";
#define EXIT_RELAUNCH_ON_LOAD       0xFFFFFFFD

void println(int line, const char *msg)
{
	int i;
	for(i = 0; i < 2; i++)
	{	//double-buffered font write
		OSScreenPutFontEx(SCREEN_TV,0,line,msg);
		OSScreenPutFontEx(SCREEN_DRC,0,line,msg);
		OSScreenFlipBuffersEx(SCREEN_TV);
		OSScreenFlipBuffersEx(SCREEN_DRC);
	}
}

int fsa_read(FSAClientHandle fsa_fd, int fd, void *buf, int len)
{
	int done = 0;
	uint8_t *buf_u8 = (uint8_t*)buf;
	while(done < len)
	{
		size_t read_size = len - done;
		int result = FSAReadFile(fsa_fd, buf_u8 + done, 0x01, read_size, fd, 0);
		if(result < 0)
			return result;
		else
			done += result;
	}
	return done;
}

int fsa_write(FSAClientHandle fsa_fd, int fd, const void *buf, int len)
{
	int done = 0;
	uint8_t *buf_u8 = (uint8_t*)buf;
	while(done < len)
	{
		size_t write_size = len - done;
		int result = FSAWriteFile(fsa_fd, buf_u8 + done, 0x01, write_size, fd, 0);
		if(result < 0)
			return result;
		else
			done += result;
	}
	return done;
}

struct DirName {
	char n[0x100];
};

int main(int argc, char **argv)
{
	WHBProcInit();
	
	if(Mocha_InitLibrary() != MOCHA_RESULT_SUCCESS)
{
	WHBLogPrint("Mocha_InitLibrary failed!");
	return 0;
	 
}	
	
	// Init screen
	OSScreenInit();
	int screen_buf0_size = OSScreenGetBufferSizeEx(SCREEN_TV);
	int screen_buf1_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
	uint8_t *screenBuffer = (uint8_t*)memalign(0x40, screen_buf0_size+screen_buf1_size);
	OSScreenSetBufferEx(SCREEN_TV, screenBuffer);
	OSScreenSetBufferEx(SCREEN_DRC, (screenBuffer + screen_buf0_size));
	OSScreenEnableEx(SCREEN_TV, 1);
	OSScreenEnableEx(SCREEN_DRC, 1);
	OSScreenClearBufferEx(SCREEN_TV, 0);
	OSScreenClearBufferEx(SCREEN_DRC, 0);
	
int action = 0;

while(WHBProcIsRunning())
	{		
		println(0,"tik2sd v1.2 by VannyBuns");
		println(2,"Press A to backup your console tickets.");
		println(3,"Press B to backup your current disc ticket.");
		println(5,"Press HOME to return to the Wii U Menu.");				
		VPADReadError error;
		VPADStatus status;
		//wait for user to decide option
		VPADRead(VPAD_CHAN_0, &status, 1, &error);
		if(error == 0)
		{
			if((status.hold | status.trigger) & VPAD_BUTTON_A)
				break;
				else if((status.hold | status.trigger) & VPAD_BUTTON_B)
				{
					action = 1;
					break;
				}	
			}					
        OSSleepTicks(OSMillisecondsToTicks(50));
    }
	
	
	FSAClientHandle fsaFd;
	int sdMounted = 0, oddMounted = 0;
	FSAFileHandle sdFd, tikFd;
	int line = 6;
	int ret;
	size_t i;
	std::vector<DirName> dirNames;
	std::set<std::string> tKeys;
	FSDirectoryEntry data;
	
	FSAInit();
	fsaFd = FSAAddClient(NULL);
	if (fsaFd < 0) 
	{
		println(line++, "Failed to add FSA client!");
		Mocha_DeInitLibrary();
		return -1;
	}
	
	ret = Mocha_UnlockFSClientEx(fsaFd);
	if (ret != MOCHA_RESULT_SUCCESS) 
	{
		println(line++, "Failed to unlock FSA Client!");
		FSADelClient(fsaFd);
		Mocha_DeInitLibrary();
		return 0;
	}
	
		ret = Mocha_MountFSEx("storage_slc", "/dev/slc01", tikPath, FSA_MOUNT_FLAG_LOCAL_MOUNT, (char*)0,0);
		if(ret < 0)
		{
			println(line++,"Failed to mount SLC!");
			 return 0;
		}	
	else
		sdMounted = 1;
	char sd2tikPath[256];
	sprintf(sd2tikPath,"%s/tik2sd",sdCardVolPath);
	FSAMakeDir(fsaFd, sd2tikPath, (FSMode)(FS_MODE_READ_OWNER | FS_MODE_WRITE_OWNER));
	if(action == 0)
	{
		FSADirectoryHandle handle;
		if(FSAOpenDir(fsaFd, tikPath, &handle) < 0)
		{
			println(line++,"Failed to open tik folder!");
			 return 0;
		}
		while(WHBProcIsRunning())
		{
			FSDirectoryEntry data;
			ret = FSAReadDir(fsaFd, handle, &data);
			if(ret != 0)
				break;
			if(data.info.flags & FS_STAT_DIRECTORY)
			{
				DirName cD;
				memcpy(cD.n, data.name, 0xFF);
				cD.n[0xFF] = '\0';
				dirNames.push_back(cD);
			}
		}
		FSACloseDir(fsaFd, handle);
		for(i = 0; i < dirNames.size(); i++)
		{
			char tikFolderPath[256];
			sprintf(tikFolderPath, "%s/%s", tikPath, dirNames[i].n);
			if(FSAOpenDir(fsaFd, tikFolderPath, &handle) < 0)
				continue;
			char sdTikFolderPath[256];
			sprintf(sdTikFolderPath, "%s/%s", sd2tikPath, dirNames[i].n);
			FSAMakeDir(fsaFd, sdTikFolderPath, (FSMode)(FS_MODE_READ_OWNER | FS_MODE_WRITE_OWNER));
			while(WHBProcIsRunning())
			{
				ret = FSAReadDir(fsaFd, handle, &data);
				if(ret != 0)
					break;
				if(!(data.info.flags & FS_STAT_DIRECTORY))
				{
					char tikRpath[256];
					sprintf(tikRpath, "%s/%s", tikFolderPath, data.name);
					if(FSAOpenFileEx(fsaFd, tikRpath, "rb", FS_MODE_READ_OWNER, FS_OPEN_FLAG_NONE, 1, &tikFd) >= 0)
					{
						FSStat stats;
						FSAGetStatFile(fsaFd, tikFd, &stats);
						size_t tikLen = stats.size;
						uint8_t *tikBuf = (uint8_t*)memalign(4, tikLen);
						fsa_read(fsaFd, tikFd, tikBuf, tikLen);
						FSACloseFile(fsaFd, tikFd);
						tikFd = -1;
						bool checkTik = true;
						int tikP = 0;
						while(checkTik == true)
						{
							checkTik = false;
							uint8_t *curTik = tikBuf+tikP;
							if((*(uint32_t*)curTik) != 0x00010004)
								break;
							char tName[256];
							sprintf(tName, "%s/%s", dirNames[i].n, data.name);
							char tEntry[256];
							sprintf(tEntry, "%08x%08x %08x%08x%08x%08x (%s@0x%x)\n", (*(uint32_t*)(curTik+0x1DC)), (*(uint32_t*)(curTik+0x1E0)),
								(*(uint32_t*)(curTik+0x1BF)), (*(uint32_t*)(curTik+0x1C3)), (*(uint32_t*)(curTik+0x1C7)), (*(uint32_t*)(curTik+0x1CB)),
								tName, tikP);
							tKeys.insert(std::string(tEntry));
							if((tikLen-tikP) > 0x354)
							{
								if((*(uint16_t*)(curTik+0x2B0)) == 0 && (*(uint32_t*)(curTik+0x2B8)) == 0x00010004)
								{
									tikP += 0x2B8;
									checkTik = true;
								}
								else if((*(uint16_t*)(curTik+0x2B0)) == 1 && (*(uint32_t*)(curTik+0x350)) == 0x00010004)
								{
									tikP += 0x350;
									checkTik = true;
								}
							}
						}
						char tikWpath[256];
						sprintf(tikWpath, "%s/%s", sdTikFolderPath, data.name);
						if(FSAOpenFileEx(fsaFd, tikWpath, "wb", FS_MODE_WRITE_OWNER, FS_OPEN_FLAG_NONE, 1, &sdFd) >= 0)
						{
							fsa_write(fsaFd, sdFd, tikBuf, tikLen);
							FSACloseFile(fsaFd, sdFd);
							sdFd = -1;
						}
						OSFreeToSystem(tikBuf);
					}
				}
			}
			FSACloseDir(fsaFd, handle);
		}
		char sdKeyPath[256];
		sprintf(sdKeyPath, "%s/keys.txt", sd2tikPath);
		if(FSAOpenFileEx(fsaFd, sdKeyPath, "wb", FS_MODE_WRITE_OWNER, FS_OPEN_FLAG_NONE, 1, &sdFd) >= 0)
		{
			char startMsg[64];
			sprintf(startMsg, "Found %i unique tickets\n", tKeys.size());
			fsa_write(fsaFd, sdFd, startMsg, strlen(startMsg));
			for(std::set<std::string>::iterator it = tKeys.begin(); it != tKeys.end(); ++it)
			{
				const char *k = it->c_str();
				fsa_write(fsaFd, sdFd, k, strlen(k));
			}
			FSACloseFile(fsaFd, sdFd);
			sdFd = -1;
		}
	}
	else
	{
		ret = Mocha_MountFSEx("storage_odd_content", "/dev/odd01", oddTikVolPath, FSA_MOUNT_FLAG_LOCAL_MOUNT, (char*)0,0);
		if(ret < 0)
		{
			println(line++,"Failed to mount ODD!");
			 return 0;
		}
		else
			oddMounted = 1;
		FSADirectoryHandle handle;
		if(FSAOpenDir(fsaFd, oddTikVolPath, &handle) < 0)
		{
			println(line++,"Failed to open tik folder!");
			 return 0;
		}
		char sdTikFolderPath[256];
		sprintf(sdTikFolderPath, "%s/odd", sd2tikPath);
		FSAMakeDir(fsaFd, sdTikFolderPath, (FSMode)(FS_MODE_READ_OWNER | FS_MODE_WRITE_OWNER));
		while(WHBProcIsRunning())
		{
			FSDirectoryEntry data;
			ret = FSAReadDir(fsaFd, handle, &data);
			if(ret != 0)
				break;
			if(data.info.flags & FS_STAT_DIRECTORY)
			{
				char tikRpath[256];
				sprintf(tikRpath,"%s/%s/title.tik", oddTikVolPath, data.name);
				if(FSAOpenFileEx(fsaFd, tikRpath, "rb", FS_MODE_READ_OWNER, FS_OPEN_FLAG_NONE, 1, &tikFd) >= 0)
				{
					FSStat stats;
					FSAGetStatFile(fsaFd, tikFd, &stats);
					size_t tikLen = stats.size;
					uint8_t *tikBuf = (uint8_t*)memalign(4, tikLen);
					fsa_read(fsaFd, tikFd, tikBuf, tikLen);
					FSACloseFile(fsaFd, tikFd);
					tikFd = -1;
					if((*(uint32_t*)(tikBuf+0x1DC)) == 0x00050000)
					{
						char tikWpath[256];
						sprintf(tikWpath, "%s/%08x%08x.tik", sdTikFolderPath, (*(uint32_t*)(tikBuf+0x1DC)), (*(uint32_t*)(tikBuf+0x1E0)));
						if(FSAOpenFileEx(fsaFd, tikWpath, "wb", FS_MODE_WRITE_OWNER, FS_OPEN_FLAG_NONE, 1, &sdFd) >= 0)
						{
							fsa_write(fsaFd, sdFd, tikBuf, tikLen);
							FSACloseFile(fsaFd, sdFd);
							sdFd = -1;
						}
					}
					OSFreeToSystem(tikBuf);
				}
			}
		}
		FSACloseDir(fsaFd, handle);
	}
	println(line++,"Tickets backed up!");
	
	if(fsaFd >= 0)
		{
			if(sdFd >= 0)
				FSACloseFile(fsaFd, sdFd);
			if(tikFd >= 0)
				FSACloseFile(fsaFd, tikFd);
			if(sdMounted)
				FSAUnmount(fsaFd, sdCardVolPath, FSA_UNMOUNT_FLAG_FORCE);
			if(oddMounted)
				FSAUnmount(fsaFd, oddTikVolPath, FSA_UNMOUNT_FLAG_FORCE);
			FSADelClient(fsaFd);
		}
			Mocha_UnmountFS("storage_slc");
			OSForceFullRelaunch();
			SYSLaunchMenu();			
			OSFreeToSystem(screenBuffer);
			Mocha_DeInitLibrary();
			WHBProcShutdown();
			return EXIT_RELAUNCH_ON_LOAD;
}

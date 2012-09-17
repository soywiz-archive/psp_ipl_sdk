/*
	MS FAT file boot loader

	PANDORA'S IPL edition
*/

#include <psptypes.h>
#include "kprintf.h"
#include "cache.h"
#include "syscon.h"

#include "memstk.h"
#include "tff.h"		/* Tiny-FatFs declarations */

#include "patch.h"
#include "patch.h"

#define SUPPORT_PANDORAS_MS_IPL 1

/****************************************************************************
	IPL load BOOT
****************************************************************************/
static int check_ipl_address(u32 address)
{
	if(address >= 0x04000000 && address < 0x04200000) return 1;
	return 0;
}

static int check_ipl_address2(u32 address,u32 offset)
{
	return check_ipl_address(address+offset);
}

/****************************************************************************
****************************************************************************/
int ms_poll_access(int cnt)
{
	// flip LED
	if(cnt&1)
		REG32(0xbe24000c)  = 0x40;
	else
		REG32(0xbe240008)  = 0x40;
	if( (cnt%32)==0)
	{
		// clear WDT
		pspSysconNop();
		return 1;
	}
	return 0;
}

/****************************************************************************
	IPL load BOOT
****************************************************************************/
static FATFS  FileSystem;
static FIL FileObject;

extern int _ms_init(void);

int ms_load_file(const char *path,void *top_addr)
{
	FRESULT result;
	WORD readed;
	BYTE *load_addr;
	int ttl_read;
	int block_cnt = 0;

	load_addr = (BYTE *)top_addr;

	result = f_mount(0,&FileSystem);
	if(result!=0)
	{
//Kprintf("f_mount error %08X\n",result);
		return -1;
	}

	result = f_open(&FileObject,path,FA_READ|FA_OPEN_EXISTING);
	if(result!=0)
	{
//Kprintf("f_open %s error\n",path);
		return -1;
	}

	ttl_read = 0;
	do{
		result = f_read(&FileObject,load_addr,0x8000,&readed);
		if(result!=0)
		{
//Kprintf("f_read error\n");
			return -1;
		}
//Kprintf("f_read addr=%08X size=%08X\n",(int)load_addr,readed);
		load_addr += readed;
		ttl_read  += readed;

		ms_poll_access(block_cnt++);

	}while(readed!=0);
//Kprintf("readed %d bytes\n",ttl_read);

	f_close(&FileObject);
	REG32(0xbe24000c)  = 0x40;

	return ttl_read;
}

/****************************************************************************
	save file
****************************************************************************/
int ms_save_file(const char *path,const void *data,int size)
{
	FRESULT result;
	WORD writed;
	int num_write;
	int block_cnt = 0;

	//
	result = f_mount(0,&FileSystem);
	if(result!=0)
	{
Kprintf("f_mount error %08X\n",result);
		return -1;
	}

	result = f_open(&FileObject,path,FA_WRITE|FA_CREATE_ALWAYS);
	if(result!=0)
	{
Kprintf("f_open %s error\n",path);
		return -1;
	}

	while(size)
	{
		num_write = size>0x8000 ? 0x8000 : size;
		result = f_write(&FileObject,data,num_write,&writed);
		if(result!=0 || num_write!=writed)
		{
Kprintf("f_write error\n");
			return -1;
		}
//Kprintf("f_write %08X:%04X\n",(int)data,writed);
		data += num_write;
		size -= num_write;

		if( ms_poll_access(block_cnt++) )
		{
Kprintf("Left %08X\n",size);
		}
	}
//Kprintf("readed %d bytes\n",ttl_read);
	f_close(&FileObject);
	REG32(0xbe24000c)  = 0x40;

	return 0;
}

/****************************************************************************
	PSP_IPL loader

1.encrypted IPL BLOCK DATA (1000h BLOCK)
2.decrypted IPL BLOCK DATA (1000h BLOCK)

****************************************************************************/
static DWORD *ipl_buf = (DWORD *)0xbfd00000;

void *ms_load_ipl(const char *path)
{
	FRESULT result;
	WORD readed;
	DWORD *src;

	DWORD *top;
	DWORD size;
	void *entry;
	DWORD sum;
	int binary_type = 0;

	result = f_mount(0,&FileSystem);
	if(result!=0)
	{
Kprintf("f_mount error %08X\n",result);
		return 0;
	}

	result = f_open(&FileObject,path,FA_READ|FA_OPEN_EXISTING);
	if(result!=0)
	{
Kprintf("f_open %s error\n",path);
		return 0;
	}

#if SUPPORT_PANDORAS_MS_IPL
/*
see ms_ipl.bin of Pandora's Battery Recovery Menu

0x0000-0x0fff : IPL EXPLOIT 1st boot loader
0x1000-0x3fff : PATCH.BIN(0x040e0000-) , boot.bin patch loader , called after setup main.bin
0x4000-end    : each 0x1000 decrypted block of 1.50 IPL data
*/
Kprintf("bypass IPL exploit block\n");
result = f_read(&FileObject,ipl_buf,0x1000,&readed);

Kprintf("loading patcher.bin\n");
result = f_read(&FileObject,(void *)(0x040e0000),0x3000,&readed);
#endif

	do{
		result = f_read(&FileObject,ipl_buf,0x1000,&readed);
		if(result!=0)
		{
Kprintf("f_read error\n");
			entry = 0;
			goto error;
		}

		if(binary_type==0)
		{
			// chech decrypted HEADER
			top  = (DWORD *)(ipl_buf[0]);
			size = (ipl_buf[1]);
			entry= (void *)(ipl_buf[2]);
			sum  = (ipl_buf[3]);
			if(
				check_ipl_address(top) &&
				check_ipl_address(top+size) && 
				(entry==0 || ( entry >= top && entry<=(top+size))) &&
				sum == 0
			){
				// decrypted with header
Kprintf("Decrypted IPL\n");
				binary_type = 2;
			} else if(
				ipl_buf[0x60/4]==0x01 &&
				ipl_buf[0x64/4]==0    &&
				ipl_buf[0x68/4]==0)
			{
				// encrypted IPL
Kprintf("Encrypted IPL\n");
				binary_type = 1;
			}
		}

		// decrypt block
		if(binary_type==1)
		{
			if( pspKirkProc(ipl_buf,0x1000,ipl_buf,0x1000,0x01) < 0)
			{
Kprintf("Decrypt error\n");
				entry = 0;
				goto error;
			}
		}

		// load BLOCK
		top  = (ipl_buf[0]);
		size = (ipl_buf[1]);
		entry= (ipl_buf[2]);
		sum  = (ipl_buf[3]);
		src  = &(ipl_buf[4]);
Kprintf("TOP %08X SIZE %08X ENTRY %08X SUM %08X\n",top,size,entry,sum);

		while(size)
		{
			*top++ = *src++;
			size -= 4;
		}

	}while(entry==0);
//Kprintf("readed %d bytes\n",ttl_read);
error:
	f_close(&FileObject);

	return entry;
}

/****************************************************************************
	initialize FAT system
****************************************************************************/
int ms_fat_init(void)
{
	pspMsInit();
}

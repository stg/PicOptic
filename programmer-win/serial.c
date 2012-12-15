// Source file for platform independant serial communications
//
// Currently supports windows and posix(unix, mac)
//
// senseitg@gmail.com 2012-May-22

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "serial.h"

#include <stdio.h>
#include <windows.h>
#include <winnt.h>
#include <setupapi.h>

static HANDLE h_serial;
static COMMTIMEOUTS restore;

// GUID for serial ports class
static const GUID GUID_SERENUM_BUS_ENUMERATOR={0x86E0D1E0L,0x8089,0x11D0,{0x9C,0xE4,0x08,0x00,0x3E,0x30,0x1F,0x73}};

// enumerate serial ports
void senum(void (*fp_enum)(char *name, char *device)) {
  // STG's 50-cents:
  // for the love of all things sacred... enumerate serial ports already!
  // not only does this require an utterly convoluted piece of code but it doesn't even work on all windows versions!
  // to support windows across the board, you'd have to implement at least three different enumeration routines...
  // god damn it... two fucking hours of my life wasted on this crap!
  // microsoft, i usually like you, but for this you deserve to be bitchslapped into outer space.
  HDEVINFO h_devinfo;
  SP_DEVICE_INTERFACE_DATA ifdata;
  SP_DEVICE_INTERFACE_DETAIL_DATA *ifdetail;
  SP_DEVINFO_DATA ifinfo;
  int count=0;
  DWORD size=0;
  char friendlyname[MAX_PATH+1];
  char name[MAX_PATH+1];
  char *namecat;
  DWORD type;
  HKEY devkey;
  DWORD keysize;
  
  // initialize some stuff
  memset(&ifdata,0,sizeof(ifdata));
  ifdata.cbSize=sizeof(SP_DEVICE_INTERFACE_DATA);
  memset(&ifinfo,0,sizeof(ifinfo));
  ifinfo.cbSize=sizeof(ifinfo);
  // set up a device information set
  h_devinfo=SetupDiGetClassDevs(&GUID_SERENUM_BUS_ENUMERATOR,NULL,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  if(h_devinfo) {
    while(1) {
      // enumerate devices
      if(!SetupDiEnumDeviceInterfaces(h_devinfo,NULL,&GUID_SERENUM_BUS_ENUMERATOR,count,&ifdata)) break;
      size=0;
      // fetch size required for "interface details" struct
      SetupDiGetDeviceInterfaceDetail(h_devinfo,&ifdata,NULL,0,&size,NULL);
      if(size) {
        // allocate and initialize "interface details" struct
        ifdetail=malloc(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA)+size);
        memset(ifdetail,0,sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA)+size);
        ifdetail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        // get "device interface details" and "device info"
        if(SetupDiGetDeviceInterfaceDetail(h_devinfo,&ifdata,ifdetail,size,&size,&ifinfo)) {
          // retrieve "friendly name" from the registry
          if(!SetupDiGetDeviceRegistryProperty(h_devinfo,&ifinfo,SPDRP_FRIENDLYNAME,&type,friendlyname,sizeof(friendlyname),&size)) {
						friendlyname[0]=0;
          }
          // open the registry key containing device information
          devkey=SetupDiOpenDevRegKey(h_devinfo,&ifinfo,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_READ);
          if(devkey!=INVALID_HANDLE_VALUE) {
            keysize=sizeof(name);
            // retrieve the actual *short* port name from the registry
            if(RegQueryValueEx(devkey,"PortName",NULL,NULL,name,&keysize)==ERROR_SUCCESS) {
              // whoop-dee-fucking-doo, we finally have what we need!
              if(strlen(friendlyname)) {
              	// build a display name (because "friendly name" may not contain port name early enough)
              	namecat=malloc(strlen(name)+strlen(friendlyname)+2);
              	strcpy(namecat,name);
              	strcat(namecat," ");
              	strcat(namecat,friendlyname);
              	fp_enum(namecat,name);
              	free(namecat);
            	} else {
            		// no "friendly name" available, just use device name
              	fp_enum(name,name);
            	}
            }
            // close the registry key
            RegCloseKey(devkey);
          }
        }
        free(ifdetail);
      }
      count++;
    }
    // DESTROY! DESTROY!
    SetupDiDestroyDeviceInfoList(h_devinfo);
  }
}

// open serial port
// device has form "COMn"
bool sopen(char* device) {
  h_serial=CreateFile(device,GENERIC_READ|GENERIC_WRITE,0,0,OPEN_EXISTING,0,0);
  return h_serial!=INVALID_HANDLE_VALUE;
}

// configure serial port
bool sconfig(char* fmt) {
  DCB dcb;
  COMMTIMEOUTS cmt;
  // clear dcb  
  memset(&dcb,0,sizeof(DCB));
  dcb.DCBlength=sizeof(DCB);
  // configure serial parameters
  if(!BuildCommDCB(fmt,&dcb)) return false;
  dcb.fOutxCtsFlow=0;
  dcb.fOutxDsrFlow=0;
  dcb.fDtrControl=0;
  dcb.fOutX=0;
  dcb.fInX=0;
  dcb.fRtsControl=0;
  if(!SetCommState(h_serial,&dcb)) return false;
  // configure buffers
  if(!SetupComm(h_serial,1024,1024)) return false;
  // configure timeouts 
  GetCommTimeouts(h_serial,&cmt);
  memcpy(&restore,&cmt,sizeof(cmt));
  cmt.ReadIntervalTimeout=100;
  cmt.ReadTotalTimeoutMultiplier=100;
  cmt.ReadTotalTimeoutConstant=100;
  cmt.WriteTotalTimeoutConstant=100;
  cmt.WriteTotalTimeoutMultiplier=100;
  if(!SetCommTimeouts(h_serial,&cmt)) return false;
  return true;
}

// get number of bytes available
int32_t speek(void) {
	DWORD errors;
	COMSTAT status;
	ClearCommError(h_serial,&errors,&status);
	return status.cbInQue;
}

// read from serial port
int32_t sread(void *p_read,uint16_t i_read) {
  DWORD i_actual=0;
  if(!ReadFile(h_serial,p_read,i_read,&i_actual,NULL)) return -1;
  return (int32_t)i_actual;
}

// write to serial port
int32_t swrite(void* p_write,uint16_t i_write) {
  DWORD i_actual=0;
  if(!WriteFile(h_serial,p_write,i_write,&i_actual,NULL)) return -1;
  return (int32_t)i_actual;
}

// close serial port
bool sclose() {
  // politeness: restore (some) original configuration
  SetCommTimeouts(h_serial,&restore);
  return CloseHandle(h_serial)!=0;
}

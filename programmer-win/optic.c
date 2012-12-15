#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <windows.h>
#include "serial.h"

typedef enum {
	IGNORE_PROTECTED = 1,
	IGNORE_OUTOFRANGE = 2,
	IGNORE_BATTERY = 4,
	DISPLAY_MAP = 8
} flags_e;

void help_out(bool full) {
	printf("Useage: optic firmware.hex -o COMn (-i)\n");
	if(full) {
		printf("firmware.hex   firmware file to download to target\n");
		printf("-o COMn        communications port to use for download\n");
		printf("-p             ignore data at protected addresses\n");
		printf("-r             ignore data at out-of-range addresses\n");
		printf("-b             ignore battery level\n");
		printf("-m             display rom map\n");
	}
}

uint8_t hexdec(char hexnuf) {
	if(hexnuf>='0'&&hexnuf<='9') return hexnuf-'0';
	if(hexnuf>='A'&&hexnuf<='F') return hexnuf-'A'+0x0A;
	if(hexnuf>='a'&&hexnuf<='f') return hexnuf-'a'+0x0A;
	return 16;
}

bool crunch(char *record) {
	char *p_in=record;
	uint8_t dec;
	uint8_t *p_out=(uint8_t*)record;
	while(*p_in!=0) {
		dec=hexdec(*p_in++);
		if(dec>0xF) return false;
		if(*p_in==0) return false;
		*p_out=dec<<4;
		dec=hexdec(*p_in++);
		if(dec>0xF) return false;
		*p_out++|=dec;
	}
	return true;
}

#define ACK 0x06
#define NAK 0x10

bool command(char cmd,uint8_t *in,size_t insz,uint8_t *out,size_t outsz) {
	uint8_t retry;
	size_t avail;
	uint8_t dummy;
	uint16_t time;
	// Flush
	avail=speek();
	while(avail--) sread(&dummy,1);
	
	for(retry=0;retry<3;retry++) {
		swrite(&cmd,1);
		for(time=0;time<500;time++) {
			Sleep(1);
			avail=speek();
			if(avail) break;
		}
		if(avail) break;		
	}
	
	if(!avail) {
		printf("Device does not respond to command\n");
		return false;
	}

	sread(&dummy,1);
	
	if(dummy!=insz+1) {
		printf("Device protocol mismatch - command size\n");
		return false;
	}
	
	swrite(in,insz);

	for(time=0;time<1000;time++) {
		Sleep(1);
		avail=speek();
		if(avail>=outsz+1) break;
	}
	if(avail) {
		avail--;
		sread(&dummy,1);
		if(dummy==NAK) {
			printf("Device was unable to execute command\n");
			return false;
		} else if(dummy!=ACK) {
			printf("Device protocol mismatch - response format\n");
			return false;
		}
	} else {
		printf("Response was not received in a timely fashion\n");
		return false;
	}
	
	if(avail!=outsz) {
		printf("Device protocol mismatch - response size\n");
		return false;
	}
	
	if(outsz) sread(out,outsz);
	
	return true;
	
}

int main(int argc,char**argv) {
	char *firmware=NULL;
	char *device=NULL;
	int n,z;
	int retry;
	uint8_t flags=0;
	printf("PicOptic download utility v1.0\n");
	if(argc>1) firmware=argv[1];
	for(n=2;n<argc;n++) {
		if(strcmp("-?",argv[n])==0) {
			help_out(true);
		} else if(strcmp("-p",argv[n])==0) {
			flags|=IGNORE_PROTECTED;
		} else if(strcmp("-r",argv[n])==0) {
			flags|=IGNORE_OUTOFRANGE;
		} else if(strcmp("-b",argv[n])==0) {
			flags|=IGNORE_BATTERY;
		} else if(strcmp("-m",argv[n])==0) {
			flags|=DISPLAY_MAP;
		} else if(strcmp("-o",argv[n])==0) {
			if(n+1<argc) device=argv[++n];
		} else if(argv[n][0]=='-'&&argv[n][1]=='o') {
			if(strlen(&argv[n][2])) device=&argv[n][2];
		} else {
			printf("Unknown option %s\n",argv[n]);
			help_out(false);
			exit(1);
		}
	}
	if(!firmware) {
		printf("No firmware specified\n");
		help_out(false);
		exit(1);
	}
	if(!device) {
		printf("No device specified\n");
		help_out(false);
		exit(1);
	}
	
	// Convert hex file to memory model
	uint16_t pgmem[0x1000];
	for(n=0;n<0x1000;n++) {
		pgmem[n]=0x3FFF;
	}
	FILE *f=fopen(firmware,"r");
	if(!f) {
		printf("Failed to open firmware file %s\n",firmware);
		exit(1);
	}
	char record[256];
	uint32_t lnum=0;
	bool read_ok=true;
	size_t llen;
	uint32_t addr=0x00000000;
	while(read_ok&&!feof(f)) {
		lnum++;
		fgets(record,sizeof(record),f);
		if(record[strlen(record)-1]==0x0A) record[strlen(record)-1]=0;
		llen=strlen(record);
		if(llen) {
			if(record[0]!=':') {
				printf("%s:%i record does not start with ':'\n",firmware,lnum);
				read_ok=false;
			} else if((llen&1)==0) {
				printf("%s:%i record is of incorrect length\n",firmware,lnum);
				read_ok=false;
			} else if(!crunch(&record[1])) {
				printf("%s:%i record contains bad characters\n",firmware,lnum);
				read_ok=false;
			} else {
				llen>>=1;
				if(llen<5) {
					printf("%s:%i record length below minimum\n",firmware,lnum);
					read_ok=false;
				} else if(llen!=record[1]+5) {
					printf("%s:%i record length/byte count field mismatch\n",firmware,lnum);
					read_ok=false;
				} else {
					uint8_t count=record[1];
					addr&=0xFFFF0000;
					addr|=(((uint8_t)record[2])<<8)|((uint8_t)record[3]);
					uint8_t type=record[4];
					uint8_t *p_data=&record[5];
					uint8_t csum=0;
					for(n=1;n<5+count;n++) csum+=record[n];
					csum=(~csum)+1;
					if(csum!=(uint8_t)record[5+count]) {
						printf("%s:%i checksum mismatch\n",firmware,lnum);
						read_ok=false;
					} else {
						if(type==0x00) {
							while(count--) {
								if(addr>=0x2000) {
									if(!(flags&IGNORE_OUTOFRANGE)) {
										printf("%s:%i address out of range\n",firmware,lnum);
										read_ok=false;
										break;
									}
								} else {
									if(addr&1) {
										pgmem[addr>>1] = (pgmem[addr>>1] & 0x00FF) | (*p_data++) << 8;
									} else {
										pgmem[addr>>1] = (pgmem[addr>>1] & 0xFF00) | *p_data++;
									}
									addr++;
								}
							}
						} else if(type==0x01) {
							if(count) {
								printf("%s:%i bad byte count for \"end of file\" record\n",firmware,lnum);
								read_ok=false;
							}
							break;
						} else if(type==0x04) {
							if((addr&0x0000FFFF)!=0x00000000) {
								printf("%s:%i bad address for \"extended linear address\" record\n",firmware,lnum);
								read_ok=false;
							} else if(count!=2) {
								printf("%s:%i incorrect byte count for \"extended linear address\" record\n",firmware,lnum);
								read_ok=false;
							} else {
								addr=(((uint8_t)record[5])<<24)|(((uint8_t)record[6])<<16);
							}
						} else {
							printf("%s:%i this record type is not supported\n",firmware,lnum);
							read_ok=false;
						}
					}
				}
			}
		}
	}
	fclose(f);
	if(!read_ok) exit(1);
	printf("Firmware file loaded\n");
	if(flags&DISPLAY_MAP) {
		char map[0x40+1];
		map[0x40]=0;
		for(n=0;n<0x1000;n+=0x40) {
			memset(map,'-',0x40);
			for(z=0;z<0x40;z++) {
				if(pgmem[n+z]!=0x3FFF) map[z]='X';
			}
			if(strchr(map,'X')) printf("%08X %s\n", n, map);
		}
	}
	if(!sopen(device)) {
		printf("Unable to open serial port %s\n",device);
		exit(1);
	}
	if(!sconfig("9600,N,8,1")) {
		printf("Unable to configure serial port %s\n",device);
		sclose();
		exit(1);
	}
	printf("Serial port is open\n");
	
	Sleep(1000);
	
	uint8_t pwrite[66],pread[1],pbuzz[2];
	uint8_t resp[65];
	
	// Verify battery voltage
	command('B',NULL,0,resp,2);
	uint16_t battery=(resp[0]<<8)|resp[1];
	float voltage=1.024/((float)battery/65535.0);
	printf("Battery voltage is %2.3f\n",voltage);
	if(voltage<2.0&&!(flags&IGNORE_BATTERY)) {
		printf("Voltage is too low\n");
		sclose();
		exit(1);
	}

	printf("Downloading firmware...\n");
	for(n=0;n<0x1000;n+=0x20) {
		for(z=0;z<0x20;z++) {
			if(pgmem[n+z]!=0x3FFF) break;
		}
		if(z!=0x20) {
			pwrite[0]=n>>5;
			if(pwrite[0]<0x10) {
				if(!(flags&IGNORE_PROTECTED)) {
					printf("Attempted to write protected area\n");
					sclose();
					exit(1);
				}
			} else {
				uint8_t csum=pwrite[0];
				for(z=0;z<0x20;z++) {
					pwrite[(z<<1)+1]=pgmem[n+z]>>8;
					csum+=pgmem[n+z]>>8;
					pwrite[(z<<1)+2]=pgmem[n+z]&0xFF;
					csum+=pgmem[n+z]&0xFF;
				}
				pwrite[65]=csum;
				for(retry=0;retry<3;retry++) {
					if(retry) printf("Trying again...\n");
					if(command('W',pwrite,66,NULL,0)) {
						pread[0]=pwrite[0];
						if(command('R',pread,1,resp,65)) {
							if(memcmp(resp,&pwrite[1],65)==0) break;
							printf("Verify failed\n");
						}
					}
				}
				if(retry==3) {
					
					printf("Download failed\n");
					sclose();
					exit(1);
				}
			}
		}
	}
	printf("Download successful!\n");
	pbuzz[0]=50;
	pbuzz[1]=2;
	uint8_t dummy;
	command('S',pbuzz,2,&dummy,1);
	pbuzz[0]=48;
	pbuzz[1]=4;
	Sleep(100);
	command('S',pbuzz,2,&dummy,1);
	swrite("X",1);

	sclose();
	
	exit(0);
}
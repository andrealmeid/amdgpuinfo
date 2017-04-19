AMDAPPSDK_PATH=/opt/AMDAPP
AMDAPPSDK_ARCH=x86
CC=gcc
CFLAGS=-O3
LDFLAGS=-lpci -lOpenCL

all: amdmeminfo

amdmeminfo: amdmeminfo.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -I$(AMDAPPSDK_PATH)/include -L$(AMDAPPSDK_PATH)/lib/$(AMDAPPSDK_ARCH)

amdmeminfo-ethos: amdmeminfo.c
	$(CC) $(CFLAGS) -o amdmeminfo $^ $(LDFLAGS) -L/opt/driver-switching/fglrx/runtime-lib

clean:
	rm -f amdmeminfo *.o

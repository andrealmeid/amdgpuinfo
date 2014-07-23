AMDAPPSDK_PATH=/opt/AMDAPP
AMDAPPSDK_ARCH=x86
CC=gcc
CFLAGS=-O3
LDFLAGS=-lpci -lOpenCL

amdmeminfo: amdmeminfo.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -I$(AMDAPPSDK_PATH)/include -L$(AMDAPPSDK_PATH)/lib/$(AMDAPPSDK_ARCH)

clean:
	rm -f amdmeminfo *.o

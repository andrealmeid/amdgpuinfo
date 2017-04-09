/*
 * AMDMemInfo, (c) 2014 by Zuikkis <zuikkis@gmail.com>
 * Updated by Yann St.Arnaud <ystarnaud@gmail.com>
 *
 * Loosely based on "amdmeminfo" by Joerie de Gram.
 *
 * AMDMemInfo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * AMDMemInfo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with AMDMemInfo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pci/pci.h>
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif

#define VERSION "AMDMemInfo by Zuikkis <zuikkis@gmail.com>\n" \
      "Updated by Yann St.Arnaud <ystarnaud@gmail.com>"

#define LOG_INFO 1
#define LOG_ERROR 2

/***********************************
 * Program Options
 ***********************************/
bool opt_bios_only = false; // --biosonly / -b
bool opt_opencl_order = false; // --opencl / -o
bool opt_output_short = false; // --short / -s
bool opt_quiet = false;  // --quiet / -q to turn off
bool opt_use_stderr = false;  // --use-stderr
bool opt_show_memconfig = false; // --memconfig / -c

// output function that only displays if verbose is on
static void print(int priority, const char *fmt, ...)
{
  if (opt_quiet && !(priority == LOG_ERROR && opt_use_stderr)) {
    return;
  }

  va_list args;

  va_start(args, fmt);
  if (priority == LOG_ERROR && opt_use_stderr) {
    vfprintf(stderr, fmt, args);
  } else {
    vprintf(fmt, args);
  }
  va_end(args);
}

// show help
static void showhelp(char *program)
{
  printf("%s\n\n"
    "Usage: %s [options]\n\n"
    "Options:\n"
    "-b, --biosonly  Only output BIOS Versions (implies -s with <OpenCLID>:<BIOSVersion> output)\n"
    "-c, --memconfig Output the memory configuration\n"
    "-h, --help      Help\n"
    "-o, --opencl    Order by OpenCL ID (cgminer/sgminer GPU order)\n"
    "-q, --quiet     Only output results\n"
    "-s, --short     Short form output - 1 GPU/line - <OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<BIOSVersion>:<Memory Type>\n"
    "--use-stderr    Output errors to stderr\n"
    "\n", VERSION, program);
}


// parse command line options
static bool load_options(int argc, char *argv[])
{
  int i;

  for (i = 1; i < argc; ++i)
  {
    if (!strcasecmp("--help", argv[i]) || !strcasecmp("-h", argv[i])) {
      showhelp(argv[0]);
      return false;
    } else if (!strcasecmp("--opencl", argv[i]) || !strcasecmp("-o", argv[i])) {
      opt_opencl_order = true;
    } else if (!strcasecmp("--biosonly", argv[i]) || !strcasecmp("-b", argv[i])) {
      opt_bios_only = true;
      opt_output_short = true;
    } else if (!strcasecmp("--short", argv[i]) || !strcasecmp("-s", argv[i])) {
      opt_output_short = true;
    } else if (!strcasecmp("--quiet", argv[i]) || !strcasecmp("-q", argv[i])) {
      opt_quiet = true;
    } else if (!strcasecmp("--memconfig", argv[i]) || !strcasecmp("-c", argv[i])) {
      opt_show_memconfig = true;
    } else if (!strcasecmp("--use-stderr", argv[i])) {
      opt_use_stderr = true;
    }
  }

  return true;
}

/***********************************************
 * GPU Types
 ***************************************************/
typedef struct {
  unsigned int vendor_id;
  unsigned int device_id;
  unsigned long subsys_id;
  unsigned char rev_id;
  const char *name;
} gputype_t;

static gputype_t gputypes[] = {
    /* Fury/Nano */
    { 0x1002, 0x7300, 0, 0, "Radeon R9 Fury/Nano/X"},
    { 0x1002, 0x7300, 0, 0xc8, "Radeon R9 Fury/Nano/X"},
    { 0x1002, 0x7300, 0, 0xc9, "Radeon R9 Fury/Nano/X"},
    { 0x1002, 0x7300, 0, 0xca, "Radeon R9 Fury/Nano/X"},
    { 0x1002, 0x7300, 0, 0xcb, "Radeon R9 Fury/Nano/X"},
    /* RX 4xx */
    { 0x1002, 0x67df, 0, 0, "Radeon RX 470/480"},
    { 0x1002, 0x67df, 0, 0xc7, "Radeon RX 480"},
    { 0x1002, 0x67df, 0, 0xcf, "Radeon RX 470"},
    { 0x1002, 0x67ef, 0, 0, "Radeon RX 460"},
    { 0x1002, 0x67ef, 0, 0xc0, "Radeon RX 460"},
    { 0x1002, 0x67ef, 0, 0xc1, "Radeon RX 460"},
    { 0x1002, 0x67ef, 0, 0xc5, "Radeon RX 460"},
    { 0x1002, 0x67ef, 0, 0xcf, "Radeon RX 460"},
    /* R9 3xx */
    { 0x1002, 0x67b1, 0, 0x80, "Radeon R9 390" },
    { 0x1002, 0x67b0, 0, 0x80, "Radeon R9 390x" },
    { 0x1002, 0x6939, 0, 0xf1, "Radeon R9 380" },
    { 0x1002, 0x6938, 0, 0, "Radeon R9 380x" },
    { 0x1002, 0x6810, 0, 0x81, "Radeon R7 370" },
    { 0x1002, 0x665f, 0, 0x81, "Radeon R7 360" },
    /* R9 2xx */
    { 0x1002, 0x67B9, 0, 0, "Radeon R9 295x2" },
    { 0x1002, 0x67b1, 0, 0, "Radeon R9 290/R9 390" },
    { 0x1002, 0x67b0, 0, 0, "Radeon R9 290x/R9 390x" },
    { 0x1002, 0x6939, 0, 0, "Radeon R9 285/R9 380" },
    { 0x1002, 0x6811, 0, 0, "Radeon R9 270" },
    { 0x1002, 0x6810, 0, 0, "Radeon R9 270x/R7 370" },
    { 0x1002, 0x6658, 0, 0, "Radeon R7 260x" },
    /* HD 7xxx */
    { 0x1002, 0x679b, 0, 0, "Radeon HD7990" },
    { 0x1002, 0x6798, 0, 0, "Radeon HD7970/R9 280x" },
    { 0x1002, 0x679a, 0, 0, "Radeon HD7950/R9 280" },
    { 0x1002, 0x679E, 0, 0, "Radeon HD7870XT" },
    { 0x1002, 0x6818, 0, 0, "Radeon HD7870" },
    { 0x1002, 0x6819, 0, 0, "Radeon HD7850" },
    { 0x1002, 0x665C, 0, 0, "Radeon HD7790" },
    /* HD 6xxx */
    { 0x1002, 0x671D, 0, 0, "Radeon HD6990" },
    { 0x1002, 0x6718, 0, 0, "Radeon HD6970" },
    { 0x1002, 0x6719, 0, 0, "Radeon HD6950" },
    { 0x1002, 0x671F, 0, 0, "Radeon HD6930" },
    { 0x1002, 0x6738, 0, 0, "Radeon HD6870" },
    { 0x1002, 0x6739, 0, 0, "Radeon HD6850" },
    { 0x1002, 0x6778, 0, 0, "Radeon HD6450/HD7470" },
    { 0x1002, 0x6779, 0, 0, "Radeon HD6450" },
    /* HD 5xxx */
    { 0x1002, 0x689C, 0, 0, "Radeon HD5970" },
    { 0x1002, 0x6898, 0, 0, "Radeon HD5870" },
    { 0x1002, 0x6899, 0, 0, "Radeon HD5850" },
    { 0x1002, 0x689E, 0, 0, "Radeon HD5830" },
    { 0, 0, 0, 0, "Unknown"}
};

// find GPU type by vendor id/device id
static gputype_t *_find_gpu(unsigned int vendor_id, unsigned int device_id, unsigned long subsys_id, unsigned char rev_id)
{
  gputype_t *g = gputypes;

  while (g->device_id)
  {
    if (g->vendor_id == vendor_id && g->device_id == device_id && g->subsys_id == subsys_id && g->rev_id == rev_id) {
      return g;
    }

    ++g;
  }

  return NULL;
}

// find GPU type by vendor id/device id
static gputype_t *find_gpu(unsigned int vendor_id, unsigned int device_id, unsigned long subsys_id, unsigned char rev_id)
{
  gputype_t *g = _find_gpu(vendor_id, device_id, subsys_id, rev_id);
      
  //if specific subsys id not found, try again with 0
  if (g == NULL && subsys_id > 0) {
    g = _find_gpu(vendor_id, device_id, 0, rev_id);
  }
      
  //if specific rev id not found, try again with 0 for general device type
  if (g == NULL && rev_id > 0) {
    g = _find_gpu(vendor_id, device_id, subsys_id, 0);
  }

  //if still not found, try no rev id or subsys id
  if (g == NULL) {
    g = _find_gpu(vendor_id, device_id, 0, 0);
  }
      
  return g;
}

/*************************************************
 * Memory Models
 *************************************************/
typedef struct {
  int manufacturer;
  int model;
  const char *name;
} memtype_t;

static memtype_t memtypes[] = {
    { 0x1, -1, "Unknown Samsung" },
    { 0x1, 0x0, "Samsung K4G20325FD" },
    { 0x1, 0x3, "Samsung K4G20325FD" },
    { 0x1, 0x2, "Samsung K4G80325FB" },
    { 0x1, 0x6, "Samsung K4G20325FS" },
    { 0x1, 0x9, "Samsung K4G41325FE" },
    { 0x2, -1, "Unknown Infineon" },
    { 0x3, -1, "Unknown Elpida" },
    { 0x3, 0x0, "Elpida EDW4032BABG" },
    { 0x3, 0x1, "Elpida EDW2032BBBG" },
    { 0x4, -1, "Unknown Etron" },
    { 0x5, -1, "Unknown Nanya" },
    { 0x6, -1, "Unknown Hynix" },
    { 0x6, 0x0, "SK Hynix H5VR2GCCM" },
    { 0x6, 0x2, "SK Hynix H5GQ2H24MFR" },
    { 0x6, 0x3, "SK Hynix H5GQ2H24AFR" },
    { 0x6, 0x4, "SK Hynix H5GC2H24BFR" },
    { 0x6, 0x5, "SK Hynix H5GQ4H24MFR" },
    { 0x6, 0x6, "SK Hynix H5GC4H24AJR" },
    { 0x7, -1, "Unknown Mosel" },
    { 0x8, -1, "Unknown Winbond" },
    { 0x9, -1, "Unknown ESMT" },
    { 0xf, -1, "Unknown Micron" },
    { 0xf, 0xf, "Micron MT51J256M3" },
    { 0x0, -1, "Unknown" }
};

// Find Memory Model by manufacturer/model
static memtype_t *find_mem(int manufacturer, int model)
{
  memtype_t *m = memtypes; //, *last = NULL;

  while (m->manufacturer)
  {
    if (m->manufacturer == manufacturer && m->model == model) {
      //last = m;

      //if (m->model == model)
      return m;
    }

    ++m;
  }

  if (model > -1) {
    return find_mem(manufacturer, -1);
  }

  return NULL;
}

/**********************************************
 * Device List
 **********************************************/

typedef struct gpu {
  u16 vendor_id, device_id;
  gputype_t *gpu;
  memtype_t *mem;
  int memconfig, mem_manufacturer, mem_model;
  u8 pcibus, pcidev, pcifunc, pcirev;
  int opencl_id;
  u32 subvendor, subdevice;
  char *path;
  unsigned char *vbios;
  char bios_version[64];
  struct gpu *prev, *next;
} gpu_t;

static gpu_t *device_list = NULL, *last_device = NULL;

// add new device
static gpu_t *new_device()
{
  gpu_t *d;

  if ((d = (gpu_t *)malloc(sizeof(gpu_t))) == NULL) {
    print(LOG_ERROR, "malloc() failed in new_device()\n");
    return NULL;
  }

  // default values
  d->gpu = NULL;
  d->mem = NULL;
  d->vbios = NULL;
  memset(d->bios_version, 0, 64);
  d->opencl_id = -1;
  d->next = d->prev = NULL;

  if (device_list == NULL && last_device == NULL) {
    device_list = last_device = d;
  } else {
    last_device->next = d;
    d->prev = last_device;
    last_device = d;
  }

  return d;
}

// free device memory
static void free_devices()
{
  gpu_t *d;

  while(last_device)
  {
    d = last_device;
    last_device = d->prev;

    if (d->vbios != NULL) {
      free(d->vbios);
    }

    free((void *)d);
  }

  last_device = device_list = NULL;
}

// find device by pci bus/dev/func
static gpu_t *find_device(u8 bus, u8 dev, u8 func)
{
  gpu_t *d = device_list;

  while (d)
  {
    if (d->pcibus == bus && d->pcidev == dev && d->pcifunc == func) {
      return d;
    }

    d = d->next;
  }

  return NULL;
}

// reorder devices based on opencl ID
static void opencl_reorder()
{
  gpu_t *p, *d = device_list;

  while (d)
  {
    // if not at the end of the list
    if (d->next) {
      // and next open cl ID is less than current...
      if (d->opencl_id > d->next->opencl_id) {
        // swap positions
        p = d->next;

        d->next = p->next;
        p->prev = d->prev;

        if (d->next) {
          d->next->prev = d;
        } else {
          last_device = d;
        }

        if (p->prev) {
          p->prev->next = p;
        } else {
          device_list = p;
        }

        p->next = d;
        d->prev = p;

        // start over from the beginning
        d = device_list;
      // next open cl ID is equal or higher, move on to the next
      } else {
        d = d->next;
      }
    // if at end of list, move up to exit loop
    } else {
      d = d->next;
    }
  }
}

/***********************************************
 * OpenCL functions
 ***********************************************/
#ifdef CL_DEVICE_TOPOLOGY_AMD
static bool opencl_get_platform(cl_platform_id *platform)
{
  cl_int status;
  cl_uint numPlatforms;
  cl_platform_id *platforms = NULL;
  bool ret = false;

  if ((status = clGetPlatformIDs(0, NULL, &numPlatforms)) == CL_SUCCESS) {
    if (numPlatforms > 0) {
      if ((platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id))) != NULL) {
        if (((status = clGetPlatformIDs(numPlatforms, platforms, NULL)) == CL_SUCCESS)) {
          // get first platform
          *platform = platforms[0];
          ret = true;
        } else {
          print(LOG_ERROR, "clGetPlatformIDs() failed: Unable to get OpenCL platform ID.\n");
        }
      } else {
        print(LOG_ERROR, "malloc() failed in opencl_get_platform().\n");
      }
    } else {
      print(LOG_ERROR, "No OpenCL platforms found.\n");
    }
  } else {
    print(LOG_ERROR, "clGetPlatformIDs() failed: Unable to get number of OpenCL platforms.\n");
  }

  // free memory
  if (platforms)
    free(platforms);

  return ret;
}

static int opencl_get_devices()
{
  cl_int status;
  cl_platform_id platform;
  cl_device_id *devices;
  cl_uint numDevices;
  int ret = -1;

  if (opencl_get_platform(&platform)) {
    if ((status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices)) == CL_SUCCESS) {
      if (numDevices) {
        if ((devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id))) != NULL) {
          if ((status = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, numDevices, devices, NULL)) == CL_SUCCESS) {
            unsigned int i;
            cl_uint intval;

            for (i = 0;i < numDevices; ++i)
            {
              clGetDeviceInfo(devices[i], CL_DEVICE_VENDOR_ID, sizeof(intval), &intval, NULL);

              // if vendor AMD, lookup pci ID
              if (intval == 0x1002) {
                cl_device_topology_amd amdtopo;
                gpu_t *dev;

                if ((status = clGetDeviceInfo(devices[i], CL_DEVICE_TOPOLOGY_AMD, sizeof(amdtopo), &amdtopo, NULL)) == CL_SUCCESS) {

                  if ((dev = find_device((u8)amdtopo.pcie.bus, (u8)amdtopo.pcie.device, (u8)amdtopo.pcie.function)) != NULL) {
                    dev->opencl_id = i;
                  }
                } else {
                  print(LOG_ERROR, "CL_DEVICE_TOPOLOGY_AMD Failed: Unable to map OpenCL device to PCI device.\n");
                }
              }

              ret = numDevices;
            }
          } else {
            print(LOG_ERROR, "CL_DEVICE_TYPE_GPU Failed: Unable to get OpenCL devices.\n");
          }

          free(devices);
        } else {
          print(LOG_ERROR, "malloc() failed in opencl_get_devices().\n");
        }
      }
    } else {
      print(LOG_ERROR, "CL_DEVICE_TYPE_GPU Failed: Unable to get the number of OpenCL devices.\n");
    }
  }

  return ret;
}
#else
static int opencl_get_devices()
{
  return 0;
}
#endif


/***********************************************
 * VBIOS functions
 ***********************************************/
static size_t dump_vbios(gpu_t *gpu)
{
  size_t success = 0;
  char obj[1024];
  FILE *fp;
  
  sprintf(obj, "%s/rom", gpu->path);
  
  //unlock vbios
  if ((fp = fopen(obj, "w")) == NULL) {
    print(LOG_ERROR, "%02x:%02x.%x: Unable to unlock vbios\n", gpu->pcibus, gpu->pcidev, gpu->pcifunc);
    return 0;
  }
  
  fputs("1\n", fp);
  fclose(fp);
  
  //if vbios buffer in use, free it
  if (gpu->vbios != NULL) {
    free(gpu->vbios);
  }
  
  //allocate 64k for vbios - could be larger but for now only read 64k
  if ((gpu->vbios = (unsigned char *)malloc(0x10000)) == NULL) {
    print(LOG_ERROR, "%02x:%02x.%x: Unable to allocate memory for vbios\n", gpu->pcibus, gpu->pcidev, gpu->pcifunc);
    goto relock;
  }
  
  //read vbios into buffer
  if ((fp = fopen(obj, "r")) == NULL) {
    print(LOG_ERROR, "%02x:%02x.%x: Unable to read vbios\n", gpu->pcibus, gpu->pcidev, gpu->pcifunc);
    free(gpu->vbios);
    goto relock;
  }
  
  success = fread(gpu->vbios, 0x10000, 1, fp);
  fclose(fp);

  //temp fix some gpus returned less than 64k...
  success = 1;
  
relock:
  //relock vbios
  if ((fp = fopen(obj, "w")) == NULL) {
    print(LOG_ERROR, "%02x:%02x.%x: Unable to relock vbios\n", gpu->pcibus, gpu->pcidev, gpu->pcifunc);
    return 0;
  }

  fputs("0\n", fp);
  fclose(fp);
  
  return success;
}

static u8 rbios8(u8 *vbios, long offset)
{
  return vbios[offset];
}

static u16 rbios16(u8 *vbios, long offset)
{
  return ((u16)vbios[offset] | (((u16)vbios[offset+1]) << 8));
}

static u32 rbios32(u8 *vbios, long offset)
{
  return ((u32)rbios16(vbios, offset) | (((u32)rbios16(vbios,offset+2)) << 16));
}

static void get_bios_version(gpu_t *gpu)
{
  char c, *p, *v;
  u16 ver_offset = rbios16(gpu->vbios, 0x6e);
  int len;
  
  p = (char *)(gpu->vbios+ver_offset);
  v = gpu->bios_version;
  len = 0;
  
  memset(v, 0, 64);
  
  while (((c = *(p++)) != 0) && len < 63) {
    *(v++) = c;
    ++len;
  }
}

/*
 * Find all suitable cards, then find their memory space and get memory information.
 */
int main(int argc, char *argv[])
{
  gpu_t *d;
  struct pci_access *pci;
  struct pci_dev *pcidev;
  int i, meminfo, manufacturer, model;
  char buf[1024];
  off_t base;
  int *pcimem;
  int fd;
  int fail=0;

  if (!load_options(argc, argv)) {
    return 0;
  }

  print(LOG_INFO, "%s\n", VERSION);

  pci = pci_alloc();
  pci_init(pci);
  pci_scan_bus(pci);
  
  char *sysfs_path = pci_get_param(pci, "sysfs.path");

  for (pcidev = pci->devices; pcidev; pcidev = pcidev->next)
  {
    if (((pcidev->device_class & 0xff00) >> 8) == PCI_BASE_CLASS_DISPLAY && pcidev->vendor_id == 0x1002) {
      if ((d = new_device()) != NULL) {
        d->vendor_id = pcidev->vendor_id;
        d->device_id = pcidev->device_id;
        d->pcibus = pcidev->bus;
        d->pcidev = pcidev->dev;
        d->pcifunc = pcidev->func;
        d->subvendor = pci_read_word(pcidev, PCI_SUBSYSTEM_VENDOR_ID);
        d->subdevice = pci_read_word(pcidev, PCI_SUBSYSTEM_ID);
        d->pcirev = pci_read_byte(pcidev, PCI_REVISION_ID);

        memset(buf, 0, 1024);
        sprintf(buf, "%s/devices/%04x:%02x:%02x.%d", sysfs_path, pcidev->domain, pcidev->bus, pcidev->dev, pcidev->func);
        d->path = strdup(buf);
        
        //printf("%s\n", d->path);

       // printf("* Vendor: %04x, Device: %04x, Revision: %02x\n", pcidev->vendor_id, pcidev->device_id, d->pcirev);

        d->gpu = find_gpu(pcidev->vendor_id, pcidev->device_id, d->subdevice, d->pcirev);
        
        if (dump_vbios(d)) {
          /*printf("%02x.%02x.%x: vbios dump successful.\n", d->pcibus, d->pcidev, d->pcifunc);
          printf("%x %x\n", d->vbios[0], d->vbios[1]);*/
          get_bios_version(d);
        }
        /*else {
          printf("%02x.%02x.%x: vbios dump failed.\n", d->pcibus, d->pcidev, d->pcifunc);
        }*/

        for (i=6;--i;)
        {
          if (pcidev->size[i] == 0x40000) {
            base = (pcidev->base_addr[i] & 0xfffffff0);
            fd = open("/dev/mem", O_RDONLY);

            if ((pcimem = (int *)mmap(NULL, 0x20000, PROT_READ, MAP_SHARED, fd, base)) != MAP_FAILED) {
              meminfo = pcimem[0xa80];
              manufacturer = (meminfo & 0xf00) >> 8;
              model = (meminfo & 0xf000) >> 12;

              d->memconfig = meminfo;
              d->mem_manufacturer = manufacturer;
              d->mem_model = model;
              d->mem = find_mem(manufacturer, model);

              munmap(pcimem, 0x20000);
            } else {
              ++fail;
            }

            close(fd);

            // memory model found so exit loop
            if (d->mem != NULL)
              break;
          }
        }
      }
    }
  }

  pci_cleanup(pci);

  // get open cl device ids and link them to pci devices found
  int numopencl = opencl_get_devices();

  // reorder by opencl id?
  if (opt_opencl_order) {
    opencl_reorder();
  }

  //display info
  d = device_list;

  while (d)
  {
    // short form
    if (opt_output_short) {

      if (d->opencl_id > -1) {
        printf("GPU%d:", d->opencl_id);
      } else {
        printf("GPU:");
      }

      //only output bios version
      if (opt_bios_only) {
        printf("%s\n", d->bios_version);
      }
      //standard short form
      else {
        printf("%02x.%02x.%x:", d->pcibus, d->pcidev, d->pcifunc);

        if (d->gpu && d->gpu->vendor_id != 0) {
          printf("%s:", d->gpu->name);
        } else {
          printf("Unknown GPU %04x-%04xr%02x:",d->vendor_id, d->device_id, d->pcirev);
        }

        printf("%s:", d->bios_version);
        
        if (opt_show_memconfig) {
          printf("0x%x:", d->memconfig);
        }

        if (d->mem && d->mem->manufacturer != 0) {
          printf("%s\n", d->mem->name);
        } else {
          printf("Unknown Memory %d-%d\n", d->mem_manufacturer, d->mem_model);
        }
      }
    // long form (original)
    } else {
      if (d->gpu) {
        printf(	"-----------------------------------\n"
          "Found card: %04x:%04x rev %02x (AMD %s)\n"
          "Bios Version: %s\n"
          "PCI: %02x:%02x.%x\n"
          "OpenCL ID: %d\n"
          "Subvendor:  0x%04x\n"
          "Subdevice:  0x%04x\n"
          "Sysfs Path: %s\n",
          d->gpu->vendor_id, d->gpu->device_id, d->pcirev, d->gpu->name,
          d->bios_version,
          d->pcibus, d->pcidev, d->pcifunc,
          d->opencl_id,
          d->subvendor, d->subdevice,
          d->path);

        if (opt_show_memconfig) {
          printf("Memory Configuration: 0x%x\n", d->memconfig);
        }

        printf("Memory type: ");

        if (d->mem && d->mem->manufacturer != 0) {
          printf("%s\n", d->mem->name);
        } else {
          printf("Unknown Memory - Mfr:%d Model:%d\n", d->mem_manufacturer, d->mem_model);
        }
      }
      else {
        printf(	"-----------------------------------\n"
          "Unknown card: %04x:%04x rev %02x\n"
          "PCI: %02x:%02x.%x\n"
          "Subvendor:  0x%04x\n"
          "Subdevice:  0x%04x\n",
          d->vendor_id, d->device_id, d->pcirev,
          d->pcibus, d->pcidev, d->pcifunc,
          d->subvendor, d->subdevice);
      }
    }

    d = d->next;
  }

  free_devices();

  if (fail) {
    print(LOG_ERROR, "Direct PCI access failed. Run AMDMemInfo as root to get memory type information!\n");
  }

  return 0;
}


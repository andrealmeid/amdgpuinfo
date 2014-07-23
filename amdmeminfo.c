/*
 * AMDMemInfo, (c) 2014 by Zuikkis <zuikkis@gmail.com>
 * Adapted for PiMP (www.getpimp.org) by Yann St.Arnaud <ystarnaud@gmail.com>
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
      "Adapted for PiMP (www.getpimp.org) by Yann St.Arnaud <ystarnaud@gmail.com>"

#define LOG_INFO 1
#define LOG_ERROR 2

/***********************************
 * Program Options
 ***********************************/
bool opt_opencl_order = false; // --opencl / -o
bool opt_output_short = false; // --short / -s
bool opt_quiet = false;  // --quiet / -q to turn off
bool opt_use_stderr = false;  // --use-stderr

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
    "-h, --help      Help\n"
    "-o, --opencl    Order by OpenCL ID (cgminer/sgminer GPU order)\n"
    "-q, --quiet     Only output results\n"
    "-s, --short     Short form output - 1 GPU/line - <OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<Memory Type>\n"
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
    } else if (!strcasecmp("--short", argv[i]) || !strcasecmp("-s", argv[i])) {
      opt_output_short = true;
    } else if (!strcasecmp("--quiet", argv[i]) || !strcasecmp("-q", argv[i])) {
      opt_quiet = true;
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
  unsigned long vendor_id;
  unsigned long device_id;
  const char *name;
} gputype_t;

static gputype_t gputypes[] = {
    { 0x1002, 0x67b1, "Radeon R9 290" },
    { 0x1002, 0x67b0, "Radeon R9 290x" },
    { 0x1002, 0x6798, "Radeon HD7970/R9 280x" },
    { 0x1002, 0x679a, "Radeon HD7950/R9 280" },
    { 0x1002, 0x6811, "Radeon R9 270" },
    { 0x1002, 0x6810, "Radeon R9 270x" },
    { 0x1002, 0x6658, "Radeon R7 260x" },
    { 0x1002, 0x679b, "Radeon HD7990" },
    { 0x1002, 0x679E, "Radeon HD7870XT" },
    { 0x1002, 0x6818, "Radeon HD7870" },
    { 0x1002, 0x6819, "Radeon HD7850" },
    { 0x1002, 0x665C, "Radeon HD7790" },
    { 0x1002, 0x671D, "Radeon HD6990" },
    { 0x1002, 0x6718, "Radeon HD6970" },
    { 0x1002, 0x6719, "Radeon HD6950" },
    { 0x1002, 0x671F, "Radeon HD6930" },
    { 0x1002, 0x6738, "Radeon HD6870" },
    { 0x1002, 0x6739, "Radeon HD6850" },
    { 0x1002, 0x6778, "Radeon HD6450/HD7470" },
    { 0x1002, 0x6779, "Radeon HD6450" },
    { 0x1002, 0x689C, "Radeon HD5970" },
    { 0x1002, 0x6898, "Radeon HD5870" },
    { 0x1002, 0x6899, "Radeon HD5850" },
    { 0x1002, 0x689E, "Radeon HD5830" },
    { 0, 0, "Unknown"}
};

// find GPU type by vendor id/device id
static gputype_t *find_gpu(unsigned long vendor_id, unsigned long device_id)
{
  gputype_t *g = gputypes;

  while (g->device_id)
  {
    if (g->vendor_id == vendor_id && g->device_id == device_id) {
      return g;
    }

    ++g;
  }

  return NULL;
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
    { 1, 0, "Samsung K4G20325FD" },
    { 3, 0, "Elpida EDW2032BBBG" },
    { 6, 2, "SK Hynix H5GQ2H24MFR" },
    { 6, 3, "SK Hynix H5GQ2H24AFR" },
    { 6, 0, "Unknown Hynix" },
    { 0, 0, "Unknown" }
};

// Find Memory Model by manufacturer/model
static memtype_t *find_mem(int manufacturer, int model)
{
  memtype_t *m = memtypes, *last = NULL;

  while (m->manufacturer)
  {
    if (m->manufacturer == manufacturer)
    {
      last = m;

      if (m->model == model)
          return m;
    }

    ++m;
  }

  return last;
}

/**********************************************
 * Device List
 **********************************************/

typedef struct gpu {
  u16 vendor_id, device_id;
  gputype_t *gpu;
  memtype_t *mem;
  int mem_manufacturer, mem_model;
  u8 pcibus, pcidev, pcifunc;
  int opencl_id;
  u32 subvendor, subdevice;
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

/*
 * Find all suitable cards, then find their memory space and get memory information.
 */
int main(int argc, char *argv[])
{
  gpu_t *d;
  struct pci_access *pci;
  struct pci_dev *pcidev;
  int i, meminfo, manufacturer, model;
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

  for (pcidev = pci->devices; pcidev; pcidev = pcidev->next)
  {
    if (pcidev->device_class == PCI_CLASS_DISPLAY_VGA && pcidev->vendor_id == 0x1002) {
      if ((d = new_device()) != NULL) {
        d->gpu = find_gpu(pcidev->vendor_id, pcidev->device_id);
        d->vendor_id = pcidev->vendor_id;
        d->device_id = pcidev->device_id;
        d->pcibus = pcidev->bus;
        d->pcidev = pcidev->dev;
        d->pcifunc = pcidev->func;
        d->subvendor = pci_read_word(pcidev, PCI_SUBSYSTEM_VENDOR_ID);
        d->subdevice = pci_read_word(pcidev, PCI_SUBSYSTEM_ID);

        for (i=6;--i;)
        {
          if (pcidev->size[i] == 0x40000) {
            base = (pcidev->base_addr[i] & 0xfffffff0);
            fd = open("/dev/mem", O_RDONLY);

            if ((pcimem = (int *)mmap(NULL, 0x20000, PROT_READ, MAP_SHARED, fd, base)) != MAP_FAILED) {
              meminfo = pcimem[0xa80];
              manufacturer = (meminfo & 0xf00) >> 8;
              model = (meminfo & 0xf000) >> 12;

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

      printf("%02x.%02x.%x:", d->pcibus, d->pcidev, d->pcifunc);

      if (d->gpu && d->gpu->vendor_id != 0) {
        printf("%s:", d->gpu->name);
      } else {
        printf("Unknown GPU %04x-%04x:",d->vendor_id, d->device_id);
      }

      if (d->mem && d->mem->manufacturer != 0) {
        printf("%s\n", d->mem->name);
      } else {
        printf("Unknown Memory %d-%d\n", d->mem_manufacturer, d->mem_model);
      }
    // long form (original)
    } else {
      printf(	"-----------------------------------\n"
        "Found card: %04x:%04x (AMD %s)\n"
        "PCI: %02x:%02x.%x\n"
        "OpenCL ID: %d\n"
        "Subvendor:  0x%x\n"
        "Subdevice:  0x%x\n",
        d->gpu->vendor_id, d->gpu->device_id, d->gpu->name,
        d->pcibus, d->pcidev, d->pcifunc,
        d->opencl_id,
        d->subvendor, d->subdevice);

      printf("Memory type: ");

      if (d->mem && d->mem->manufacturer != 0) {
        printf("%s\n", d->mem->name);
      } else {
        printf("Unknown Memory - Mfr:%d Model:%d\n", d->mem_manufacturer, d->mem_model);
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


/*
 * AMDGPUInfo
 *
 * (C) 2014 Zuikkis <zuikkis@gmail.com>
 * (C) 2018 Yann St.Arnaud <ystarnaud@gmail.com>
 * (C) 2020 André Almeida <andrealmeid@riseup.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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
#include <regex.h>
#include <strings.h>

#include "config.h"

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif
#ifdef CL_DEVICE_TOPOLOGY_AMD
  #define USE_OPENCL 1
#endif

#define LOG_INFO 1
#define LOG_ERROR 2

#define MEM_UNKNOWN 0x0
#define MEM_GDDR5 0x5
#define MEM_HBM  0x6
#define MEM_GDDR6 0x7

#define mmMC_SEQ_MISC0 0xa80
#define mmMC_SEQ_MISC0_FIJI 0xa71

#define AMD_PCI_VENDOR_ID 0x1002

#define BLANK_BIOS_VER "xxx-xxx-xxxx"

typedef enum AMD_CHIPS {
  CHIP_UNKNOWN = 0,
  CHIP_CYPRESS,
  CHIP_HEMLOCK,
  CHIP_CAICOS,
  CHIP_BARTS,
  CHIP_CAYMAN,
  CHIP_ANTILLES,
  CHIP_TAHITI,
  CHIP_PITCAIRN,
  CHIP_VERDE,
  CHIP_OLAND,
  CHIP_HAINAN,
  CHIP_BONAIRE,
  CHIP_KAVERI,
  CHIP_KABINI,
  CHIP_HAWAII,
  CHIP_MULLINS,
  CHIP_TOPAZ,
  CHIP_TONGA,
  CHIP_FIJI,
  CHIP_CARRIZO,
  CHIP_STONEY,
  CHIP_POLARIS10,
  CHIP_POLARIS11,
  CHIP_POLARIS12,
  CHIP_POLARIS20,
  CHIP_POLARIS30,
  CHIP_VEGA10,
  CHIP_VEGA20,
  CHIP_NAVI10,
  CHIP_NAVI12,
  CHIP_NAVI14,
  CHIP_RAVEN,
} asic_type_t;

static const char *mem_type_label[] = {
  "Unknown",
  "DDR1",
  "DDR2",
  "DDR3",
  "DDR4",
  "GDDR5",
  "GDDR6",
  "HBM",
};

static const char *amd_asic_name[] = {
  "Unknown",
  "Cypress",
  "Hemlock",
  "Caicos",
  "Barts",
  "Cayman",
  "Antilles",
  "Tahiti",
  "Pitcairn",
  "Verde",
  "Oland",
  "Hainan",
  "Bonaire",
  "Kaveri",
  "Kabini",
  "Hawaii",
  "Mullins",
  "Topaz",
  "Tonga",
  "Fiji",
  "Carrizo",
  "Stoney",
  "Polaris10",
  "Polaris11",
  "Polaris12",
  "Polaris20",
  "Polaris30",
  "Vega10",
  "Vega20",
  "Navi10",
  "Navi12",
  "Navi14",
  "Raven",
};

/***********************************
 * Program Options
 ***********************************/
bool opt_bios_only = false; // --biosonly / -b
bool opt_opencl_order = false; // --opencl / -o
bool opt_output_short = false; // --short / -s
bool opt_quiet = false;  // --quiet / -q to turn off
#ifdef USE_OPENCL
bool opt_opencl_enabled = true;  // --no-opencl / -n to turn off
#endif
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
  printf(
    NAME " v"  VERSION "\n\n"
    "Usage: %s [options]\n\n"
    "Options:\n"
    "-b, --biosonly  Only output BIOS Versions (implies -s with <OpenCLID>:<BIOSVersion> output)\n"
    "-c, --memconfig Output the memory configuration\n"
    "-h, --help      Help\n"
    "-n, --no-opencl Disable OpenCL information lookup\n"
#ifdef USE_OPENCL
    "-o, --opencl    Order by OpenCL ID (cgminer/sgminer GPU order)\n"
#endif
    "-q, --quiet     Only output results\n"
    "-s, --short     Short form output - 1 GPU/line - <OpenCLID>:<PCI Bus.Dev.Func>:<GPU Type>:<BIOSVersion>:<Memory Type>\n"
    "--use-stderr    Output errors to stderr\n"
    "\n", program);
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
#ifdef OPENCL
    } else if (!strcasecmp("--no-opencl", argv[i]) || !strcasecmp("-n", argv[i])) {
      opt_opencl_enabled = false;
#endif
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
  unsigned int device_id;
  unsigned long subsys_id;
  unsigned char rev_id;
  const char *name;
  unsigned int asic_type;
} gputype_t;

static gputype_t gputypes[] = {
    /* Vega */
    { 0x687f, 0, 0,    "Radeon RX Vega", CHIP_VEGA10},
    { 0x687f, 0, 0xc0, "Radeon RX Vega 64", CHIP_VEGA10},
    { 0x687f, 0, 0xc1, "Radeon RX Vega 64", CHIP_VEGA10},
    { 0x687f, 0, 0xc3, "Radeon RX Vega 56", CHIP_VEGA10},
    { 0x6863, 0, 0,    "Radeon Vega FE", CHIP_VEGA10},

    /*Vega20*/
    { 0x66af, 0, 0,    "Radeon VII", CHIP_VEGA20},
    { 0x66af, 0, 0xc4, "Radeon VII", CHIP_VEGA20},

    /*Navi10*/
    { 0x7310, 0, 0,    "Radeon RX 5700", CHIP_NAVI10},
    { 0x7312, 0, 0,    "Radeon Pro W5700", CHIP_NAVI10},
    { 0x7318, 0, 0,    "Radeon RX 5700", CHIP_NAVI10},
    { 0x7319, 0, 0,    "Radeon RX 5700", CHIP_NAVI10},
    { 0x731a, 0, 0,    "Radeon RX 5700", CHIP_NAVI10},
    { 0x731b, 0, 0,    "Radeon RX 5700", CHIP_NAVI10},
    { 0x731f, 0, 0,    "Radeon RX 5600/5700", CHIP_NAVI10},
    { 0x731f, 0, 0xc0, "Radeon RX 5700 XT", CHIP_NAVI10}, /* XTX or 50th Anniversary Edition */
    { 0x731f, 0, 0xc1, "Radeon RX 5700 XT", CHIP_NAVI10},
    { 0x731f, 0, 0xc4, "Radeon RX 5700",    CHIP_NAVI10},
    { 0x731f, 0, 0xca, "Radeon RX 5600 XT", CHIP_NAVI10},
    /* Navi12 */
    { 0x7360, 0, 0, "Radeon Navi 12", CHIP_NAVI12},
    { 0x7362, 0, 0, "Radeon Navi 12", CHIP_NAVI12},

    /*Navi14*/
    { 0x7340, 0, 0,    "Radeon RX 5500",    CHIP_NAVI14},
    { 0x7340, 0, 0xc5, "Radeon RX 5500 XT", CHIP_NAVI14},
    { 0x7341, 0, 0, "Radeon Pro W5500", CHIP_NAVI14},
    { 0x7347, 0, 0, "Radeon Pro W5500M", CHIP_NAVI14},
    { 0x734f, 0, 0, "Radeon Pro W5500M", CHIP_NAVI14},

    /* Fury/Nano */
    { 0x7300, 0, 0,    "Radeon R9 Fury/Nano/X", CHIP_FIJI},
    { 0x7300, 0, 0xc8, "Radeon R9 Fury/Nano/X", CHIP_FIJI},
    { 0x7300, 0, 0xc9, "Radeon R9 Fury/Nano/X", CHIP_FIJI},
    { 0x7300, 0, 0xca, "Radeon R9 Fury/Nano/X", CHIP_FIJI},
    { 0x7300, 0, 0xcb, "Radeon R9 Fury", CHIP_FIJI},
    /* RX 5xx */
    { 0x67df, 0, 0xe7, "Radeon RX 580", CHIP_POLARIS10},
    { 0x67df, 0, 0xef, "Radeon RX 570", CHIP_POLARIS10},

    { 0x67df, 0, 0xe1, "Radeon RX 590", CHIP_POLARIS30},   /* AMD Radeon RX 590 */
    { 0x6fdf, 0, 0xef, "Radeon RX 580", CHIP_POLARIS20},   /* AMD Radeon RX 580 2048SP */

    { 0x67ff, 0, 0xcf, "Radeon RX 560", CHIP_POLARIS11},
    { 0x67ef, 0, 0xe5, "Radeon RX 560", CHIP_POLARIS11},  /* known also as RX560D with CU 14/shaders 896 */
    { 0x67ff, 0, 0xff, "Radeon RX 550", CHIP_POLARIS11},  /* new RX550 with 640 shaders */
    { 0x699f, 0, 0xc7, "Radeon RX 550", CHIP_POLARIS12},
    /* RX 4xx */
    { 0x67df, 0, 0, "Radeon RX 470/480", CHIP_POLARIS10},
    { 0x67df, 0, 0xc7, "Radeon RX 480", CHIP_POLARIS10},
    { 0x67df, 0, 0xcf, "Radeon RX 470", CHIP_POLARIS10},
    { 0x67ef, 0, 0, "Radeon RX 460", CHIP_POLARIS11},
    { 0x67ef, 0, 0xc0, "Radeon RX 460", CHIP_POLARIS11},
    { 0x67ef, 0, 0xc1, "Radeon RX 460", CHIP_POLARIS11},
    { 0x67ef, 0, 0xc5, "Radeon RX 460", CHIP_POLARIS11},
    { 0x67ef, 0, 0xcf, "Radeon RX 460", CHIP_POLARIS11},
    /* R9 3xx */
    { 0x67b1, 0, 0x80, "Radeon R9 390", CHIP_HAWAII},
    { 0x67b0, 0, 0x80, "Radeon R9 390x", CHIP_HAWAII},
    { 0x6939, 0, 0xf1, "Radeon R9 380", CHIP_TONGA},
    { 0x6938, 0, 0, "Radeon R9 380x", CHIP_TONGA},
    { 0x6810, 0, 0x81, "Radeon R7 370", CHIP_PITCAIRN},
    { 0x665f, 0, 0x81, "Radeon R7 360", CHIP_BONAIRE},
    /* R9 2xx */
    { 0x67B9, 0, 0, "Radeon R9 295x2", CHIP_HAWAII},
    { 0x67b1, 0, 0, "Radeon R9 290/R9 390", CHIP_HAWAII},
    { 0x67b0, 0, 0, "Radeon R9 290x/R9 390x", CHIP_HAWAII},
    { 0x6939, 0, 0, "Radeon R9 285/R9 380", CHIP_TONGA},
    { 0x6811, 0, 0, "Radeon R9 270", CHIP_PITCAIRN},
    { 0x6810, 0, 0, "Radeon R9 270x/R7 370", CHIP_PITCAIRN},
    { 0x6658, 0, 0, "Radeon R7 260x", CHIP_BONAIRE},
    /* HD 7xxx */
    { 0x679b, 0, 0, "Radeon HD7990", CHIP_TAHITI},
    { 0x6798, 0, 0, "Radeon HD7970/R9 280x", CHIP_TAHITI},
    { 0x679a, 0, 0, "Radeon HD7950/R9 280", CHIP_TAHITI},
    { 0x679E, 0, 0, "Radeon HD7870XT", CHIP_TAHITI},
    { 0x6818, 0, 0, "Radeon HD7870", CHIP_PITCAIRN},
    { 0x6819, 0, 0, "Radeon HD7850", CHIP_PITCAIRN},
    { 0x665C, 0, 0, "Radeon HD7790", CHIP_BONAIRE},
    /* HD 6xxx */
    { 0x671D, 0, 0, "Radeon HD6990", CHIP_ANTILLES},
    { 0x6718, 0, 0, "Radeon HD6970", CHIP_CAYMAN},
    { 0x6719, 0, 0, "Radeon HD6950", CHIP_CAYMAN},
    { 0x671F, 0, 0, "Radeon HD6930", CHIP_CAYMAN},
    { 0x6738, 0, 0, "Radeon HD6870", CHIP_BARTS},
    { 0x6739, 0, 0, "Radeon HD6850", CHIP_BARTS},
    { 0x6778, 0, 0, "Radeon HD6450/HD7470", CHIP_CAICOS},
    { 0x6779, 0, 0, "Radeon HD6450", CHIP_CAICOS},
    /* HD 5xxx */
    { 0x689C, 0, 0, "Radeon HD5970", CHIP_HEMLOCK},
    { 0x6898, 0, 0, "Radeon HD5870", CHIP_CYPRESS},
    { 0x6899, 0, 0, "Radeon HD5850", CHIP_CYPRESS},
    { 0x689E, 0, 0, "Radeon HD5830", CHIP_CYPRESS},
    { 0, 0, 0, "Unknown", CHIP_UNKNOWN}
};

// find GPU type by vendor id/device id
static gputype_t *_find_gpu(unsigned int device_id, unsigned long subsys_id, unsigned char rev_id)
{
  gputype_t *g = gputypes;

  while (g->device_id)
  {
    if (g->device_id == device_id && g->subsys_id == subsys_id && g->rev_id == rev_id) {
      return g;
    }

    ++g;
  }

  return NULL;
}

// find GPU type by vendor id/device id
static gputype_t *find_gpu(unsigned int device_id, unsigned long subsys_id, unsigned char rev_id)
{
  gputype_t *g = _find_gpu(device_id, subsys_id, rev_id);

  //if specific subsys id not found, try again with 0
  if (g == NULL && subsys_id > 0) {
    g = _find_gpu(device_id, 0, rev_id);
  }

  //if specific rev id not found, try again with 0 for general device type
  if (g == NULL && rev_id > 0) {
    g = _find_gpu(device_id, subsys_id, 0);
  }

  //if still not found, try no rev id or subsys id
  if (g == NULL) {
    g = _find_gpu(device_id, 0, 0);
  }

  return g;
}

/*************************************************
 * Memory Models
 *************************************************/
typedef struct {
  int type;
  int manufacturer;
  int model;
  const char *name;
} memtype_t;


/*
 * Memory type information can be determined by using "amdmeminfo -c". This will output the MC scratch register value.
 * The format of the MC scratch register is: 0xTXXXMVXX where T = Memory Type, V = Vendor ID and M is Memory Model ID
 *
 * For example the value: 0x506021f2 translates to T = 0x5, V = 0x1 and M = 0x2. This leads us to the record below:
 *    { MEM_GDDR5, 0x1, 0x2, "Samsung K4G80325FB" }
 */
static memtype_t memtypes[] = {
    /* GDDR5 */
    { MEM_GDDR5, 0x1, -1, "Unknown Samsung GDDR5" },
    { MEM_GDDR5, 0x1, 0x0, "Samsung K4G20325FD" },
    { MEM_GDDR5, 0x1, 0x2, "Samsung K4G80325FB" },
    { MEM_GDDR5, 0x1, 0x3, "Samsung K4G20325FD" },
    { MEM_GDDR5, 0x1, 0x6, "Samsung K4G20325FS" },
    { MEM_GDDR5, 0x1, 0x9, "Samsung K4G41325FE" },
    { MEM_GDDR5, 0x2, -1, "Unknown Infineon GDDR5" },
    { MEM_GDDR5, 0x3, -1, "Unknown Elpida GDDR5 GDDR5" },
    { MEM_GDDR5, 0x3, 0x0, "Elpida EDW4032BABG" },
    { MEM_GDDR5, 0x3, 0x1, "Elpida EDW2032BBBG" },
    { MEM_GDDR5, 0x4, -1, "Unknown Etron GDDR5" },
    { MEM_GDDR5, 0x5, -1, "Unknown Nanya GDDR5" },
    { MEM_GDDR5, 0x6, -1, "Unknown SK Hynix GDDR5" },
    { MEM_GDDR5, 0x6, 0x2, "SK Hynix H5GQ2H24MFR" },
    { MEM_GDDR5, 0x6, 0x3, "SK Hynix H5GQ2H24AFR" },
    { MEM_GDDR5, 0x6, 0x4, "SK Hynix H5GC2H24BFR" },
    { MEM_GDDR5, 0x6, 0x5, "SK Hynix H5GQ4H24MFR" },
    { MEM_GDDR5, 0x6, 0x6, "SK Hynix H5GC4H24AJR" },
    { MEM_GDDR5, 0x6, 0x7, "SK Hynix H5GQ8H24MJR" },
    { MEM_GDDR5, 0x6, 0x8, "SK Hynix H5GC8H24AJR" },
    { MEM_GDDR5, 0x7, -1, "Unknown Mosel GDDR5" },
    { MEM_GDDR5, 0x8, -1, "Unknown Winbond GDDR5" },
    { MEM_GDDR5, 0x9, -1, "Unknown ESMT GDDR5" },
    { MEM_GDDR5, 0xf, -1, "Unknown Micron" },
    { MEM_GDDR5, 0xf, 0x1, "Micron MT51J256M32" },
    { MEM_GDDR5, 0xf, 0x0, "Micron MT51J256M3" },

    /* HBM */
    { MEM_HBM, 0x1, -1, "Unknown Samsung HBM" },
    { MEM_HBM, 0x1, 0, "Samsung KHA843801B" },
    { MEM_HBM, 0x2, -1, "Unknown Infineon HBM" },
    { MEM_HBM, 0x3, -1, "Unknown Elpida HBM" },
    { MEM_HBM, 0x4, -1, "Unknown Etron HBM" },
    { MEM_HBM, 0x5, -1, "Unknown Nanya HBM" },
    { MEM_HBM, 0x6, -1, "Unknown SK Hynix HBM" },
    { MEM_HBM, 0x6, 0x0, "SK Hynix H5VR2GCCM" },
    { MEM_HBM, 0x7, -1, "Unknown Mosel HBM" },
    { MEM_HBM, 0x8, -1, "Unknown Winbond HBM" },
    { MEM_HBM, 0x9, -1, "Unknown ESMT HBM" },
    { MEM_HBM, 0xf, -1, "Unknown Micron HBM" },

    /* GDDR6 */
    { MEM_GDDR6, 0x1, -1, "Samsung GDDR6"},
    { MEM_GDDR6, 0x1, 0x8, "Samsung K4Z80325BC"},
    { MEM_GDDR6, 0x6, -1, "Hynix GDDR6"},
    { MEM_GDDR6, 0xf, -1, "Micron GDDR6"},
    { MEM_GDDR6, 0xf, 0x0, "Micron MT61K256M32"},

    /* UNKNOWN LAST */
    { MEM_GDDR5, -1, -1, "GDDR5"},
    { MEM_GDDR6, -1, -1, "GDDR6"},
    { MEM_HBM, -1, -1, "Unknown HBM"},
    { MEM_UNKNOWN, -1, -1, "Unknown Memory"},
};


// Find Memory Model by manufacturer/model
static memtype_t *find_mem(int mem_type, int manufacturer, int model)
{
  memtype_t *m = memtypes;

  while (m->type)
  {
    if (m->type == mem_type && m->manufacturer == manufacturer && m->model == model)
      return m;

    ++m;
  }

  if (model > -1) {
    return find_mem(mem_type, manufacturer, -1);
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
  int memconfig, mem_type, mem_manufacturer, mem_model;
  u8 pcibus, pcidev, pcifunc, pcirev;
  int opencl_platform;
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
  d->path = NULL;
  memset(d->bios_version, 0, 64);
  d->opencl_platform = -1;
  d->opencl_id = -1;
  d->next = d->prev = NULL;
  d->mem_manufacturer = 0;
  d->mem_model = 0;

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

    if (d->path != NULL) {
      free(d->path);
    }

    free((void *)d);
  }

  last_device = device_list = NULL;
}

/***********************************************
 * OpenCL functions
 ***********************************************/
#ifdef USE_OPENCL
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

static cl_platform_id *opencl_get_platforms(int *platform_count)
{
  cl_int status;
  cl_platform_id *platforms = NULL;
  cl_uint numPlatforms;

  *platform_count = 0;

  if ((status = clGetPlatformIDs(0, NULL, &numPlatforms)) == CL_SUCCESS) {
    if (numPlatforms > 0) {
      if ((platforms = (cl_platform_id *)malloc(numPlatforms*sizeof(cl_platform_id))) != NULL) {
        if (((status = clGetPlatformIDs(numPlatforms, platforms, NULL)) == CL_SUCCESS)) {
          *platform_count = (int)numPlatforms;
          return platforms;
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
  if (platforms == NULL) {
    free(platforms);
    platforms = NULL;
  }

  return NULL;
}

static int opencl_get_devices()
{
  cl_int status;
  cl_platform_id *platforms = NULL;
  cl_device_id *devices;
  cl_uint numDevices;
  int p, numPlatforms, ret = -1;

  if ((platforms = opencl_get_platforms(&numPlatforms)) != NULL) {
    for(p = 0; p < numPlatforms; ++p) {
      if ((status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, 0, NULL, &numDevices)) == CL_SUCCESS) {
        if (numDevices) {
          if ((devices = (cl_device_id *)malloc(numDevices*sizeof(cl_device_id))) != NULL) {
            if ((status = clGetDeviceIDs(platforms[p], CL_DEVICE_TYPE_GPU, numDevices, devices, NULL)) == CL_SUCCESS) {
              unsigned int i;
              cl_uint intval;

              for (i = 0;i < numDevices; ++i)
              {
                clGetDeviceInfo(devices[i], CL_DEVICE_VENDOR_ID, sizeof(intval), &intval, NULL);

                // if vendor AMD, lookup pci ID
                if (intval == AMD_PCI_VENDOR_ID) {
                  cl_device_topology_amd amdtopo;
                  gpu_t *dev;

                  if ((status = clGetDeviceInfo(devices[i], CL_DEVICE_TOPOLOGY_AMD, sizeof(amdtopo), &amdtopo, NULL)) == CL_SUCCESS) {

                    if ((dev = find_device((u8)amdtopo.pcie.bus, (u8)amdtopo.pcie.device, (u8)amdtopo.pcie.function)) != NULL) {
                      dev->opencl_platform = p;
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
  }
  else {
    print(LOG_ERROR, "No OpenCL platforms detected.\n");
  }

  if (platforms != NULL) {
    free(platforms);
    platforms = NULL;
  }

  return ret;
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

#define rbios8(vbios, offset) *((u8 *)(vbios)+(offset))
#define rbios16(vbios, offset) *((u16 *)((vbios)+(offset)))
#define rbios32(vbios, offset) *((u32 *)((vbios)+(offset)))

static void get_bios_version(gpu_t *gpu)
{
  char c, *p, *v;
  u16 ver_offset = rbios16(gpu->vbios, 0x6e);
  int len;

  v = gpu->bios_version;
  memset(v, 0, 64);

  //check for invalid vbios
  if (*((u16 *)gpu->vbios) != 0xaa55) {
    return;
  }

  p = (char *)(gpu->vbios+ver_offset);
  len = 0;

  while (((c = *(p++)) != 0) && len < 63) {
    *(v++) = c;
    ++len;
  }
}

/*
 * Check if a device is an APU
 */
static bool is_apu(struct pci_access *pci, struct pci_dev *pcidev)
{
  bool is_apu = false;
  regex_t regex;
  char buf[1024];

  memset(buf, 0, 1024);

  if (pci_lookup_name(pci, buf, sizeof(buf), PCI_LOOKUP_DEVICE, pcidev->vendor_id, pcidev->device_id) != NULL) {
    if (regcomp(&regex, "(Kaveri|Beavercreek|Sumo|Wrestler|Kabini|Mullins|Temash|Trinity|Richland|Stoney|Carrizo|Raven)", REG_ICASE | REG_EXTENDED) == 0) {
      if (regexec(&regex, buf, 0, NULL, 0) == 0) {
        is_apu = true;
      }
    }
  }

  regfree(&regex);

  return is_apu;
}

/*
 * Find all suitable cards, then find their memory space and get memory information.
 */
int main(int argc, char *argv[])
{
  gpu_t *d;
  struct pci_access *pci;
  struct pci_dev *pcidev;
  int i, meminfo, manufacturer, model, mem_type, fd;
  char buf[1024];
  off_t base;
  int *pcimem;
  int fail=0;

  if (!load_options(argc, argv)) {
    return 0;
  }

  print(LOG_INFO, NAME " v" VERSION "\n");

  pci = pci_alloc();
  pci_init(pci);
  pci_scan_bus(pci);

  char *sysfs_path = pci_get_param(pci, "sysfs.path");

  for (pcidev = pci->devices; pcidev; pcidev = pcidev->next)
  {
    if (((pcidev->device_class & 0xff00) >> 8) == PCI_BASE_CLASS_DISPLAY && pcidev->vendor_id == AMD_PCI_VENDOR_ID) {

      // skip APUs
      if (is_apu(pci, pcidev))
        continue;

      if ((d = new_device()) != NULL) {
        d->vendor_id = AMD_PCI_VENDOR_ID;
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

        d->gpu = find_gpu(pcidev->device_id, d->subdevice, d->pcirev);

        if (dump_vbios(d))
          get_bios_version(d);

        //currenty Vega GPUs do not have a memory configuration register to read
	  if ((d->gpu->asic_type == CHIP_VEGA10) ||
            (d->gpu->asic_type == CHIP_VEGA20)) {
          d->memconfig = 0x61000000;
          d->mem_type = MEM_HBM;
          d->mem_manufacturer = 1;
          d->mem_model = 0;
          d->mem = find_mem(MEM_HBM, 1, 0);
        }
        else {
          for (i=6;--i;)
          {
            if (pcidev->size[i] == 0x40000) {
              base = (pcidev->base_addr[i] & 0xfffffff0);
              fd = open("/dev/mem", O_RDONLY);

              if ((pcimem = (int *)mmap(NULL, 0x20000, PROT_READ, MAP_SHARED, fd, base)) != MAP_FAILED) {
                if (d->gpu->asic_type == CHIP_FIJI) {
                  meminfo = pcimem[mmMC_SEQ_MISC0_FIJI];
                }
                else {
                  meminfo = pcimem[mmMC_SEQ_MISC0];
                }

                mem_type = (meminfo & 0xf0000000) >> 28;
                manufacturer = (meminfo & 0xf00) >> 8;
                model = (meminfo & 0xf000) >> 12;

                d->memconfig = meminfo;
                d->mem_type = mem_type;
                d->mem_manufacturer = manufacturer;
                d->mem_model = model;
                d->mem = find_mem(mem_type, manufacturer, model);

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
  }

  pci_cleanup(pci);

#ifdef USE_OPENCL
  // get open cl device ids and link them to pci devices found
  if (opt_opencl_enabled) {
    int numopencl = opencl_get_devices();

    // reorder by opencl id?
    if (opt_opencl_order) {
      opencl_reorder();
    }
  }
#endif

  //display info
  d = device_list;
  while (d)
  {
    //if bios version is blank, replace it with BLANK_BIOS_VER
    if (d->bios_version[0] == 0) {
      strcpy(d->bios_version, BLANK_BIOS_VER);
    }

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

        if (d->gpu) {
          printf("%s:", d->gpu->name);
        } else {
          printf("Unknown GPU %04x-%04xr%02x:",d->vendor_id, d->device_id, d->pcirev);
        }

        printf("%s:", d->bios_version);

        if (opt_show_memconfig) {
          printf("0x%x:", d->memconfig);
        }

        if (d->mem && d->mem->manufacturer != 0) {
          printf("%s:%s:", d->mem->name, mem_type_label[d->mem->type]);
        } else {
          printf("Unknown Memory %d-%d:%s:", d->mem_manufacturer, d->mem_model, mem_type_label[0]);
        }

        printf("%s", amd_asic_name[d->gpu->asic_type]);

        printf("\n");
      }
    // long form (original)
    } else {
      if (d->gpu) {
        printf(	"-----------------------------------\n"
          "Found Card: %04x:%04x rev %02x (AMD %s)\n"
          "Chip Type: %s\n"
          "BIOS Version: %s\n"
          "PCI: %02x:%02x.%x\n"
          "OpenCL Platform: %d\n"
          "OpenCL ID: %d\n"
          "Subvendor:  0x%04x\n"
          "Subdevice:  0x%04x\n"
          "Sysfs Path: %s\n",
          AMD_PCI_VENDOR_ID, d->gpu->device_id, d->pcirev, d->gpu->name,
          amd_asic_name[d->gpu->asic_type], d->bios_version,
          d->pcibus, d->pcidev, d->pcifunc,
          d->opencl_platform, d->opencl_id,
          d->subvendor, d->subdevice,
          d->path);

        if (opt_show_memconfig) {
          printf("Memory Configuration: 0x%x\n", d->memconfig);
        }

        printf("Memory Model: ");

        if (d->mem && d->mem->manufacturer != 0) {
          printf("%s:%s:", d->mem->name, mem_type_label[d->mem->type]);
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
    print(LOG_ERROR, "Direct PCI access failed. Run AMDGPUInfo as root to get memory type information!\n");
  }

  return 0;
}


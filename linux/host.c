/* What the kernel provided, done with Linux instead: memory, time, the clock,
 * system statistics, the PCI list and power. */

#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "host.h"

sysinfo_t g_sys;

/* ---- memory --------------------------------------------------------------------------- */

void *kmalloc(size_t n) { return malloc(n ? n : 1); }
void *kzalloc(size_t n) { return calloc(1, n ? n : 1); }
void *krealloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void kfree(void *p) { free(p); }

/* "the heap" is the machine's memory now */
static size_t mem_total, mem_used;
static uint32_t mem_read_ms;

static void read_meminfo(void)
{
    uint32_t now = timer_ms();
    if (mem_total && now - mem_read_ms < 500)
        return;
    mem_read_ms = now;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return;
    char line[128];
    unsigned long total = 0, avail = 0, v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemTotal: %lu", &v) == 1)
            total = v;
        else if (sscanf(line, "MemAvailable: %lu", &v) == 1)
            avail = v;
    }
    fclose(f);
    mem_total = (size_t)total * 1024;
    mem_used = (size_t)(total - avail) * 1024;
}

size_t heap_total(void)
{
    read_meminfo();
    return mem_total ? mem_total : 1;
}

size_t heap_used(void)
{
    read_meminfo();
    return mem_used;
}

/* ---- logging ------------------------------------------------------------------------------ */

void kprintf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stderr);
    fflush(stderr);
}

void panic(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n*** KeplerOS shell stopped: %s\n", buf);
    extern void gui_panic(const char *msg);
    gui_panic(buf);
    sleep(8);
    exit(1);
}

/* ---- time ----------------------------------------------------------------------------------- */

uint32_t timer_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static uint64_t start;
    uint64_t now = ts.tv_sec * 1000ull + ts.tv_nsec / 1000000;
    if (!start)
        start = now;
    return (uint32_t)(now - start);
}

void sleep_ms(uint32_t ms) { usleep(ms * 1000); }

void rtc_read(datetime_t *dt)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    dt->year = tm.tm_year + 1900;
    dt->month = tm.tm_mon + 1;
    dt->day = tm.tm_mday;
    dt->hour = tm.tm_hour;
    dt->minute = tm.tm_min;
    dt->second = tm.tm_sec;
    dt->weekday = tm.tm_wday;
}

uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}

/* busy share of all CPUs since the previous call */
uint32_t cpu_busy_percent(void)
{
    static unsigned long long last_total, last_idle;
    static uint32_t last_pct;
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return 0;
    unsigned long long v[8] = { 0 };
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (n < 4)
        return last_pct;
    unsigned long long total = 0, idle = v[3] + v[4];
    for (int i = 0; i < 8; i++)
        total += v[i];
    if (last_total && total > last_total)
        last_pct = (uint32_t)(100 - (idle - last_idle) * 100 / (total - last_total));
    last_total = total;
    last_idle = idle;
    return last_pct;
}

/* ---- hardware list --------------------------------------------------------------------------- */

static const char *class_name(unsigned cls)
{
    switch (cls >> 8) {
    case 0x0100: return "SCSI controller";
    case 0x0101: return "IDE controller";
    case 0x0106: return "SATA controller";
    case 0x0108: return "NVMe controller";
    case 0x0200: return "Ethernet controller";
    case 0x0280: return "Network controller";
    case 0x0300: return "VGA controller";
    case 0x0380: return "Display controller";
    case 0x0401: return "Audio device";
    case 0x0403: return "Audio device";
    case 0x0600: return "Host bridge";
    case 0x0601: return "ISA bridge";
    case 0x0604: return "PCI bridge";
    case 0x0680: return "Bridge";
    case 0x0c03: return "USB controller";
    case 0x0c05: return "SMBus controller";
    }
    switch (cls >> 16) {
    case 0x01: return "Storage controller";
    case 0x02: return "Network controller";
    case 0x03: return "Display controller";
    case 0x04: return "Multimedia device";
    case 0x06: return "Bridge";
    case 0x0c: return "Serial bus controller";
    }
    return "Device";
}

static unsigned read_hex(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "/sys/bus/pci/devices/%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    unsigned v = 0;
    if (f) {
        if (fscanf(f, "%x", &v) != 1)
            v = 0;
        fclose(f);
    }
    return v;
}

static int by_name(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }

int pci_list(char lines[][64], int max)
{
    struct dirent **list;
    int k = scandir("/sys/bus/pci/devices", &list, NULL, by_name);
    int n = 0;
    for (int i = 0; i < k; i++) {
        const char *d = list[i]->d_name;
        if (d[0] != '.' && n < max) {
            unsigned bus = 0, slot = 0, fn = 0, dom = 0;
            sscanf(d, "%x:%x:%x.%x", &dom, &bus, &slot, &fn);
            ksnprintf(lines[n++], 64, "%02x:%02x.%u  %04x:%04x  %s", bus, slot, fn, read_hex(d, "vendor"),
                      read_hex(d, "device"), class_name(read_hex(d, "class")));
        }
        free(list[i]);
    }
    if (k >= 0)
        free(list);
    return n;
}

/* ---- power ------------------------------------------------------------------------------------- */

static void run_power(const char *verb)
{
    sync();
    execlp("systemctl", "systemctl", verb, (char *)NULL);
    execlp("sudo", "sudo", "-n", "systemctl", verb, (char *)NULL);
}

void power_off(void)
{
    run_power("poweroff");
    exit(0);
}

void power_reboot(void)
{
    run_power("reboot");
    exit(0);
}

/* ---- system information ------------------------------------------------------------------------- */

void host_sysinfo_init(void)
{
    struct sysinfo si;
    if (sysinfo(&si) == 0)
        g_sys.mem_kb = (uint32_t)((unsigned long long)si.totalram * si.mem_unit / 1024);
    strlcpy(g_sys.cpu_brand, "x86-64 processor", sizeof g_sys.cpu_brand);
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        double mhz = 0;
        while (fgets(line, sizeof line, f)) {
            char *colon = strchr(line, ':');
            if (!colon)
                continue;
            char *val = colon + 1;
            while (*val == ' ')
                val++;
            val[strcspn(val, "\n")] = 0;
            if (!strncmp(line, "model name", 10) && !strcmp(g_sys.cpu_brand, "x86-64 processor"))
                strlcpy(g_sys.cpu_brand, val, sizeof g_sys.cpu_brand);
            else if (!strncmp(line, "vendor_id", 9) && !g_sys.cpu_vendor[0])
                strlcpy(g_sys.cpu_vendor, val, sizeof g_sys.cpu_vendor);
            else if (!strncmp(line, "cpu MHz", 7) && !mhz)
                mhz = atof(val);
        }
        fclose(f);
        g_sys.cpu_mhz = (uint32_t)mhz;
    }
    ksrand((uint32_t)rdtsc() ^ (uint32_t)time(NULL));
}

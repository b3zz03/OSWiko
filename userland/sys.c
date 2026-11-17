/* OSWiko - informazioni e controlli di sistema via /sys e /proc */
#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

static int read_str(const char *path, char *buf, int n)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int ok = fgets(buf, n, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

static int read_int(const char *path, int *v)
{
	char buf[32];
	if (read_str(path, buf, sizeof(buf)) < 0)
		return -1;
	return sscanf(buf, "%d", v) == 1 ? 0 : -1;
}

static int write_int(const char *path, int v)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	fprintf(f, "%d\n", v);
	fclose(f);
	return 0;
}

/* Trova la prima voce di una classe sysfs che soddisfa un filtro sul tipo. */
static int find_class(const char *cls, const char *want_type, char *out, int n)
{
	char path[512], type[32];
	DIR *d = opendir(cls);
	struct dirent *de;

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		if (want_type) {
			snprintf(path, sizeof(path), "%s/%s/type", cls, de->d_name);
			if (read_str(path, type, sizeof(type)) < 0 ||
			    strcmp(type, want_type) != 0)
				continue;
		}
		snprintf(out, n, "%s/%s", cls, de->d_name);
		closedir(d);
		return 0;
	}
	closedir(d);
	return -1;
}

int sys_battery(int *percent, int *charging)
{
	char dir[300], path[400], status[32];

	if (find_class("/sys/class/power_supply", "Battery", dir, sizeof(dir)) < 0)
		return 0;
	snprintf(path, sizeof(path), "%s/capacity", dir);
	if (read_int(path, percent) < 0)
		return 0;
	snprintf(path, sizeof(path), "%s/status", dir);
	*charging = read_str(path, status, sizeof(status)) == 0 &&
		    (!strcmp(status, "Charging") || !strcmp(status, "Full"));
	return 1;
}

static int backlight_dir(char *dir, int n)
{
	return find_class("/sys/class/backlight", NULL, dir, n);
}

int sys_backlight_get(void)
{
	char dir[300], path[400];
	int cur, max;

	if (backlight_dir(dir, sizeof(dir)) < 0)
		return -1;
	snprintf(path, sizeof(path), "%s/max_brightness", dir);
	if (read_int(path, &max) < 0 || max <= 0)
		return -1;
	snprintf(path, sizeof(path), "%s/brightness", dir);
	if (read_int(path, &cur) < 0)
		return -1;
	return cur * 100 / max;
}

void sys_backlight_set(int percent)
{
	char dir[300], path[400];
	int max;

	if (backlight_dir(dir, sizeof(dir)) < 0)
		return;
	snprintf(path, sizeof(path), "%s/max_brightness", dir);
	if (read_int(path, &max) < 0)
		return;
	if (percent < 5)
		percent = 5; /* mai completamente buio */
	snprintf(path, sizeof(path), "%s/brightness", dir);
	write_int(path, max * percent / 100);
}

void sys_screen_power(int on)
{
	char dir[300], path[400];

	if (backlight_dir(dir, sizeof(dir)) < 0)
		return;
	snprintf(path, sizeof(path), "%s/bl_power", dir);
	write_int(path, on ? 0 : 4); /* FB_BLANK_UNBLANK / FB_BLANK_POWERDOWN */
}

void sys_model(char *buf, int n)
{
	if (read_str("/sys/firmware/devicetree/base/model", buf, n) < 0)
		snprintf(buf, n, "Sconosciuto");
	if (!strcmp(buf, "linux,dummy-virt"))
		snprintf(buf, n, "Emulatore QEMU");
}

void sys_kernel(char *buf, int n)
{
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(buf, n, "Linux %s", u.release);
	else
		snprintf(buf, n, "?");
}

void sys_memory(long *total_mb, long *free_mb)
{
	FILE *f = fopen("/proc/meminfo", "r");
	char line[128];
	long v;

	*total_mb = *free_mb = 0;
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "MemTotal: %ld kB", &v) == 1)
			*total_mb = v / 1024;
		else if (sscanf(line, "MemAvailable: %ld kB", &v) == 1)
			*free_mb = v / 1024;
	}
	fclose(f);
}

long sys_uptime(void)
{
	struct sysinfo si;
	return sysinfo(&si) == 0 ? si.uptime : 0;
}

int sys_cpus(void)
{
	return (int)sysconf(_SC_NPROCESSORS_ONLN);
}

/* ---- piccole impostazioni permanenti (record dei giochi, ecc.) ---- */

int sys_load_int(const char *key, int def)
{
	char path[256];
	int v;
	snprintf(path, sizeof(path), "/data/settings/%s", key);
	return read_int(path, &v) == 0 ? v : def;
}

void sys_save_int(const char *key, int v)
{
	char path[256];
	mkdir("/data/settings", 0755);
	snprintf(path, sizeof(path), "/data/settings/%s", key);
	write_int(path, v);
}

int sys_data_volatile(void)
{
	return access("/run/data-volatile", F_OK) == 0;
}

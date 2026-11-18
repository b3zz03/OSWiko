/* OSWiko - informazioni e controlli di sistema via /sys e /proc */
#include "ui.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
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

/* ---- rete: accendi/spegni le interfacce (Wi-Fi, modalità aereo) ---- */

static int iface_flags(int sk, const char *name, int set, int clear)
{
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
	if (ioctl(sk, SIOCGIFFLAGS, &ifr) < 0)
		return -1;
	if (set || clear) {
		ifr.ifr_flags = (ifr.ifr_flags | set) & ~clear;
		if (ioctl(sk, SIOCSIFFLAGS, &ifr) < 0)
			return -1;
	}
	return ifr.ifr_flags;
}

/* Gateway predefinito dell'interfaccia (da /proc/net/route), 0 se nessuno. */
static unsigned default_gw(const char *name)
{
	FILE *f = fopen("/proc/net/route", "r");
	char line[256], ifn[32];
	unsigned dst, gw;
	unsigned found = 0;
	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, "%31s %x %x", ifn, &dst, &gw) == 3 && dst == 0 &&
		    !strcmp(ifn, name))
			found = gw;
	fclose(f);
	return found;
}

void sys_net_set(int up)
{
	DIR *d = opendir("/sys/class/net");
	struct dirent *de;
	int sk = socket(AF_INET, SOCK_DGRAM, 0);
	if (!d || sk < 0)
		goto out;
	while ((de = readdir(d))) {
		const char *n = de->d_name;
		char path[128];
		if (n[0] == '.' || !strcmp(n, "lo"))
			continue;
		snprintf(path, sizeof(path), "/run/gw-%.32s", n);
		if (!up) {
			/* spegnere l'interfaccia cancella il percorso verso internet:
			 * ricordiamo il gateway per rimetterlo alla riaccensione */
			unsigned gw = default_gw(n);
			if (gw) {
				FILE *f = fopen(path, "w");
				if (f) { fprintf(f, "%u\n", gw); fclose(f); }
			}
			iface_flags(sk, n, 0, IFF_UP);
		} else {
			iface_flags(sk, n, IFF_UP, 0);
			FILE *f = fopen(path, "r");
			unsigned gw = 0;
			if (f) {
				if (fscanf(f, "%u", &gw) != 1)
					gw = 0;
				fclose(f);
			}
			if (gw && !default_gw(n)) {
				struct rtentry rt;
				memset(&rt, 0, sizeof(rt));
				struct sockaddr_in *a = (struct sockaddr_in *)&rt.rt_gateway;
				a->sin_family = AF_INET;
				a->sin_addr.s_addr = gw;
				((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
				((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
				rt.rt_flags = RTF_UP | RTF_GATEWAY;
				rt.rt_dev = (char *)n;
				for (int i = 0; i < 20 && ioctl(sk, SIOCADDRT, &rt) < 0; i++)
					usleep(50000); /* il collegamento può impiegare un attimo */
			}
		}
	}
out:
	if (d) closedir(d);
	if (sk >= 0) close(sk);
}

/* 1 se almeno un'interfaccia (non lo) è accesa e collegata. */
int sys_net_up(char *name, int n)
{
	DIR *d = opendir("/sys/class/net");
	struct dirent *de;
	int sk = socket(AF_INET, SOCK_DGRAM, 0), up = 0;
	if (name)
		snprintf(name, n, "Nessuna rete");
	while (d && sk >= 0 && (de = readdir(d))) {
		if (de->d_name[0] == '.' || !strcmp(de->d_name, "lo"))
			continue;
		int fl = iface_flags(sk, de->d_name, 0, 0);
		if (fl >= 0 && (fl & IFF_UP) && (fl & IFF_RUNNING)) {
			up = 1;
			if (name) /* nell'emulatore la rete è un "cavo" virtuale */
				snprintf(name, n, "%s", !strncmp(de->d_name, "wlan", 4) ? "Wi-Fi" : "Emulatore");
		}
	}
	if (d) closedir(d);
	if (sk >= 0) close(sk);
	return up;
}

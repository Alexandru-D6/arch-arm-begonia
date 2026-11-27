// SPDX-License-Identifier: GPL-2.0-or-later
/* Android property and logging adapter, loaded only by the stock GPS runtime. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct prop_info {
	const char *name;
	const char *value;
};

static const struct prop_info properties[] = {
	{"persist.vendor.connsys.chipid", "0x6785"},
	{"vendor.connsys.driver.ready", "yes"},
	{"vendor.service.nvram_init", "Ready"},
	{"ro.hardware", "mt6785"},
	{"ro.boot.hardware", "mt6785"},
	{"ro.vendor.mtk_gps_support", "1"},
};

const struct prop_info *__system_property_find(const char *key)
{
	for (unsigned i = 0; i < sizeof(properties) / sizeof(properties[0]); i++)
		if (!strcmp(key, properties[i].name))
			return &properties[i];
	return NULL;
}

void __system_property_read_callback(const struct prop_info *property,
		void (*callback)(void *, const char *, const char *, uint32_t), void *cookie)
{
	callback(cookie, property->name, property->value, strlen(property->value) << 24);
}

int property_get(const char *key, char *value, const char *fallback)
{
	const char *result = fallback ? fallback : "";
	const struct prop_info *property = __system_property_find(key);
	if (property)
		result = property->value;
	/* Android's PROP_VALUE_MAX includes the terminating NUL. */
	int n = snprintf(value, 92, "%s", result);
	return n < 0 ? 0 : n > 91 ? 91 : n;
}

int __system_property_get(const char *key, char *value)
{
	return property_get(key, value, "");
}

int __system_property_read(const struct prop_info *property, char *name, char *value)
{
	if (name)
		snprintf(name, 32, "%s", property->name);
	return snprintf(value, 92, "%s", property->value);
}

uint32_t __system_property_serial(const struct prop_info *property)
{
	return strlen(property->value) << 24;
}

int property_set(const char *key, const char *value)
{
	(void)key;
	(void)value;
	return 0;
}

int __android_log_buf_write(int buffer, int priority, const char *tag,
			    const char *message)
{
	(void)buffer;
	int debug = getenv("BEGONIA_GPS_DEBUG") != NULL;
	/* Standalone GNSS has no Android assistance daemon. Retain those messages
	 * in debug mode without writing the same expected failure every second.
	 */
	if (!debug && ((tag && !strcmp(tag, "MNL2AGPS")) ||
	    strstr(message, "mnl2agps") || strstr(message, "unknown sentence")))
		return 1;
	if (priority >= 5 || debug || strstr(message, "GPS NVRam") ||
	    strstr(message, "calibration read from AP NVRAM") ||
	    strstr(message, "GPS coclock"))
		fprintf(stderr, "%s: %s\n", tag ? tag : "gps", message);
	return 1;
}

int __android_log_write(int priority, const char *tag, const char *message)
{
	return __android_log_buf_write(0, priority, tag, message);
}

int __android_log_buf_vprint(int buffer, int priority, const char *tag,
			     const char *format, va_list args)
{
	char message[2048];
	vsnprintf(message, sizeof(message), format, args);
	return __android_log_buf_write(buffer, priority, tag, message);
}

int __android_log_vprint(int priority, const char *tag, const char *format,
			va_list args)
{
	return __android_log_buf_vprint(0, priority, tag, format, args);
}

int __android_log_print(int priority, const char *tag, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int ret = __android_log_vprint(priority, tag, format, args);
	va_end(args);
	return ret;
}

int __android_log_buf_print(int buffer, int priority, const char *tag,
			    const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int ret = __android_log_buf_vprint(buffer, priority, tag, format, args);
	va_end(args);
	return ret;
}

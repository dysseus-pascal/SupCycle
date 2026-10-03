// Nur fuer die Host-Tests in tools/: so viel vom Pebble-SDK, wie plan.c und
// cycle.c brauchen. Persist liegt im Speicher, die Uhrzeit stellt der Test.
//
// WARUM NICHT IM EMULATOR: dort laesst sich die Zeit nicht halten (jeder
// pebble-Befehl stellt sie neu), und ein Neustart der App mitten im Tag ist
// muehsam nachzustellen. Hier ist beides eine Zeile.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define APP_LOG_LEVEL_INFO 0
#define APP_LOG_LEVEL_WARNING 1
#define APP_LOG(level, fmt, ...) ((void)(level))

// Die Uhrzeit der Uhr - der Test setzt sie.
extern time_t stub_jetzt;
static inline time_t stub_time(time_t *t) {
  if (t) *t = stub_jetzt;
  return stub_jetzt;
}
#define time(t) stub_time(t)

bool persist_exists(uint32_t key);
int persist_read_data(uint32_t key, void *buf, size_t size);
int32_t persist_read_int(uint32_t key);
int persist_write_data(uint32_t key, const void *data, size_t size);
int persist_write_int(uint32_t key, int32_t value);
int persist_delete(uint32_t key);

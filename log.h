#pragma once

#define dfLOG_LEVEL_DEBUG 0
#define dfLOG_LEVEL_SYSTEM 1
#define dfLOG_LEVEL_ERROR 2
#define dfMAX_STRING 256

extern INT64 g_logCount;
extern int g_logLevel;
extern CRITICAL_SECTION g_log_CS;

void logInit();
void systemLog(LPCWSTR String, int LogLevel, LPCWSTR StringFormat, ...);
#pragma once

#define PROFILE

#ifdef PROFILE
#define PRO_BEGIN(TagName)	ProfileBegin(TagName)
#define PRO_END(TagName)	ProfileEnd(TagName)
#define PRO_INIT()  ProfileInit()
#define PRO_LOG() ProfileLog()

#else
#define PRO_BEGIN(TagName)
#define PRO_END(TagName)
#define PRO_INIT()
#define PRO_LOG()
#endif

#define MAX_PROFILE_THREAD 100

void ProfileInit();
void ProfileReset();
void ProfileBegin(const char* TagName);
void ProfileEnd(const char* TagName);
void ProfileLog();

class CProfiler
{
public:
	CProfiler(const char* tag)
	{
		_tag = tag;
		PRO_BEGIN(_tag);
	}

	~CProfiler()
	{
		PRO_END(_tag);
	}

	const char* _tag;
};


enum { MAX_ARRAY = 50, MICRO_SECOND = 100000 };

extern LARGE_INTEGER g_Freq;

struct PROFILER
{
	bool Flag = false;
	char Name[64];

	LARGE_INTEGER StartTime;

	__int64 TotalTime; // 10회이상 측정시에는 평균계산시 최대,최소 제외. 10회 이하는 최대 최소 포함 
	__int64 Min;
	__int64 Max;

	__int64 Call;

	int isError = 0;
};

struct ThreadProfilelist
{
	PROFILER Profilelist[MAX_ARRAY];
	DWORD threadID;
};

extern ThreadProfilelist g_th_Profilelist[MAX_PROFILE_THREAD];
extern DWORD TLS_ProfilerAddress;
extern DWORD TLS_ProfilerIndex;
extern volatile LONG threadNumber;
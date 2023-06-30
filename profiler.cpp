#include <windows.h>
#include "profiler.h"
#include <algorithm>
#include <iostream>

LARGE_INTEGER g_Freq;
DWORD TLS_ProfilerAddress;
DWORD TLS_ProfilerIndex;
volatile LONG threadNumber = -1;

ThreadProfilelist g_th_Profilelist[MAX_PROFILE_THREAD];

void ProfileInit()
{
	QueryPerformanceFrequency(&g_Freq);
	TLS_ProfilerAddress = TlsAlloc();
	TLS_ProfilerIndex = TlsAlloc();
	for (int i = 0; i < MAX_PROFILE_THREAD; i++)
	{
		g_th_Profilelist[i].threadID = 0;
		for (int j = 0; j < MAX_ARRAY; j++)
		{
			g_th_Profilelist[i].Profilelist[j].StartTime.QuadPart = 0;
			g_th_Profilelist[i].Profilelist[j].Min = 0xFFFFFFFF;
			g_th_Profilelist[i].Profilelist[j].Max = 0;
			g_th_Profilelist[i].Profilelist[j].Call = 0;
			g_th_Profilelist[i].Profilelist[j].TotalTime = 0;
		}
	}
}

void ProfileReset()
{
	PROFILER* p = (PROFILER*)TlsGetValue(TLS_ProfilerAddress);
	int* pIndex = (int*)TlsGetValue(TLS_ProfilerIndex);
	ThreadProfilelist& ProfileSample = g_th_Profilelist[*pIndex];

	for (int i = 0; i < MAX_ARRAY; i++)
	{
		p[i].Flag = FALSE;
		p[i].isError = 0;
		p[i].StartTime.QuadPart = 0;
		p[i].Min = 0xFFFFFFFF;
		p[i].Max = 0;
		p[i].Call = 0;
		p[i].TotalTime = 0;

		ProfileSample.Profilelist[i].Flag = FALSE;
		ProfileSample.Profilelist[i].isError = 0;
		ProfileSample.Profilelist[i].StartTime.QuadPart = 0;
		ProfileSample.Profilelist[i].Min = 0xFFFFFFFF;
		ProfileSample.Profilelist[i].Max = 0;
		ProfileSample.Profilelist[i].Call = 0;
		ProfileSample.Profilelist[i].TotalTime = 0;
	}
}

void ProfileBegin(const char* TagName)
{
	PROFILER* p;
	int* pIndex;
	p = (PROFILER*)TlsGetValue(TLS_ProfilerAddress);
	if (p == NULL)
	{
		p = new PROFILER[MAX_ARRAY];
		for (int i = 0; i < MAX_ARRAY; i++)
		{
			p[i].Flag = FALSE;
			p[i].isError = 0;
			p[i].StartTime.QuadPart = 0;
			p[i].Min = 0xFFFFFFFF;
			p[i].Max = 0;
			p[i].Call = 0;
			p[i].TotalTime = 0;
		}
		TlsSetValue(TLS_ProfilerAddress, p);

		pIndex = new int;
		*pIndex = InterlockedIncrement(&threadNumber);
		if (*pIndex >= MAX_PROFILE_THREAD)
		{
			//CRASH();
		}
		TlsSetValue(TLS_ProfilerIndex, pIndex);
		g_th_Profilelist[*pIndex].threadID = GetCurrentThreadId();
	}
	pIndex = (int*)TlsGetValue(TLS_ProfilerIndex);

	for (int i = 0; i < MAX_ARRAY; i++) //태그가 저장된 인덱스 위치가 있을경우
	{
		if (p[i].Flag == TRUE && strcmp(p[i].Name, TagName) == 0)
		{
			if (p[i].StartTime.QuadPart != 0)
			{
				p[i].isError = 1;
				//CRASH()
			}
			QueryPerformanceCounter(&p[i].StartTime);
			return;
		}
	}

	for (int i = 0; i < MAX_ARRAY; i++) //처음일경우 빈곳에 할당
	{
		if (p[i].Flag == false)
		{
			p[i].Flag = true;
			g_th_Profilelist[*pIndex].Profilelist[i].Flag = true;
			strcpy_s(p[i].Name, 64, TagName);
			strcpy_s(g_th_Profilelist[*pIndex].Profilelist[i].Name, 64, TagName);
			QueryPerformanceCounter(&p[i].StartTime);
			return;
		}
	}
}

void ProfileEnd(const char* TagName)
{
	PROFILER* p;
	p = (PROFILER*)TlsGetValue(TLS_ProfilerAddress);

	int* pIndex;
	pIndex = (int*)TlsGetValue(TLS_ProfilerIndex);

	if (p == NULL)
	{
		//CRASH();
	}
	for (int i = 0; i < MAX_ARRAY; i++)
	{
		if (p[i].Flag == true && strcmp(p[i].Name, TagName) == 0)
		{
			LARGE_INTEGER EndTime;
			__int64 TimePeriod;
			QueryPerformanceCounter(&EndTime);

			TimePeriod = EndTime.QuadPart - p[i].StartTime.QuadPart;
			p[i].TotalTime += TimePeriod;

			p[i].Min = min(TimePeriod, p[i].Min);
			p[i].Max = max(TimePeriod, p[i].Max);

			p[i].Call++;
			p[i].StartTime.QuadPart = 0;

			g_th_Profilelist[*pIndex].Profilelist[i].TotalTime = p[i].TotalTime;
			g_th_Profilelist[*pIndex].Profilelist[i].Min = p[i].Min;
			g_th_Profilelist[*pIndex].Profilelist[i].Max = p[i].Max;
			g_th_Profilelist[*pIndex].Profilelist[i].Call = p[i].Call;

			return;
		}
	}

}

void ProfileLog()
{
	FILE* fp;
	if (!fopen_s(&fp, "PRO_filelog", "at"))
	{

		for (int idx = 0; idx < MAX_PROFILE_THREAD; idx++)
		{
			ThreadProfilelist& ProfileSample = g_th_Profilelist[idx];
			if (ProfileSample.threadID == 0)
			{
				continue;
			}
			fprintf_s(fp, " thread ID : %d\n", ProfileSample.threadID);
			fprintf_s(fp, "-------------------------------------------------------------------------------------------------------\n");
			fprintf_s(fp, "|            Name           |    Average(us)     |      Min(us)     |       Max(us)      |         Call       |\n");
			fprintf_s(fp, "-------------------------------------------------------------------------------------------------------\n");
			for (int i = 0; i < MAX_ARRAY; i++)
			{
				if (ProfileSample.Profilelist[i].Flag == true)
				{

					int size = strlen(ProfileSample.Profilelist[i].Name);
					fprintf_s(fp, "|");
					for (int i = 0; i < 23 - size; i++)
					{
						fprintf_s(fp, " ");
					}
					fprintf_s(fp, "%s ", ProfileSample.Profilelist[i].Name);
					fprintf_s(fp, " |");
					if (ProfileSample.Profilelist[i].Call < 10)
					{
						fprintf_s(fp, "       %.4f        ", (double)(ProfileSample.Profilelist[i].TotalTime * MICRO_SECOND) / (double)(ProfileSample.Profilelist[i].Call * g_Freq.QuadPart));
					}
					else
					{
						fprintf_s(fp, "       %.4f        ", ((double)(ProfileSample.Profilelist[i].TotalTime * MICRO_SECOND) - (double)(ProfileSample.Profilelist[i].Max * MICRO_SECOND) - (double)(ProfileSample.Profilelist[i].Min * MICRO_SECOND)) / (double)((ProfileSample.Profilelist[i].Call - 2) * g_Freq.QuadPart));
					}
					fprintf_s(fp, "|");
					fprintf_s(fp, "      %.4f     ", (double)(ProfileSample.Profilelist[i].Min * MICRO_SECOND) / double(g_Freq.QuadPart));
					fprintf_s(fp, " |");
					fprintf_s(fp, "       %.4f       ", (double)(ProfileSample.Profilelist[i].Max * MICRO_SECOND) / double(g_Freq.QuadPart));
					fprintf_s(fp, "|");
					__int64 callNum;
					if (ProfileSample.Profilelist[i].Call < 10)
					{
						callNum = ProfileSample.Profilelist[i].Call;
					}
					else
					{
						callNum = ProfileSample.Profilelist[i].Call;
						//callNum = ProfileSample.Profilelist[i].Call-2;
					}
					__int64 backupNum = callNum;
					int len = 0;
					while (callNum > 0)
					{
						len++;
						callNum = callNum / 10;
					}
					for (int i = 0; i < 15 - len; i++)
					{
						fprintf_s(fp, " ");
					}
					fprintf_s(fp, "%lld    |\n", backupNum);
				}
			}
			fprintf_s(fp, "-------------------------------------------------------------------------------------------------------\n");
			fprintf_s(fp, "\n");
		}
		fclose(fp);
	}
}
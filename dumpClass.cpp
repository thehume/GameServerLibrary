#pragma warning(push)
#pragma warning(disable: 4091)
#pragma warning(pop)
#pragma comment(lib, "DbgHelp.Lib")
#include <Windows.h>
#include <iostream>
#include <dbghelp.h>
#include <crtdbg.h>
#include "dumpClass.h"

CrashDump::CrashDump()
{
	DumpCount = 0;

	_invalid_parameter_handler oldHandler, newHandler;
	newHandler = myInvalidParameterHandler;

	oldHandler = _set_invalid_parameter_handler(newHandler);
	_CrtSetReportMode(_CRT_WARN, 0);
	_CrtSetReportMode(_CRT_ASSERT, 0);
	_CrtSetReportMode(_CRT_ERROR, 0);
	_CrtSetReportHook(_custom_Report_hook);

	_set_purecall_handler(myPurecallHandler);

	SetHandlerDump();
}
void CrashDump::Crash(void)
{
	int* p = nullptr;
	*p = 0;
}


LONG WINAPI CrashDump::MyExceptionFilter(__in PEXCEPTION_POINTERS pExceptionPointer)
{
	SYSTEMTIME NowTime;
	long tempDumpCount = InterlockedIncrement(&DumpCount);

	WCHAR filename[MAX_PATH];

	GetLocalTime(&NowTime);
	wsprintf(filename, L"Dump_%d%02d%02d_%02d.%02d.%02d_%d.dmp",
		NowTime.wYear, NowTime.wMonth, NowTime.wDay, NowTime.wHour, NowTime.wMinute, NowTime.wSecond, tempDumpCount);
	wprintf(L"\n\n\n!!! Crash Error !!! %d. %d. %d / %d:%d:%d \n",
		NowTime.wYear, NowTime.wMonth, NowTime.wDay, NowTime.wHour, NowTime.wMinute, NowTime.wSecond);
	wprintf(L"Now Save dump file...\n");

	HANDLE hDumpFile = ::CreateFile(filename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hDumpFile != INVALID_HANDLE_VALUE)
	{
		_MINIDUMP_EXCEPTION_INFORMATION MinidumpExceptionInformation;

		MinidumpExceptionInformation.ThreadId = ::GetCurrentThreadId();
		MinidumpExceptionInformation.ExceptionPointers = pExceptionPointer;
		MinidumpExceptionInformation.ClientPointers = TRUE;

		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpWithFullMemory, &MinidumpExceptionInformation, NULL, NULL);
		CloseHandle(hDumpFile);
		wprintf(L"CrashDump Save Finish!");
	}
	return EXCEPTION_EXECUTE_HANDLER;
}
void CrashDump::SetHandlerDump()
{
	SetUnhandledExceptionFilter(MyExceptionFilter);
}
void CrashDump::myInvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t pReserved)
{
	Crash();
}
int CrashDump::_custom_Report_hook(int ireposttype, char* message, int* returnvalue)
{
	Crash();
	return true;
}
void CrashDump::myPurecallHandler(void)
{
	Crash();
}

long CrashDump::DumpCount;
#pragma comment(lib, "winmm.lib" )
#pragma comment(lib, "ws2_32")
#pragma comment(lib,"Pdh.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <dbghelp.h>
#include <list>
#include <locale.h>
#include <random>
#include <process.h>
#include <stdlib.h>
#include <iostream>
#include <Pdh.h>
#include <strsafe.h>
#include <unordered_map>
#include <conio.h>
#include "log.h"
#include "ringbuffer.h"
#include "MemoryPoolBucket.h"
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CNetServer.h"
#include "CommonProtocol.h"
//#include "ChatServer.h"
//#include "HardwareMonitor.h"
//#include "ProcessMonitor.h"

using namespace std;

CrashDump myDump;

WCHAR IPaddress[20] = L"0.0.0.0";
CInitParam initParam(IPaddress, 6000, 3, 3, true, 20000);
CNetServer NetServer(&initParam);
//CChatServer ChatServer;

//CHardwareMonitor Hardware_Monitor;
//CProcessMonitor Process_Monitor(GetCurrentProcess());


int main()
{
	volatile bool g_ShutDown = false;
	logInit();

	CContentsHandler HandleInstance;
	HandleInstance.attachServerInstance(&NetServer, &ChatServer);

	NetServer.attachHandler(&HandleInstance);
	ChatServer.attachServerInstance(&NetServer);

	if (ChatServer.Start() == false)
	{
		wprintf(L"ChatServer Thread init error");
		systemLog(L"Start Error", dfLOG_LEVEL_ERROR, L"ChatServer Thread init Error");
		return false;
	}

	if (NetServer.Start() == false)
	{
		systemLog(L"Start Error", dfLOG_LEVEL_ERROR, L"NetServer Init Error, ErrorNo : %u, ErrorCode : %d", NetServer.InitErrorNum, NetServer.InitErrorCode);
		return false;
	}

	return 0;
}
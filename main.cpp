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
#include <queue>
#include <Pdh.h>
#include <strsafe.h>
#include <unordered_map>
#include <conio.h>
#include "log.h"
#include "ringbuffer.h"
#include "MemoryPoolBucket.h"
#include "CommonProtocol.h"
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CNetServer.h"
#include "CThreadManager.h"
#include "GameServer.h"
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

	//넷서버 초기화
	CNetServerHandler NetServer_HandleInstance;
	NetServer_HandleInstance.attachServerInstance(&NetServer);
	NetServer.attachHandler(&NetServer_HandleInstance);


	//스레드 핸들러, 스레드 매니저 생성
	MyThreadHandler handler_Instance;
	CThreadManager Manager_Instance;
	Manager_Instance.attachHandler(&handler_Instance);


	//게임 스레드, 로그인 스레드 생성 및 등록
	GameThread GameThread(50, df_THREAD_CONTENTS);
	GameLoginThread LoginThread(50, df_THREAD_LOGIN);

	Manager_Instance.registThread(df_THREAD_LOGIN, &LoginThread, true);
	Manager_Instance.registThread(df_THREAD_CONTENTS, &GameThread, false);


	//매니저에 서버 붙임. 이걸 마지막에 해줘야함
	Manager_Instance.attachServerInstance(&NetServer);

	
	//네트워크 엔진부 가동
	if (NetServer.Start() == false)
	{
		systemLog(L"Start Error", dfLOG_LEVEL_ERROR, L"NetServer Init Error, ErrorNo : %u, ErrorCode : %d", NetServer.InitErrorNum, NetServer.InitErrorCode);
		return false;
	}


	//게임서버 가동
	Manager_Instance.start();
	
	while (!g_ShutDown)
	{
		if (_kbhit())
		{
			WCHAR ControlKey = _getwch();
			if (L'q' == ControlKey || L'Q' == ControlKey)
			{
				g_ShutDown = true;
			}
		}

		wprintf(L"======================\n");
		wprintf(L"session number : %d\n", NetServer.getSessionCount());
		wprintf(L"PlayerPool UseSize : %d\n", CParentThread::PlayerPoolSize()* POOL_BUCKET_SIZE);
		wprintf(L"JobPool UseSize : %d\n", NetServer.getJobPoolSize()* POOL_BUCKET_SIZE);
		wprintf(L"Accept Sum : %lld\n", NetServer.getAcceptSum());
		wprintf(L"Accept TPS : %d\n", NetServer.getAcceptTPS());
		wprintf(L"Disconnect TPS : %d\n", NetServer.getDisconnectTPS());
		wprintf(L"Send TPS : %d\n", NetServer.getSendMessageTPS());
		wprintf(L"Recv TPS : %d\n", NetServer.getRecvMessageTPS());
		wprintf(L"Login TPS : %d\n", Manager_Instance.LoginTPS);
		wprintf(L"ThreadMove TPS : %d\n", Manager_Instance.ThreadMoveTPS);
		wprintf(L"LoginThread TPS : %d\n", Manager_Instance.LoginThread_JobTPS);
		wprintf(L"GameThread TPS : %d\n", Manager_Instance.GameThread_JobTPS);
		wprintf(L"PacketPool UseSize : %d\n", CPacket::getPoolUseSize() * POOL_BUCKET_SIZE);
		Manager_Instance.updateMonitor();
		wprintf(L"======================\n");
		wprintf(L"GameThread Frame : %d\n", GameThread.getFrame());
		wprintf(L"GameThread PlayerSize : %d\n", GameThread.getPlayerSize());
		wprintf(L"GameThread EnterUserTPS : %d\n", GameThread.getEnterUserTPS());
		wprintf(L"GameThread EnterQueue Size : %d\n", GameThread.getEnterQueueSize());
		GameThread.UpdateTPS();
		wprintf(L"======================\n");
		wprintf(L"LoginThread Frame : %d\n", LoginThread.getFrame());
		wprintf(L"LoginThread PlayerSize : %d\n", LoginThread.getPlayerSize());
		LoginThread.UpdateTPS();
		wprintf(L"======================\n");
		Sleep(1000);
	}



	return 0;
}
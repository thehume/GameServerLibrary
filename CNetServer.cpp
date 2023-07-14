#pragma comment(lib, "winmm.lib" )
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <dbghelp.h>
#include <random>
#include <locale.h>
#include <process.h>
#include <stdlib.h>
#include <iostream>
#include <queue>
#include <unordered_map>
#include "log.h"
#include "ringbuffer.h"
#include "MemoryPoolBucket.h"
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CNetServer.h"


CNetServer::CNetServer(CInitParam* pParam)
{
	wcscpy_s(openIP, pParam->openIP);
	openPort = pParam->openPort;
	maxThreadNum = pParam->maxThreadNum;
	concurrentThreadNum = pParam->concurrentThreadNum;
	Nagle = pParam->Nagle;
	maxSession = pParam->maxSession;

	InitFlag = FALSE;
	InitErrorNum = 0;
	InitErrorCode = 0;
	Shutdown = FALSE;
}

CNetServer::~CNetServer()
{
	Stop();
}

bool CNetServer::Start()
{
	_wsetlocale(LC_ALL, L"korean");
	timeBeginPeriod(1);
	for (int i = 0; i < maxSession; i++)
	{
		sessionList[i].releaseFlag = DELFLAG_OFF;
		sessionList[i].isValid = FALSE;
		sessionList[i].recvCount = 0;
		sessionList[i].sendCount = 0;
		sessionList[i].disconnectCount = 0;
		sessionList[i].IOcount = 0;
		sessionList[i].sendPacketCount = 0;
		sessionList[i].disconnectStep = SESSION_NORMAL_STATE;
	}

	for (int i = 0; i < maxSession; i++)
	{
		emptyIndexStack.push(i);
	}

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		InitErrorCode = WSAGetLastError();
		wprintf(L"Error code : %u\n", InitErrorCode);
		InitErrorNum = 1;
		return false;
	}
	wprintf(L"WSAStartup #\n");

	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, concurrentThreadNum);
	if (hcp == NULL)
	{
		wprintf(L"Create IOCP error\n");
		InitErrorNum = 2;
		return false;
	}
	wprintf(L"Create CompletionPort OK #\n");

	//socket()
	listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (listenSock == INVALID_SOCKET)
	{
		InitErrorCode = WSAGetLastError();
		wprintf(L"\nError code : %u", InitErrorCode);
		InitErrorNum = 3;
		return false;
	}
	wprintf(L"SOCKET() ok #\n");

	//bind
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	InetPtonW(AF_INET, openIP, &serveraddr.sin_addr.s_addr);

	serveraddr.sin_port = htons(openPort);
	int ret_bind = bind(listenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (ret_bind == SOCKET_ERROR)
	{
		InitErrorCode = WSAGetLastError();
		wprintf(L"\nError code : %u", InitErrorCode);
		InitErrorNum = 4;
		return false;
	}

	//listen()
	int ret_listen = listen(listenSock, SOMAXCONN);
	if (ret_listen == SOCKET_ERROR)
	{
		InitErrorCode = WSAGetLastError();
		wprintf(L"\nError code : %u", InitErrorCode);
		InitErrorNum = 5;
		return false;
	}


	//Nagle 
	if (Nagle == false)
	{
		int opt_val = TRUE;
		int ret_nagle = setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt_val, sizeof(opt_val));
		if (ret_nagle == SOCKET_ERROR)
		{
			InitErrorCode = WSAGetLastError();
			wprintf(L"\nError code : %u", InitErrorCode);
			InitErrorNum = 6;
			return false;
		}
	}

	//linger
	LINGER optval;
	optval.l_onoff = 1;
	optval.l_linger = 0;
	int ret_linger = setsockopt(listenSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
	if (ret_linger == SOCKET_ERROR)
	{
		InitErrorCode = WSAGetLastError();
		wprintf(L"\nError code : %u", InitErrorCode);
		InitErrorNum = 7;
		return false;
	}
	wprintf(L"linger option OK\n");

	for (int i = 0; i < maxThreadNum; i++)
	{
		hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&WorkerThread, this, 0, 0);
		if (hWorkerThread[i] == NULL)
		{
			wprintf(L"Create workerThread error\n");
			InitErrorNum = 8;
			return false;
		}
	}

	hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&AcceptThread, this, 0, 0);
	if (hAcceptThread == NULL)
	{
		wprintf(L"AcceptThread init error");
		InitErrorNum = 9;
		return false;
	}

	//컨트롤스레드생성
	hControlThread = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&ControlThread, this, 0, 0);
	if (hControlThread == NULL)
	{
		wprintf(L"ControlThread init error");
		InitErrorNum = 10;
		return false;
	}

	return true;
}

void CNetServer::Stop()
{
	Shutdown = TRUE;

	for (int i = 0; i < maxThreadNum; i++)
	{
		PostQueuedCompletionStatus(hcp, 0, 0, 0);
	}

	WaitForMultipleObjects(maxThreadNum, hWorkerThread, true, INFINITE);
	closesocket(listenSock);
	WaitForSingleObject(hAcceptThread, INFINITE);
	WaitForSingleObject(hControlThread, INFINITE);
	WSACleanup();
}

void CNetServer::recvPost(st_Session* pSession)
{
	if (pSession->disconnectStep == SESSION_DISCONNECT)
	{
		return;
	}

	WSABUF wsaRecvbuf[2];
	wsaRecvbuf[0].buf = pSession->recvQueue.GetRearBufferPtr();
	wsaRecvbuf[0].len = pSession->recvQueue.DirectEnqueueSize();
	wsaRecvbuf[1].buf = pSession->recvQueue.GetBeginPtr();
	int FreeSize = pSession->recvQueue.GetFreeSize();
	int EnqueueSize = pSession->recvQueue.DirectEnqueueSize();
	if (EnqueueSize < FreeSize)
	{
		wsaRecvbuf[1].len = FreeSize - EnqueueSize;
	}
	else
	{
		wsaRecvbuf[1].len = 0;
	}

	ZeroMemory(&pSession->RecvOverlapped, sizeof(WSAOVERLAPPED));

	InterlockedIncrement(&pSession->IOcount);

	DWORD flags_recv = 0;
	int ret_recv = WSARecv(pSession->sock, wsaRecvbuf, 2, NULL, &flags_recv, (LPWSAOVERLAPPED)&pSession->RecvOverlapped, NULL);
	if (ret_recv == SOCKET_ERROR)
	{
		DWORD ErrorCode = WSAGetLastError();
		switch (ErrorCode)
		{
		case WSA_IO_PENDING:
			break;
		case 10004:
		case 10053:
		case 10054:
			InterlockedDecrement(&pSession->IOcount);
			break;
		default:
			InterlockedDecrement(&pSession->IOcount);
			systemLog(L"WSARECV EXCEPTION", dfLOG_LEVEL_SYSTEM, L"ErrorCode : %d", ErrorCode);
			break;
		}
	}

	if (pSession->disconnectStep == SESSION_DISCONNECT)
	{
		CancelIoEx((HANDLE)pSession->sock, (LPOVERLAPPED)&pSession->RecvOverlapped);
	}
}

void CNetServer::sendPost(st_Session* pSession)
{
	if (_InterlockedExchange(&pSession->sendFlag, 1) == 0)
	{
		if (pSession->disconnectStep == SESSION_DISCONNECT)
		{
			_InterlockedExchange(&pSession->sendFlag, 0);
			return;
		}

		WSABUF wsaSendbuf[dfMAX_PACKET];

		if (pSession->sendQueue.nodeCount <= 0)
		{
			_InterlockedExchange(&pSession->sendFlag, 0);
			return;
		}

		CPacket* bufferPtr;
		int packetCount = 0;
		while (packetCount < dfMAX_PACKET)
		{
			if (pSession->sendQueue.Dequeue(&bufferPtr) == false)
			{
				break;
			}

			wsaSendbuf[packetCount].buf = bufferPtr->GetReadBufferPtr();
			wsaSendbuf[packetCount].len = bufferPtr->GetDataSize();

			pSession->sentPacketArray[packetCount] = bufferPtr;

			packetCount++;
		}

		if (packetCount == dfMAX_PACKET)
		{
			//로그 찍어야하는 상황 한번에 보낼수있는 한계를 넘어섬
			systemLog(L"NetServer Exception", dfLOG_LEVEL_SYSTEM, L"Send Packet Full, sessionID : %lld", pSession->sessionID);
		}

		if (packetCount == 0)
		{
			//컨텐츠단의 문제
			systemLog(L"NetServer Exception", dfLOG_LEVEL_SYSTEM, L"No packet Insert, sessionID : %lld", pSession->sessionID);
		}

		pSession->sendPacketCount = packetCount;
		pSession->sendCount += packetCount;

		ZeroMemory(&pSession->SendOverlapped, sizeof(WSAOVERLAPPED));

		InterlockedIncrement(&pSession->IOcount);
		if (pSession->disconnectStep == SESSION_SENDPACKET_LAST)
		{
			InterlockedCompareExchange(&pSession->disconnectStep, SESSION_SENDPOST_LAST, SESSION_SENDPACKET_LAST);
		}
		DWORD flags_send = 0;
		int ret_send = WSASend(pSession->sock, wsaSendbuf, packetCount, NULL, flags_send, (LPWSAOVERLAPPED)&pSession->SendOverlapped, NULL);
		if (ret_send == SOCKET_ERROR)
		{
			int Errorcode = WSAGetLastError();
			switch (Errorcode)
			{
			case WSA_IO_PENDING:
				break;
			case 10004:
			case 10053:
			case 10054:
			{
				CPacket* pPacket;
				for (int i = 0; i < pSession->sendPacketCount; i++)
				{
					pPacket = pSession->sentPacketArray[i];
					int ret_ref = pPacket->subRef();
					if (ret_ref == 0)
					{
						CPacket::mFree(pPacket);
					}
				}
				disconnectSession(pSession);
				InterlockedExchange(&pSession->sendPacketCount, 0);

				_InterlockedExchange(&pSession->sendFlag, 0);
				InterlockedDecrement(&pSession->IOcount);
				break;
			}
			default:
			{
				systemLog(L"WSASend Error", dfLOG_LEVEL_ERROR, L"ErrorCode : %d", Errorcode);
				{
					CPacket* pPacket;
					for (int i = 0; i < pSession->sendPacketCount; i++)
					{
						pPacket = pSession->sentPacketArray[i];
						int ret_ref = pPacket->subRef();
						if (ret_ref == 0)
						{
							CPacket::mFree(pPacket);
						}
					}
					disconnectSession(pSession);
					InterlockedExchange(&pSession->sendPacketCount, 0);

					_InterlockedExchange(&pSession->sendFlag, 0);
					InterlockedDecrement(&pSession->IOcount);
				}
				break;
			}

			}

		}
		if (pSession->disconnectStep == SESSION_DISCONNECT)
		{
			CancelIoEx((HANDLE)pSession->sock, (LPOVERLAPPED)&pSession->SendOverlapped);
		}
	}
}

bool CNetServer::findSession(INT64 SessionID, st_Session** ppSession)
{
	short index = (short)SessionID;

	if (index >= dfMAX_SESSION || index < 0)
	{
		return false;
	}

	st_Session* pSession = &sessionList[index];

	if (pSession->isValid == 0)
	{
		return false;
	}

	InterlockedIncrement(&pSession->IOcount);

	if (pSession->releaseFlag == DELFLAG_ON)
	{
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseRequest(pSession);
		}
		return false;
	}

	if (SessionID != pSession->sessionID)
	{
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseRequest(pSession);
		}
		return false;
	}

	*ppSession = pSession;
	return true;
}

void CNetServer::disconnectSession(st_Session* pSession)
{
	InterlockedExchange(&pSession->disconnectStep, SESSION_DISCONNECT);
	CancelIoEx((HANDLE)pSession->sock, NULL); //send , recv 모두를 cancel시킨다
}

void CNetServer::releaseSession(INT64 SessionID)
{
	short index = (short)SessionID;

	if (index >= dfMAX_SESSION || index < 0)
	{
		return;
	}

	st_Session* pSession = &sessionList[index];
	if (pSession->isValid == FALSE)
	{
		return;
	}

	LONG tempCount = pSession->IOcount;
	if (tempCount < 0)
	{
		systemLog(L"Error", dfLOG_LEVEL_ERROR, L"IOcount WRONG use, sessionID : %lld, IOcount : %d ", SessionID, tempCount);
	}
	if (InterlockedCompareExchange(&pSession->releaseFlag, DELFLAG_ON, tempCount) != tempCount)
	{
		return;
	}

	//이때 성공했으나, 만약 재할당된 세션에 대해 cas가 성공해버린것이라면? sessionID는 바뀌었을 것이다.
	INT64 tempID = pSession->sessionID;
	if (SessionID != pSession->sessionID)
	{
		//systemLog(L"double Closesocket Try", dfLOG_LEVEL_DEBUG, L"old sessionID : %lld, new sessionID : %lld ", SessionID, pSession->sessionID);
		InterlockedIncrement(&pSession->IOcount);
		InterlockedExchange(&pSession->releaseFlag, DELFLAG_OFF);
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseSession(tempID);
		}
		return;
	}
	//정리로직시작



	_InterlockedExchange(&pSession->isValid, FALSE);
	closesocket(pSession->sock);
	pSession->disconnectCount++;

	if (SessionID != pSession->sessionID)
	{
		systemLog(L"Error", dfLOG_LEVEL_ERROR, L"double Closesocket occur, old sessionID : %lld, new sessionID : %lld ", SessionID, pSession->sessionID);
	}

	//sendQueue, sentpacketArray 정리부분
	CPacket* pPacket;

	while (1)
	{
		if (pSession->sendQueue.Dequeue(&pPacket) == false)
		{
			break;
		}
		int ret_ref = pPacket->subRef();
		if (ret_ref == 0)
		{
			CPacket::mFree(pPacket);
		}
	}

	for (int i = 0; i < pSession->sendPacketCount; i++)
	{
		pPacket = pSession->sentPacketArray[i];
		int ret_ref = pPacket->subRef();
		if (ret_ref == 0)
		{
			CPacket::mFree(pPacket);
		}
	}

	InterlockedExchange(&pSession->sendPacketCount, 0);

	pHandler->OnClientLeave(pSession);
	//emptyIndexStack.push(index);
}


void CNetServer::releaseRequest(st_Session* pSession)
{
	PostQueuedCompletionStatus(hcp, dfRELEASE_REQ, (ULONG_PTR)pSession, 0);
}

void CNetServer::PostLeaveCompletion(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	PostQueuedCompletionStatus(hcp, dfONCLIENTLEAVE_SUCESS, (ULONG_PTR)pSession, 0);
}

void CNetServer::AcquireJobQueueLock(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	AcquireSRWLockExclusive(&pSession->JobQueueLock);
}

void CNetServer::AcquireJobQueueLock(st_Session* pSession)
{
	AcquireSRWLockExclusive(&pSession->JobQueueLock);
}

void CNetServer::ReleaseJobQueueLock(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	ReleaseSRWLockExclusive(&pSession->JobQueueLock);
}

void CNetServer::ReleaseJobQueueLock(st_Session* pSession)
{
	ReleaseSRWLockExclusive(&pSession->JobQueueLock);
}


int CNetServer::getJobQueueSize(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	return pSession->JobQueue.size();
}

int CNetServer::getJobPoolSize()
{
	return JobPool.getUseSize();
}

std::queue<st_JobItem*>* CNetServer::getJobQueue(INT64 SessionID)
{
	short index = (short)SessionID;
	if (index >= dfMAX_SESSION || index < 0)
	{
		return NULL;
	}

	else
	{
		st_Session* pSession = &sessionList[index];
		return &pSession->JobQueue;
	}
}

void CNetServer::ClearJobQueue(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	AcquireSRWLockExclusive(&pSession->JobQueueLock);
	std::queue<st_JobItem*> empty;
	std::swap(pSession->JobQueue, empty);
	ReleaseSRWLockExclusive(&pSession->JobQueueLock);
}

st_JobItem* CNetServer::PopJobItem(INT64 SessionID)
{
	short index = (short)SessionID;
	st_Session* pSession = &sessionList[index];
	st_JobItem* pJobItem = pSession->JobQueue.front();
	pSession->JobQueue.pop();
	return pJobItem;
}

void CNetServer::freeJobItem(st_JobItem* JobItem)
{
	JobPool.mFree(JobItem);
}

bool CNetServer::popLoginQueue(INT64* pSessionID)
{
	INT64 temp;
	if (LoginQueue.Dequeue(&temp) == true)
	{
		*pSessionID = temp;
		return true;
	}
	
	return false;
}


void CNetServer::sendPacket(INT64 SessionID, CPacket* pPacket, BOOL LastPacket)
{
	st_Session* pSession;
	if (findSession(SessionID, &pSession) == false)
	{
		return;
	}

	if (pSession->disconnectStep == SESSION_DISCONNECT)
	{
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseRequest(pSession);
		}
		return;
	}
	//사용시작

	pPacket->addRef(1);

	if (pPacket->isEncoded() == FALSE)
	{
		BOOL ret_Encode = pPacket->Encode();
		if (ret_Encode == FALSE)
		{
			CrashDump::Crash();
		}
	}

	//LockFree Sendqueue Enqueue
	if (pSession->sendQueue.Enqueue(pPacket) == false)
	{
		systemLog(L"NetServer Exception", dfLOG_LEVEL_SYSTEM, L"sendQueue Enqueue Fail, Session Number : %lld ", SessionID);
		disconnectSession(pSession);
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseRequest(pSession);
		}
		return;
	}

	if (LastPacket == TRUE)
	{
		InterlockedCompareExchange(&pSession->disconnectStep, SESSION_SENDPACKET_LAST, SESSION_NORMAL_STATE);
	}

	//IOcount 1 올리고 PQCS
	InterlockedIncrement(&pSession->IOcount);
	if (PostQueuedCompletionStatus(hcp, dfSENDPOST_REQ, (ULONG_PTR)pSession, 0) == FALSE)
	{
		systemLog(L"Error", dfLOG_LEVEL_ERROR, L"PQCS Fail, Session Number : %lld, Error code : %d", SessionID, WSAGetLastError());
		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			releaseRequest(pSession);
		}
	}
	//sendPost(pSession);

	//사용완료

	if (InterlockedDecrement(&pSession->IOcount) == 0)
	{
		releaseRequest(pSession);
	}
	return;
}


void CNetServer::sendPacket(CSessionSet* pSessionSet, CPacket* pPacket, BOOL LastPacket)
{
	for (int i = 0; i < pSessionSet->Session_Count; i++)
	{
		sendPacket(pSessionSet->Session_Array[i], pPacket, LastPacket);
	}
}

int CNetServer::getMaxSession()
{
	return this->maxSession;
}

int CNetServer::getSessionCount()
{
	return this->sessionNum;
}
int CNetServer::getAcceptTPS()
{
	return this->acceptTPS;
}

INT64 CNetServer::getAcceptSum()
{
	return this->acceptSum;
}

int CNetServer::getDisconnectTPS()
{
	return this->disconnectTPS;
}

int CNetServer::getRecvMessageTPS()
{
	return this->recvTPS;
}
int CNetServer::getSendMessageTPS()
{
	return this->sendTPS;
}

void CNetServer::attachHandler(CNetServerHandler* pHandler)
{
	this->pHandler = pHandler;
}

DWORD WINAPI CNetServer::ControlThread(CNetServer* ptr)
{
	while (!ptr->Shutdown)
	{
		ptr->Temp_sendTPS = 0;
		ptr->Temp_recvTPS = 0;
		ptr->Temp_disconnectTPS = 0;
		ptr->Temp_sessionNum = 0;
		/*
		ptr->sendTPS = 0;
		ptr->recvTPS = 0;
		ptr->disconnectTPS = 0;
		*/

		ptr->acceptTPS = ptr->acceptCount;
		ptr->acceptCount = 0;

		for (int i = 0; i < ptr->maxSession; i++)
		{
			if (ptr->sessionList[i].isValid == TRUE)
			{
				ptr->Temp_sessionNum++;
			}

			ptr->Temp_disconnectTPS += ptr->sessionList[i].disconnectCount;
			ptr->sessionList[i].disconnectCount = 0;


			ptr->Temp_sendTPS += ptr->sessionList[i].sendCount;
			ptr->sessionList[i].sendCount = 0;

			ptr->Temp_recvTPS += ptr->sessionList[i].recvCount;
			ptr->sessionList[i].recvCount = 0;

		}
		ptr->sendTPS = ptr->Temp_sendTPS;
		ptr->recvTPS = ptr->Temp_recvTPS;
		ptr->disconnectTPS = ptr->Temp_disconnectTPS;
		ptr->sessionNum = ptr->Temp_sessionNum;

		Sleep(1000);
	}
	return 0;

}

DWORD WINAPI CNetServer::AcceptThread(CNetServer* ptr)
{
	//accept()
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;
	addrlen = sizeof(clientaddr);
	DWORD flags;
	int retval;

	while (1)
	{
		client_sock = accept(ptr->listenSock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			DWORD ErrorCode = WSAGetLastError();
			if (ErrorCode == 10004)
			{
				continue;
			}
			else
			{
				systemLog(L"ACCEPT EXCEPTION", dfLOG_LEVEL_ERROR, L"ErrorCode : %d", ErrorCode);
				break;
			}

		}
		ptr->acceptSum++;
		ptr->acceptCount++;
		char clientIP1[20] = { 0 };
		inet_ntop(AF_INET, &clientaddr.sin_addr, clientIP1, sizeof(clientIP1));

		BOOL ret_req = ptr->pHandler->OnConnectionRequest();
		if (ret_req == FALSE)
		{
			continue;
		}
		int index = 0;

		BOOL ret_pop = ptr->emptyIndexStack.pop(&index);
		if (ret_pop == FALSE)
		{
			systemLog(L"ACCEPT EXCEPTION", dfLOG_LEVEL_DEBUG, L"sessionList FULL");
			closesocket(client_sock);
		}

		INT64 AllocNum = ptr->sessionAllocNum++;//CompletionKey로 사용
		INT64 newSessionID = (AllocNum << 16 | (INT64)index);
		st_Session* pSession = &ptr->sessionList[index];
		InterlockedExchange64(&pSession->sessionID, newSessionID);
		CreateIoCompletionPort((HANDLE)client_sock, ptr->hcp, (ULONG_PTR)pSession, 0);

		InterlockedIncrement(&pSession->IOcount);
		InterlockedExchange(&pSession->isValid, TRUE);
		InterlockedExchange(&pSession->releaseFlag, DELFLAG_OFF);
		ZeroMemory(&pSession->RecvOverlapped, sizeof(WSAOVERLAPPED));
		ZeroMemory(&pSession->SendOverlapped, sizeof(WSAOVERLAPPED));
		pSession->RecvOverlapped.flag = 0;
		pSession->SendOverlapped.flag = 1;
		pSession->sock = client_sock;
		pSession->sendFlag = 0;
		pSession->recvQueue.ClearBuffer();
		pSession->disconnectStep = SESSION_NORMAL_STATE;

		CPacket* pPacket;
		while (1)
		{
			if (pSession->sendQueue.Dequeue(&pPacket) == false)
			{
				break;
			}
			int ret_ref = pPacket->subRef();
			if (ret_ref == 0)
			{
				CPacket::mFree(pPacket);
			}
		}

		WSABUF wsabuf;
		wsabuf.buf = pSession->recvQueue.GetRearBufferPtr();
		wsabuf.len = pSession->recvQueue.DirectEnqueueSize();

		//recv걸기
		flags = 0;

		ptr->pHandler->OnClientJoin(pSession);
		retval = WSARecv(client_sock, &wsabuf, 1, NULL, &flags, (LPWSAOVERLAPPED)&pSession->RecvOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			DWORD ErrorCode = WSAGetLastError();
			switch (ErrorCode)
			{
			case WSA_IO_PENDING:
				break;
			case 10004:
			case 10053:
			case 10054:
				InterlockedDecrement(&pSession->IOcount);
				break;
			default:
				InterlockedDecrement(&pSession->IOcount);
				systemLog(L"WSARECV EXCEPTION", dfLOG_LEVEL_SYSTEM, L"ErrorCode : %d", ErrorCode);
				break;
			}
		}

	}
	return 0;
}

DWORD WINAPI CNetServer::WorkerThread(CNetServer* ptr)
{
	int ret_GQCS;

	while (1)
	{
		DWORD transferred = 0;
		st_Session* pSession = NULL;
		st_MyOverlapped* pOverlapped = NULL;
		BOOL error_flag = FALSE;
		ret_GQCS = GetQueuedCompletionStatus(ptr->hcp, &transferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&pOverlapped, INFINITE);

		if (transferred == 0 && pSession == 0 && pOverlapped == 0)
		{
			break;
		}

		if (ret_GQCS == 0 || transferred == 0)
		{
			error_flag = TRUE;
		}

		if (error_flag == FALSE)
		{
			//sendpacket 요청일시
			if (transferred == dfSENDPOST_REQ && pOverlapped == 0)
			{
				ptr->sendPost(pSession);
			}

			//release 요청시
			else if (transferred == dfRELEASE_REQ && pOverlapped == 0)
			{
				ptr->releaseSession(pSession->sessionID);
				continue;
			}

			else if (transferred == dfONCLIENTLEAVE_SUCESS && pOverlapped == 0)
			{
				short index = (short)pSession->sessionID;
				ptr->emptyIndexStack.push(index);
				continue;
			}

			//recv일시
			else if (pOverlapped->flag == 0)
			{

				DWORD flags_send = 0;
				DWORD flags_recv = 0;

				pSession->recvQueue.MoveRear(transferred);
				DWORD leftByte = transferred;
				while (1)
				{
					if (leftByte == 0) // 다 뺐다면
					{
						break;
					}
					st_header header;

					pSession->recvQueue.Peek((char*)&header, sizeof(st_header));
					if (header.code != dfNETWORK_CODE) //key값 다를시 잘못된 패킷
					{
						systemLog(L"NETWORK CODE WRONG", dfLOG_LEVEL_DEBUG, L"%d", header.code);
						ptr->disconnectSession(pSession);
						break;
					}

					if (header.len > CPacket::en_PACKET::BUFFER_DEFAULT || header.len < 0) // len값 부정확할시 잘못된 패킷
					{
						systemLog(L"PACKET LEN WRONG", dfLOG_LEVEL_DEBUG, L"%d", header.len);
						ptr->disconnectSession(pSession);
						break;
					}

					if (header.len + dfNETWORK_HEADER_SIZE > pSession->recvQueue.GetUseSize()) //아직 다 안 온 상황
					{					
						break;						
					}

					CPacket* pRecvBuf = CPacket::mAlloc();
					pRecvBuf->addRef(1);
					pRecvBuf->ClearNetwork();
					int ret_dequeue = pSession->recvQueue.Dequeue(pRecvBuf->GetWriteBufferPtr(), header.len + dfNETWORK_HEADER_SIZE);
					if (ret_dequeue <= 0)
					{
						systemLog(L"RECV DEQUEUE FAIL", dfLOG_LEVEL_DEBUG, L"");
						int ret_ref = pRecvBuf->subRef();
						if (ret_ref == 0)
						{
							CPacket::mFree(pRecvBuf);
						}
						ptr->disconnectSession(pSession); //Dequeue 오류
						break;
					}
					pRecvBuf->MoveWritePos(ret_dequeue);
					if (pRecvBuf->Decode() == FALSE)
					{
						int ret_ref = pRecvBuf->subRef();
						if (ret_ref == 0)
						{
							CPacket::mFree(pRecvBuf);
						}
						systemLog(L"DECODE FAIL", dfLOG_LEVEL_DEBUG, L"");
						ptr->disconnectSession(pSession); //패킷 디코딩 실패
						break;
					}

					pRecvBuf->MoveReadPos(dfNETWORK_HEADER_SIZE);
					
					ptr->pHandler->OnRecv(pSession, pRecvBuf);
					int ret_ref = pRecvBuf->subRef();
					if (ret_ref == 0)
					{
						CPacket::mFree(pRecvBuf);
					}
					leftByte -= header.len + dfNETWORK_HEADER_SIZE;
					pSession->recvCount++;
				}
				ptr->recvPost(pSession);
			}

			//send일시
			else if (pOverlapped->flag == 1)
			{
				if (pSession->disconnectStep == SESSION_SENDPOST_LAST)
				{
					ptr->disconnectSession(pSession);
				}

				CPacket* pPacket;
				for (int i = 0; i < pSession->sendPacketCount; i++)
				{
					pPacket = pSession->sentPacketArray[i];
					int ret_ref = pPacket->subRef();
					if (ret_ref == 0)
					{
						CPacket::mFree(pPacket);
					}
				}
				InterlockedExchange(&pSession->sendPacketCount, 0);

				_InterlockedExchange(&pSession->sendFlag, 0);
				if (pSession->sendQueue.nodeCount > 0)
				{
					ptr->sendPost(pSession);
				}
			}
		}

		if (InterlockedDecrement(&pSession->IOcount) == 0)
		{
			ptr->releaseSession(pSession->sessionID);
		}
	}
	return 0;
}


bool CNetServerHandler::OnConnectionRequest()
{
	return true;
}

void CNetServerHandler::OnClientJoin(st_Session* pSession)
{
	/*
	st_JobItem* jobItem;
	pNetServer->JobPool.mAlloc(&jobItem);
	jobItem->JobType = en_JOB_ON_CLIENT_JOIN;
	jobItem->SessionID = pSession->sessionID;
	jobItem->pPacket = NULL;

	pNetServer->AcquireJobQueueLock(pSession);
	pSession->JobQueue.push(jobItem); // 로그인 스레드에서 처리
	pNetServer->ReleaseJobQueueLock(pSession);
	*/

	//로그인스레드에 수동으로 넣어줘야함.
	pNetServer->LoginQueue.Enqueue(pSession->sessionID);
}

void CNetServerHandler::OnClientLeave(st_Session* pSession)
{
	st_JobItem* jobItem;
	pNetServer->JobPool.mAlloc(&jobItem);
	jobItem->JobType = en_JOB_ON_CLIENT_LEAVE;
	jobItem->SessionID = pSession->sessionID;
	jobItem->pPacket = NULL;

	pNetServer->AcquireJobQueueLock(pSession);
	pSession->JobQueue.push(jobItem); //현재 소유하는 스레드에서 처리 
	pNetServer->ReleaseJobQueueLock(pSession);
}

void CNetServerHandler::OnError(st_Session* pSession, int errorCode)
{

}

bool CNetServerHandler::OnRecv(st_Session* pSession, CPacket* pPacket)
{
	pPacket->addRef(1);

	st_JobItem* jobItem;
	pNetServer->JobPool.mAlloc(&jobItem);
	jobItem->JobType = en_JOB_ON_RECV;
	jobItem->SessionID = pSession->sessionID;
	jobItem->pPacket = pPacket;

	pNetServer->AcquireJobQueueLock(pSession);
	pSession->JobQueue.push(jobItem); //현재 소유하는 스레드에서 처리 
	pNetServer->ReleaseJobQueueLock(pSession);

	return true;
}
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
#include <list>
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
#include "CThreadManager.h"

CMemoryPoolBucket<st_Player> CParentThread::PlayerPool;

DWORD WINAPI CParentThread::ThreadFunction(CParentThread* Instance)
{
	
	CThreadHandler* pThreadHandler = Instance->pThreadManager->pThreadHandler;
	CNetServer* pServer = Instance->pServer;
	int enterQueueSize;
	int PlayerListSize;
	while (!Instance->shutdown)
	{
		Instance->Frame++;
		DWORD startTime = timeGetTime();

		Instance->Update();

		//입장대기큐 디큐 -> 잡전부삭제 -> playerList로 이동
		//만약 이 삭제 중 OnclientLeave가 있다면 거기까지만 처리하고
		//workerthread쪽에 PQCS로 OnclientLeave의 성공을 알림(stack push를 하기위해)
		PlayerListSize = Instance->PlayerList.size();
		enterQueueSize = Instance->enterQueue.size();
		if (enterQueueSize > 0)
		{
			AcquireSRWLockExclusive(&Instance->enterQueueLock);
			for (int i = 0; i < enterQueueSize; i++)
			{
				Instance->pThreadManager->ThreadMoveTPS++;
				Instance->EnterUserTPS++;

				st_ThreadMoveInfo temp = Instance->enterQueue.front();
				INT64 sessionID = temp.pPlayer->SessionID;
				INT64 AccountNo = temp.pPlayer->AccountNo;
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				if (pJobQueue != NULL)
				{
					volatile bool LeaveFlag = false;
					pServer->AcquireJobQueueLock(sessionID);
					st_JobItem* JobItem;
					INT64 JobType;
					int JobQueueSize = pServer->getJobQueueSize(sessionID);
					{
						for (int i = 0; i < JobQueueSize; i++)
						{
							JobItem = pServer->PopJobItem(sessionID);
							JobType = JobItem->JobType;
							CPacket* pPacket = JobItem->pPacket;
							pServer->freeJobItem(JobItem);

							switch (JobType)
							{
							case en_JOB_ON_CLIENT_LEAVE:
							{
								LeaveFlag = true;
								break;
							}

							case en_JOB_ON_RECV:
							{
								if (pPacket->subRef() == 0)
								{
									CPacket::mFree(pPacket);
								}
								break;
							}

							default:
							{
								//로그찍기
								break;
							}
							}


						}

					}
					pServer->ReleaseJobQueueLock(sessionID);
					if (LeaveFlag == true)
					{
						pServer->PostLeaveCompletion(sessionID); //PQCS로 워커에게 Leave완료 알려주기.
						Instance->pThreadManager->pThreadHandler->onPlayerLeave(AccountNo, temp.pPlayer->playerInfo);
						PlayerPool.mFree(temp.pPlayer);
					}
					else
					{
						Instance->PlayerList.push_back(temp.pPlayer);
						pThreadHandler->onPlayerMove(AccountNo, temp.srcThread, temp.desThread); //OnPlayerMove 호출		
						Instance->onThreadJoin(temp.pPlayer->SessionID, AccountNo, temp.pPlayer->playerInfo);
					}
				}


				Instance->enterQueue.pop();
			}
			ReleaseSRWLockExclusive(&Instance->enterQueueLock);
		}

		//PlayerList 순회하며 잡처리.
		for (int i = 0; i < PlayerListSize; i++)
		{
			INT64 sessionID;
			for (auto iter = Instance->PlayerList.begin(); iter != Instance->PlayerList.end(); )
			{
				sessionID = (*iter)->SessionID;
				pServer->AcquireJobQueueLock(sessionID);
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				int JobQueueSize = pServer->getJobQueueSize(sessionID);
				if (pJobQueue != NULL)
				{
					

					volatile short MoveThreadNum = -1;
					volatile bool MoveFlag = false;
					volatile bool LeaveFlag = false;
					st_JobItem* JobItem;
					INT64 JobType;
					INT64 AccountNo;
					if (JobQueueSize >= 0)
					{

						for (int i = 0; i < JobQueueSize && !(MoveFlag == true && LeaveFlag == false); i++)
						{
							Instance->pThreadManager->GameThread_JobTPS++;
							JobItem = pServer->PopJobItem(sessionID);
							JobType = JobItem->JobType;
							AccountNo = JobItem->AccountNo;
							CPacket* pPacket = JobItem->pPacket;
							pServer->freeJobItem(JobItem);


							switch (JobType)
							{
							case en_JOB_ON_CLIENT_LEAVE: //LeaveFlag가 true로 바꿔줌, 이후 모든 잡 및 스레드이동 무시됨, PQCS로 완료를 던져야함.
							{
								LeaveFlag = true;
								break;
							}

							case en_JOB_ON_RECV:
							{

								if (LeaveFlag == false)
								{
									Instance->OnRecv(&MoveFlag, &MoveThreadNum, (*iter)->playerInfo, (*iter)->SessionID, AccountNo, pPacket);
								}
								if (pPacket->subRef() == 0)
								{
									CPacket::mFree(pPacket);
								}
								break;
							}

							default:
							{
								//로그찍기
								break;
							}
							}
						}
						if (LeaveFlag == true)
						{
							pServer->PostLeaveCompletion(sessionID); //PQCS로 워커에게 Leave완료 알려주기.
							//컨텐츠에 leave 알림.
							st_Player* pPlayer = *iter;
							iter = Instance->PlayerList.erase(iter);
							pThreadHandler->onPlayerLeave(AccountNo, pPlayer->playerInfo);
							PlayerPool.mFree(pPlayer);
						}
						else if (LeaveFlag == false && MoveFlag == true)
						{
							//이동할 대상 스레드의 대기큐에 꽂고, 나 삭제
							//스레드 이동결과를 컨텐츠쪽에 알리는것은 이동한 스레드에서 처리
							Instance->pThreadManager->MovePlayer(Instance->ThreadNumber, MoveThreadNum, *iter);
							iter = Instance->PlayerList.erase(iter);
						}
						else
						{
							iter++;
						}
					}
				}
				else
				{
					//오류, 로그찍기.
				}
				pServer->ReleaseJobQueueLock(sessionID);
			}


		}

		DWORD Period = timeGetTime() - startTime;
		if (Period < Instance->TimeInterval)
		{
			Sleep(Instance->TimeInterval - Period);
		}


	}
	return true;
}



DWORD WINAPI CLoginThread::LoginThreadFunction(CLoginThread* Instance)
{


	//대기큐에서 뽑는다
	CThreadHandler* pThreadHandler = Instance->pThreadManager->pThreadHandler;
	CNetServer* pServer = Instance->pServer;
	int enterQueueSize;
	int PlayerListSize;
	while (!Instance->shutdown)
	{
		Instance->Frame++;
		DWORD startTime = timeGetTime();

		INT64 LoginSessionID;
		while (pServer->popLoginQueue(&LoginSessionID) == true)
		{
			Instance->pThreadManager->LoginTPS++;

			st_Player* pPlayer;
			PlayerPool.mAlloc(&pPlayer);
			pPlayer->SessionID = LoginSessionID;
			pPlayer->AccountNo = -1;
			pPlayer->playerInfo = NULL;
			
			PlayerInfo* pPlayerInfo = pThreadHandler->onPlayerJoin(LoginSessionID);
			if (pPlayerInfo != NULL)
			{
				pPlayer->playerInfo = pPlayerInfo;
				Instance->PlayerList.push_back(pPlayer);
			}
			else
			{
				PlayerPool.mFree(pPlayer);
				st_Session* pSession;
				if (pServer->findSession(LoginSessionID, &pSession) == true)
				{
					pServer->disconnectSession(pSession);
				}
				
			}
			
		}

		Instance->Update();

		PlayerListSize = Instance->PlayerList.size();

		//PlayerList 순회하며 잡처리.
		for (int i = 0; i < PlayerListSize; i++)
		{
			INT64 sessionID;
			for (auto iter = Instance->PlayerList.begin(); iter != Instance->PlayerList.end(); )
			{
				sessionID = (*iter)->SessionID;
				pServer->AcquireJobQueueLock(sessionID);
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				int JobQueueSize = pServer->getJobQueueSize(sessionID);
				if (pJobQueue != NULL)
				{
					volatile short MoveThreadNum = -1;
					volatile bool MoveFlag = false;
					volatile bool LeaveFlag = false;
					st_JobItem* JobItem;
					INT64 JobType;
					INT64 AccountNo;
					for (int i = 0; i < JobQueueSize && !(MoveFlag == true && LeaveFlag == false); i++)
					{
						Instance->pThreadManager->LoginThread_JobTPS++;


						JobItem = pServer->PopJobItem(sessionID);
						JobType = JobItem->JobType;
						AccountNo = JobItem->AccountNo;
						CPacket* pPacket = JobItem->pPacket;
						pServer->freeJobItem(JobItem);


						switch (JobType)
						{
						case en_JOB_ON_CLIENT_LEAVE: //LeaveFlag가 true로 바꿔줌, 이후 모든 잡 및 스레드이동 무시됨, PQCS로 완료를 던져야함.
						{
							LeaveFlag = true;
							break;
						}

						case en_JOB_ON_RECV:
						{

							if (LeaveFlag == false)
							{
								Instance->OnRecv(&MoveFlag, &MoveThreadNum, (*iter)->playerInfo, (*iter)->SessionID, AccountNo, pPacket);
							}
							if (pPacket->subRef() == 0)
							{
								CPacket::mFree(pPacket);
							}
							break;
						}

						default:
						{
							//로그찍기
							break;
						}
						}
					}
					if (LeaveFlag == true)
					{
						pServer->PostLeaveCompletion(sessionID); //PQCS로 워커에게 Leave완료 알려주기.
						//컨텐츠에 leave 알림.
						st_Player* pPlayer = *iter;
						iter = Instance->PlayerList.erase(iter);
						pThreadHandler->onPlayerLeave(AccountNo, pPlayer->playerInfo);
						PlayerPool.mFree(pPlayer);
					}
					else if (LeaveFlag == false && MoveFlag == true)
					{
						//이동할 대상 스레드의 대기큐에 꽂고, 나 삭제
						//스레드 이동결과를 컨텐츠쪽에 알리는것은 이동한 스레드에서 처리
						Instance->pThreadManager->MovePlayer(Instance->ThreadNumber, MoveThreadNum, *iter);
						iter = Instance->PlayerList.erase(iter);
					}
					else
					{
						iter++;
					}
				}
				else
				{
					//오류, 로그찍기.
				}
				pServer->ReleaseJobQueueLock(sessionID);
			}


		}

		DWORD Period = timeGetTime() - startTime;
		if (Period < Instance->TimeInterval)
		{
			Sleep(Instance->TimeInterval - Period);
		}


	}
	return true;
}


void CLoginThread::onLoginSucess(INT64 sessionID, INT64 AccountNo)
{
	for (auto iter = PlayerList.begin(); iter != PlayerList.end(); iter++)
	{
		st_Player* pPlayer = *iter;
		if (pPlayer->SessionID == sessionID)
		{
			pPlayer->AccountNo = AccountNo;
			pPlayer->playerInfo->isValid = true;
			break;
		}
	}
}


void CThreadManager::registThread(short threadNo, CParentThread* pThread, bool isLoginThread)
{
	if (isLoginThread)
	{
		LoginThreadList.insert(make_pair(threadNo, (CLoginThread*)pThread));
	}
	else
	{
		GameThreadList.insert(make_pair(threadNo, (CParentThread*)pThread));
	}
	pThread->pThreadManager = this;
	pThread->pThreadHandler = this->pThreadHandler;
	Threadindex++;
}

bool CThreadManager::MovePlayer(short srcThreadNo, short DesThreadNo, st_Player* pPlayer)
{
	auto iter = GameThreadList.find(DesThreadNo);
	if (iter == GameThreadList.end())
	{
		return false;
	}

	CParentThread* pThread = iter->second;
	AcquireSRWLockExclusive(&pThread->enterQueueLock);
	st_ThreadMoveInfo MoveInfo;
	MoveInfo.desThread = DesThreadNo;
	MoveInfo.srcThread = srcThreadNo;
	MoveInfo.pPlayer = pPlayer;
	pThread->enterQueue.push(MoveInfo);
	ReleaseSRWLockExclusive(&pThread->enterQueueLock);

	return true;
}

void CThreadManager::attachServerInstance(CNetServer* pNetServer)
{
	this->pServer = pNetServer;
	for (auto iter = LoginThreadList.begin(); iter != LoginThreadList.end(); iter++)
	{
		CLoginThread* pLoginThread = iter->second;
		pLoginThread->attachServer(pNetServer);
	}

	for (auto iter = GameThreadList.begin(); iter != GameThreadList.end(); iter++)
	{
		CParentThread* pGameThread = iter->second;
		pGameThread->attachServer(pNetServer);
	}
}

void CThreadManager::start()
{
	int index = 0;
	for (auto iter = LoginThreadList.begin(); iter != LoginThreadList.end(); iter++)
	{
		CLoginThread* pLoginThread = iter->second;
		ThreadHANDLE[index] = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&pLoginThread->LoginThreadFunction, pLoginThread, 0, 0);
		index++;
	}

	for (auto iter = GameThreadList.begin(); iter != GameThreadList.end(); iter++)
	{
		CParentThread* pGameThread = iter->second;
		ThreadHANDLE[index] = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&pGameThread->ThreadFunction, pGameThread, 0, 0);
		index++;
	}
}
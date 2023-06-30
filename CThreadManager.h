#pragma once

#define MAX_THREAD 1000

class PlayerInfo
{
public:
	bool isValid;
};

struct st_Player
{
	INT64 SessionID;
	INT64 AccountNo;
	PlayerInfo* playerInfo;
};

struct st_ThreadMoveInfo
{
	short srcThread;
	short desThread;
	st_Player* pPlayer;
};

CMemoryPoolBucket<st_JobItem> JobPool;

class CThreadManager;
class CThreadHandler;

class CParentThread
{


public:
	static DWORD WINAPI ThreadFunction(CParentThread* Instance); // 스레드 함수. 
	//void JobProcess(void); // 현재스레드에 소속된 플레이어들의 잡을 순회하면서 뺀다
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum) = 0;
	virtual void Update(void) = 0;
	void setPlayerInfo(st_Player* pPlayer, PlayerInfo* playerInfo) // Player 객체에 Info를 붙여준다.
	{
		pPlayer->playerInfo = playerInfo;
	}
	void attachServer(CNetServer* pNetServer)
	{
		this->pServer = pNetServer;
	}

	friend class CThreadManager;

protected:

	queue<st_ThreadMoveInfo> enterQueue; //스레드 진입 대기 큐
	SRWLOCK enterQueueLock;
	list<st_Player*> PlayerList; // 플레이어 리스트
	CNetServer* pServer;
	ULONGLONG Frame;
	volatile BOOL shutdown;
	short ThreadNumber;

	static CMemoryPoolBucket<st_Player> PlayerPool;
	CThreadManager* pThreadManager;
	DWORD TimeInterval;
};

class CLoginThread : public CParentThread
{
public:
	static DWORD WINAPI LoginThreadFunction(CLoginThread* Instance);
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum) = 0;
	virtual void Update(void) = 0;
	
};

class CThreadManager
{
public:
	void registThread(short threadNo, CParentThread* pThread, bool isLoginThread=false);
	bool MovePlayer(short srcThreadNo, short DesThreadNo, st_Player* pPlayer);
	void attachServerInstance(CNetServer* pNetServer);
	void start();

	CThreadHandler* pThreadHandler;
	CNetServer* pNetServer;
private:
	CNetServer* pServer;
	unordered_map<short, CLoginThread*> LoginThreadList;
	unordered_map<short, CParentThread*> GameThreadList;

	HANDLE ThreadHANDLE[MAX_THREAD];
	short Threadindex = 0;
};

class CThreadHandler
{
public:
	virtual bool onPlayerJoin(INT64 sessionID, INT64* pAccountNo, PlayerInfo** ppPlayerInfo) = 0;
	virtual void onPlayerMove(INT64 accountNo, short srcThread, short desThread) = 0;
	virtual void onPlayerLeave(INT64 accountNo) = 0;
};
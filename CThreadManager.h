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
	static DWORD WINAPI ThreadFunction(CParentThread* Instance); // ������ �Լ�. 
	//void JobProcess(void); // ���罺���忡 �Ҽӵ� �÷��̾���� ���� ��ȸ�ϸ鼭 ����
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum) = 0;
	virtual void Update(void) = 0;
	void setPlayerInfo(st_Player* pPlayer, PlayerInfo* playerInfo) // Player ��ü�� Info�� �ٿ��ش�.
	{
		pPlayer->playerInfo = playerInfo;
	}
	void attachServer(CNetServer* pNetServer)
	{
		this->pServer = pNetServer;
	}

	friend class CThreadManager;

protected:

	queue<st_ThreadMoveInfo> enterQueue; //������ ���� ��� ť
	SRWLOCK enterQueueLock;
	list<st_Player*> PlayerList; // �÷��̾� ����Ʈ
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
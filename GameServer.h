#pragma once

class GameThread : public CParentThread
{
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum);
	virtual void Update(void);
};

class GameLoginThread : public CLoginThread
{
public:
	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum);
	virtual void Update(void);
};

class MyThreadHandler : public CThreadHandler
{
public:
	virtual bool onPlayerJoin(INT64 sessionID, INT64* pAccountNo, PlayerInfo** ppPlayerInfo);
	virtual void onPlayerMove(INT64 accountNo, short srcThread, short desThread);
	virtual void onPlayerLeave(INT64 accountNo);
};
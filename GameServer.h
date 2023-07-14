#pragma once

#define df_THREAD_LOGIN 1
#define df_THREAD_CONTENTS 2

#define df_CERTIFICATION_BEFORE 0
#define df_CERTIFICATION_SUCESS 1
#define df_CERTIFICATION_COMPLETE 2

struct st_SessionKey
{
	char sessionKey[64];
};

inline CPacket& operator << (CPacket& packet, st_SessionKey& SessionKey)
{

	if (packet.GetLeftUsableSize() >= sizeof(st_SessionKey))
	{
		memcpy(packet.GetWriteBufferPtr(), SessionKey.sessionKey, sizeof(st_SessionKey));
		packet.MoveWritePos(sizeof(st_SessionKey));
	}
	return packet;
}

inline CPacket& operator >> (CPacket& packet, st_SessionKey& SessionKey)
{
	if (packet.GetDataSize() >= sizeof(st_SessionKey))
	{
		memcpy(SessionKey.sessionKey, packet.GetReadBufferPtr(), sizeof(st_SessionKey));
		packet.MoveReadPos(sizeof(st_SessionKey));
	}
	return packet;
}

class MyPlayerInfo : public PlayerInfo
{
public:
	INT64 AccountNo;
	short Certificationflag = df_CERTIFICATION_BEFORE;
	bool Loginflag = false;
};


class GameThread : public CParentThread
{
public:
	GameThread(DWORD TimeInterval, short ThreadNum) : CParentThread(TimeInterval, ThreadNum) {};

	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket);
	virtual void Update(void) {};

	virtual void onThreadJoin(INT64 sessionID, INT64 accountNo, PlayerInfo* pPlayerInfo);
	void CS_GAME_RES_LOGIN(INT64 sessionID, BYTE	Status, INT64 AccountNo);
	void CS_GAME_RES_ECHO(INT64 sessionID, INT64 AccountNo, LONGLONG SendTick);


};

class GameLoginThread : public CLoginThread
{
public:
	GameLoginThread(DWORD TimeInterval, short ThreadNum) : CLoginThread(TimeInterval, ThreadNum) {};

	virtual void OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket);
	virtual void Update(void) {};

	void CS_GAME_RES_LOGIN(INT64 sessionID, BYTE	Status, INT64 AccountNo);
	void CS_GAME_RES_ECHO(INT64 sessionID, INT64 AccountNo, LONGLONG SendTick);
};

class MyThreadHandler : public CThreadHandler
{
public:
	virtual PlayerInfo* onPlayerJoin(INT64 sessionID);
	virtual void onPlayerMove(INT64 accountNo, short srcThread, short desThread) {};
	virtual void onPlayerLeave(INT64 accountNo, PlayerInfo* pPlayerInfo);
};

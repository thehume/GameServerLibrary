#pragma once

#define dfNETWORK_HEADER_SIZE 5
#define dfNETWORK_CODE 0x77
#define dfNETWORK_KEY 0x32

#pragma pack(1)

struct st_header
{
	unsigned char code;
	unsigned short len;
	unsigned char randkey;
	unsigned char checksum;
};

#pragma pack()


class CPacket
{
public:

	enum en_PACKET
	{
		BUFFER_DEFAULT = 2000
	};

	void addRef(long addNum);
	long subRef(void);

	void Release(void);
	void Clear(void);
	void ClearNetwork(void);
	int GetBufferSize(void);
	int GetDataSize(void);
	int GetLeftUsableSize(void);
	void AddDataSize(int size);
	void SubDataSize(int size);
	char* GetWriteBufferPtr(void);
	char* GetReadBufferPtr(void);
	int MoveWritePos(int Size);
	int MoveReadPos(int Size);
	BOOL isEncoded() // 멀티스레드에 안전하지 않다. 같은 패킷에 대한 sendpacket은 같은 스레드에서 순차적으로 진행하도록 설계해야함
	{
		return this->encodeFlag;
	}
	BOOL Encode();
	BOOL Decode();
	static CPacket* mAlloc();
	static bool mFree(CPacket* inParam);
	static LONG getPoolUseSize();

	CPacket& operator << (unsigned char byValue);
	CPacket& operator << (char chValue);

	CPacket& operator << (short shValue);
	CPacket& operator << (unsigned short wValue);

	CPacket& operator << (int iValue);
	CPacket& operator << (unsigned int iValue);
	CPacket& operator << (long lValue);
	CPacket& operator << (unsigned long lValue);
	CPacket& operator << (float fValue);

	CPacket& operator << (__int64 iValue);
	CPacket& operator << (double dValue);

	CPacket& operator >> (unsigned char& byValue);
	CPacket& operator >> (char& chValue);

	CPacket& operator >> (short& shValue);
	CPacket& operator >> (unsigned short& wValue);

	CPacket& operator >> (int& iValue);
	CPacket& operator >> (unsigned int& iValue);
	CPacket& operator >> (long& dwValue);
	CPacket& operator >> (unsigned long& dwValue);
	CPacket& operator >> (float& fValue);

	CPacket& operator >> (__int64& iValue);
	CPacket& operator >> (double& dValue);

	int GetData(char* chpDest, int GetSize);
	int PutData(char* chpSrc, int PutSize);

	friend class DataBlock<CPacket>;
	friend class CMemoryPool<CPacket>;
	static CMemoryPoolBucket<CPacket> PacketPool;

	int DataSize; //current use size

private:
	CPacket();
	CPacket& operator = (CPacket& SrcPacket);
	CPacket(const CPacket& src) {};
	virtual ~CPacket();

	int BufferSize; //whole size

	LONG refCount;

	char* begin;
	char* end;
	char* readPos;
	char* writePos;

	BOOL encodeFlag;
};
#pragma once
#include "CLanServer.h"
#include <unordered_map>
using namespace std;

//class CLoginServer;

class CLoginLanServer : public CLanServer {
private:
	static CObjectPool<st_CLIENT> _ClientPool;
	static SRWLOCK srwCLIENT;
	//key - SessionID, value - st_CLIENT
	static unordered_map<INT64, st_CLIENT*> _ClientMap;
	LPVOID pLoginServer;

public:
	LONG _LoginSuccessTPS;
	LONG _TotalLoginTime;
	LONG _LoginWaitCount;
	friend class CLoginServer;
	CLoginLanServer(LPVOID ptr);

	void MPLoginResLogin(CPacket* pPacket, WORD Type, INT64 AccountNo, BYTE Status, WCHAR* ID,
		WCHAR* Nickname, WCHAR* GameServerIP, USHORT GameServerPort, WCHAR* ChatServerIP, USHORT ChatServerPort);

	static void InsertClient(st_CLIENT* pClient) {
		_ClientMap.insert(make_pair(pClient->SessionID, pClient));
	}
	static st_CLIENT* FindClient(INT64 SessionID) {
		st_CLIENT* pClient = NULL;
		auto it = _ClientMap.find(SessionID);
		if (it != _ClientMap.end()) {
			pClient = it->second;
		}
		return pClient;
	}
	static void RemoveClient(INT64 SessionID) {
		_ClientMap.erase(SessionID);
	}


	virtual void OnClientJoin(SOCKADDR_IN clientaddr, INT64 SessionID);
	virtual void OnClientLeave(INT64 SessionID);

	virtual bool OnConnectRequest(SOCKADDR_IN clientaddr);

	virtual void OnRecv(INT64 SessionID, CPacket* pRecvPacket);
	//virtual void OnSend(INT64 SessionID, int SendSize) = 0;

	virtual void OnError(int errorcode, const WCHAR* Err);
};
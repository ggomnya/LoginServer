#pragma once
#include "CNetServer.h"
#include "CLoginLanServer.h"
#include "CommonProtocol.h"
#include <unordered_map>
#include "mysql/include/mysql.h"
#include "mysql/include/errmsg.h"
#include "CDBConnector_TLS.h"
#pragma comment(lib, "mysql/lib/libmysql.lib")
using namespace std;

class CLoginServer : public CNetServer {
private:
	INT64 _ParameterCnt;
	static SRWLOCK srwACCOUNT;
	static CRITICAL_SECTION csSESSION;
	//static SRWLOCK srwSESSION;
	static CObjectPool<st_ACCOUNT> _AccountPool;
	static CObjectPool<st_LOGINSESSION> _SessionPool;
	HANDLE _hEvent;
	HANDLE _hTimeoutThread;
	//key - AccountNo, value - st_ACCOUNT
	static unordered_map<INT64, st_ACCOUNT*> _AccountMap;
	//key - SessionID, value - st_LOGINSESSION
	static unordered_map<INT64, st_LOGINSESSION*> _SessionMap;

	//DB 연결용 맴버
	CDBConnector_TLS* _DBConnector_TLS;
	//SRWLOCK srwDB;
	//CDBConnector* _DBConnector;
public:
	friend class CLoginLanServer;
	CLoginLanServer* _LoginLanServer;
	INT64 _DBTokenMiss;
	INT64 _TimeoutCount;
	CLoginServer(WCHAR* DBConnectIP, WCHAR* DBID, WCHAR* DBPW);

	//timeout처리용 쓰레드 생성
	static unsigned int WINAPI TimeoutThreadFunc(LPVOID lParam) {
		((CLoginServer*)lParam)->TimeoutThread(lParam);
		return 0;
	}
	unsigned int WINAPI TimeoutThread(LPVOID lParam);

	void CheckTimeout();

	void MPReqNewClientLogin(CPacket* pPacket, WORD Type, INT64 AccountNo, char* Token, INT64 Parameter);

	static void InsertLoginSession(st_LOGINSESSION* pLoginSession) {
		_SessionMap.insert(make_pair(pLoginSession->SessionID, pLoginSession));
	}
	static st_LOGINSESSION* FindLoginSession(INT64 SessionID) {
		st_LOGINSESSION* pLoginSession = NULL;
		auto it = _SessionMap.find(SessionID);
		if (it != _SessionMap.end()) {
			pLoginSession = it->second;
		}
		return pLoginSession;
	}
	static void RemoveLoginSession(INT64 SessionID) {
		_SessionMap.erase(SessionID);
	}

	static void InsertAccount(st_ACCOUNT* pAccount, INT64 AccountNo) {
		_AccountMap.insert(make_pair(AccountNo, pAccount));
	}
	static st_ACCOUNT* FindAccount(INT64 AccountNo) {
		st_ACCOUNT* pAccount = NULL;
		auto it = _AccountMap.find(AccountNo);
		if (it != _AccountMap.end())
			pAccount = it->second;
		return pAccount;
	}
	static void RemoveAccount(INT64 AccountNo) {
		int retval = _AccountMap.erase(AccountNo);
		if (retval == 0)
			CCrashDump::Crash();
	}

	static INT64 GetSessionPoolAlloc() {
		return _SessionPool.GetAllocCount();
	}

	static INT64 GetSessionCount() {
		return _SessionMap.size();
	}

	static INT64 GetAccountPoolAlloc() {
		return _AccountPool.GetAllocCount();
	}

	static INT64 GetAccountCount() {
		return _AccountMap.size();
	}
	//클라가 연결 했을 때 할 건 따로 없음
	virtual void OnClientJoin(SOCKADDR_IN clientaddr, INT64 SessionID);
	//클라가 끊겼을 때 따로 할 건 없음
	virtual void OnClientLeave(INT64 SessionID);

	virtual bool OnConnectRequest(SOCKADDR_IN clientaddr);

	virtual void OnRecv(INT64 SessionID, CPacket* pRecvPacket);
	//virtual void OnSend(INT64 SessionID, int SendSize) = 0;

	virtual void OnError(int errorcode, const WCHAR* Err);

	~CLoginServer() {
		delete _DBConnector_TLS;
		//delete _DBConnector;
	}

};
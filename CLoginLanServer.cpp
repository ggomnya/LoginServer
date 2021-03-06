#include "CLoginLanServer.h"
#include "CLoginServer.h"

CObjectPool<st_CLIENT> CLoginLanServer::_ClientPool;
SRWLOCK CLoginLanServer::srwCLIENT;
unordered_map<INT64, st_CLIENT*> CLoginLanServer::_ClientMap;

extern WCHAR ChatServerIP[16];
extern USHORT ChatServerPort;
extern WCHAR GameServerIP[16];
extern USHORT GameServerPort;

CLoginLanServer::CLoginLanServer(LPVOID ptr) {
	pLoginServer = ptr;
	InitializeSRWLock(&srwCLIENT);
	_LoginSuccessTPS = 0;
}



void CLoginLanServer::MPLoginResLogin(CPacket* pPacket, WORD Type, INT64 AccountNo, BYTE Status, WCHAR* ID, WCHAR* Nickname, 
	WCHAR* GameServerIP, USHORT GameServerPort, WCHAR* ChatServerIP, USHORT ChatServerPort) {
	*pPacket << Type << AccountNo << Status;
	pPacket->PutData((char*)ID, 40);
	pPacket->PutData((char*)Nickname, 40);
	pPacket->PutData((char*)GameServerIP, 32);
	*pPacket << GameServerPort;
	pPacket->PutData((char*)ChatServerIP, 32);
	*pPacket << ChatServerPort;

}

void CLoginLanServer::OnClientJoin(SOCKADDR_IN clientaddr, INT64 SessionID) {
	st_CLIENT* newClient = _ClientPool.Alloc();
	newClient->Type = -1;
	newClient->SessionID = SessionID;
	AcquireSRWLockExclusive(&srwCLIENT);
	InsertClient(newClient);
	ReleaseSRWLockExclusive(&srwCLIENT);
	return;
}

void CLoginLanServer::OnClientLeave(INT64 SessionID) {
	st_CLIENT* pClient;
	AcquireSRWLockExclusive(&srwCLIENT);
	pClient = FindClient(SessionID);
	if (pClient == NULL) {
		//있어선 안됨
		CCrashDump::Crash();
	}
	RemoveClient(SessionID);
	ReleaseSRWLockExclusive(&srwCLIENT);
	_ClientPool.Free(pClient);
	return;
}

bool CLoginLanServer::OnConnectRequest(SOCKADDR_IN clientaddr) {
	return true;
}

void CLoginLanServer::OnRecv(INT64 SessionID, CPacket* pRecvPacket) {
	WORD Type;
	*pRecvPacket >> Type;
	//다른 서버가 로그인 했을 시
	if (Type == en_PACKET_SS_LOGINSERVER_LOGIN) {
		BYTE ServerType;
		*pRecvPacket >> ServerType;
		AcquireSRWLockExclusive(&srwCLIENT);
		st_CLIENT* pClient = FindClient(SessionID);
		if (pClient == NULL) {
			//있을 수 없음
			CCrashDump::Crash();
		}
		pClient->Type = ServerType;
		memcpy((char*)pClient->IP, (char*)ChatServerIP, 32);
		pClient->Port = ChatServerPort;
		ReleaseSRWLockExclusive(&srwCLIENT);
	}
	//Token 확인 메세지가 온 경우
	else if (Type == en_PACKET_SS_RES_NEW_CLIENT_LOGIN) {
		INT64 AccountNo;
		INT64 Parameter;
		INT64 LoginSessionID;
		DWORD SuccessTime;
		st_LOGINSESSION* pLoginSession;
		st_ACCOUNT* pAccount;
		*pRecvPacket >> AccountNo >> Parameter;
		//AccountNo 확인하기
		AcquireSRWLockShared(&CLoginServer::srwACCOUNT);
		pAccount = CLoginServer::FindAccount(AccountNo);
		//없는 경우 - 이미 Client 연결이 끊겨서 Account 정보가 지워짐
		//있는 경우 중 parameter가 다른경우 - 이미 새로 클라가 연결됨
		//있는 경우 중 Parameter가 일치하는 경우만 확인하면 됨
		if (pAccount != NULL) {
			if (pAccount->Parameter == Parameter) {
				LoginSessionID = pAccount->SessionID;
				SuccessTime = pAccount->RecvTime;
				ReleaseSRWLockShared(&CLoginServer::srwACCOUNT);
				WORD Status = dfLOGIN_STATUS_OK;
				int ServerCnt = 0;
				WCHAR ID[20];
				WCHAR Nickname[20];

				wsprintf(ID, L"ID_%d", AccountNo);
				wsprintf(Nickname, L"NICK_%d", AccountNo);

				//Packet 만들어서 보내기
				CPacket* pSendPacket = CPacket::Alloc();
				MPLoginResLogin(pSendPacket, en_PACKET_CS_LOGIN_RES_LOGIN, AccountNo, Status,
					ID, Nickname, GameServerIP, GameServerPort, ChatServerIP, ChatServerPort);
				((CLoginServer*)pLoginServer)->SendPacket(LoginSessionID, pSendPacket, eNET);
				pSendPacket->Free();
				
				//log
				InterlockedIncrement(&_LoginSuccessTPS);
				SuccessTime = timeGetTime() - SuccessTime;
				InterlockedAdd(&_TotalLoginTime, SuccessTime);
			}
			else {
				CCrashDump::Crash();
				ReleaseSRWLockShared(&CLoginServer::srwACCOUNT);
			}
		}
		else {
			ReleaseSRWLockShared(&CLoginServer::srwACCOUNT);
		}

	}
}

void CLoginLanServer::OnError(int errorcode, const WCHAR* Err) {
	return;
}
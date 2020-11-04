#include "CLoginLanServer.h"
#include "CLoginServer.h"

CObjectPool<st_CLIENT> CLoginLanServer::_ClientPool;
SRWLOCK CLoginLanServer::srwCLIENT;
unordered_map<INT64, st_CLIENT*> CLoginLanServer::_ClientMap;

extern WCHAR ChatServerIP[16];
extern USHORT ChatServerPort;

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
	/*WCHAR szClientIP[16];
	USHORT port = ntohs(clientaddr.sin_port);
	InetNtop(AF_INET, &clientaddr.sin_addr, szClientIP, 16);*/
	return;
}

void CLoginLanServer::OnClientLeave(INT64 SessionID) {
	st_CLIENT* pClient;
	AcquireSRWLockExclusive(&srwCLIENT);
	pClient = FindClient(SessionID);
	if (pClient == NULL) {
		//�־ �ȵ�
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
	//�ٸ� ������ �α��� ���� ��
	if (Type == en_PACKET_SS_LOGINSERVER_LOGIN) {
		BYTE ServerType;
		*pRecvPacket >> ServerType;
		AcquireSRWLockExclusive(&srwCLIENT);
		st_CLIENT* pClient = FindClient(SessionID);
		if (pClient == NULL) {
			//���� �� ����
			CCrashDump::Crash();
		}
		pClient->Type = ServerType;
		memcpy((char*)pClient->IP, (char*)ChatServerIP, 32);
		pClient->Port = ChatServerPort;
		/*pRecvPacket->GetData((char*)pClient->IP, 32);
		*pRecvPacket >> pClient->Port;*/
		ReleaseSRWLockExclusive(&srwCLIENT);
	}
	//Token Ȯ�� �޼����� �� ���
	else if (Type == en_PACKET_SS_RES_NEW_CLIENT_LOGIN) {
		INT64 AccountNo;
		INT64 Parameter;
		INT64 LoginSessionID;
		DWORD SuccessTime;
		st_LOGINSESSION* pLoginSession;
		st_ACCOUNT* pAccount;
		*pRecvPacket >> AccountNo >> Parameter;
		//AccountNo Ȯ���ϱ�
		AcquireSRWLockExclusive(&CLoginServer::srwACCOUNT);
		pAccount = CLoginServer::FindAccount(AccountNo);
		//���� ��� - �̹� Client ������ ���ܼ� Account ������ ������
		//�ִ� ��� �� parameter�� �ٸ���� - �̹� ���� Ŭ�� �����
		//�ִ� ��� �� Parameter�� ��ġ�ϴ� ��츸 Ȯ���ϸ� ��
		if (pAccount != NULL) {
			if (pAccount->Parameter == Parameter) {
				LoginSessionID = pAccount->SessionID;
				SuccessTime = pAccount->RecvTime;
				ReleaseSRWLockExclusive(&CLoginServer::srwACCOUNT);


				//SessionMap���� ã�´�
				AcquireSRWLockExclusive(&CLoginServer::srwSESSION);
				pLoginSession = CLoginServer::FindLoginSession(LoginSessionID);
				//���� ��� - �̹� �ش� ������ ��ȿ���� ���� - ó���� ���� ����
				//�����ϴ� ���
				if (pLoginSession != NULL) {
					if (AccountNo != pLoginSession->AccountNo) {
						CCrashDump::Crash();
					}
				}
				else {
					ReleaseSRWLockExclusive(&CLoginServer::srwSESSION);
					return;
				}
				ReleaseSRWLockExclusive(&CLoginServer::srwSESSION);
				WORD Status = dfLOGIN_STATUS_OK;
				int ServerCnt = 0;
				WCHAR ID[20];
				WCHAR Nickname[20];
				WCHAR GameServerIP[16];
				USHORT GameServerPort=0;
				WCHAR ChatServerIP[16];
				USHORT ChatServerPort=0;
				AcquireSRWLockShared(&srwCLIENT);
				for (auto it = _ClientMap.begin(); it != _ClientMap.end(); it++) {
					if (it->second->Type == dfSERVER_TYPE_GAME) {
						GameServerPort = it->second->Port;
						memcpy(GameServerIP, it->second->IP, 32);
						ServerCnt++;
					}
					else if (it->second->Type == dfSERVER_TYPE_CHAT) {
						ChatServerPort = it->second->Port;
						memcpy(ChatServerIP, it->second->IP, 32);
						ServerCnt++;
					}
				}
				ReleaseSRWLockShared(&srwCLIENT);
				if (ServerCnt == 0) {
					Status = dfLOGIN_STATUS_NOSERVER;
				}

				wsprintf(ID, L"ID_%d", AccountNo);
				wsprintf(Nickname, L"NICK_%d", AccountNo);

				//Packet ���� ������
				CPacket* pSendPacket = CPacket::Alloc();
				MPLoginResLogin(pSendPacket, en_PACKET_CS_LOGIN_RES_LOGIN, AccountNo, Status,
					ID, Nickname, GameServerIP, GameServerPort, ChatServerIP, ChatServerPort);
				//((CLoginServer*)pLoginServer)->SendPacket(LoginSessionID, pSendPacket, NET);
				pSendPacket->Free();
				
				//log
				InterlockedIncrement(&_LoginSuccessTPS);
				SuccessTime = timeGetTime() - SuccessTime;
				InterlockedAdd(&_TotalLoginTime, SuccessTime);
			}
		}
		else {
			ReleaseSRWLockExclusive(&CLoginServer::srwACCOUNT);
		}

	}
}

void CLoginLanServer::OnError(int errorcode, const WCHAR* Err) {
	return;
}
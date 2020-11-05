#include "CLoginServer.h"
#include "CLoginLanServer.h"
CObjectPool<st_ACCOUNT> CLoginServer::_AccountPool;
CObjectPool<st_LOGINSESSION> CLoginServer::_SessionPool;
unordered_map<INT64, st_ACCOUNT*> CLoginServer::_AccountMap;
unordered_map<INT64, st_LOGINSESSION*> CLoginServer::_SessionMap;
SRWLOCK CLoginServer::srwACCOUNT;
SRWLOCK CLoginServer::srwSESSION;

CLoginServer::CLoginServer() {
	InitializeSRWLock(&srwACCOUNT);
	InitializeSRWLock(&srwSESSION);
	_ParameterCnt = 0;
	_DBTokenMiss = 0;
	//DB ����
	InitializeSRWLock(&_srwMYSQL);
	mysql_init(&_conn);
	my_bool reconnect = 1;
	mysql_options(&_conn, MYSQL_OPT_RECONNECT, &reconnect);
	_connection = mysql_real_connect(&_conn, "127.0.0.1", "root", "password", "accountdb", 3306, (char*)NULL, 0);
	mysql_set_character_set(_connection, "utf8");
	_LoginLanServer = new CLoginLanServer((LPVOID)this);
}



void CLoginServer::MPReqNewClientLogin(CPacket* pPacket, WORD Type, INT64 AccountNo, char* Token, INT64 Parameter) {
	*pPacket << Type << AccountNo;
	pPacket->PutData(Token, 64);
	*pPacket << Parameter;
}



void CLoginServer::OnClientJoin(SOCKADDR_IN clientaddr, INT64 SessionID) {
	st_LOGINSESSION* newLoginSession = _SessionPool.Alloc();
	newLoginSession->SessionID = SessionID;
	newLoginSession->AccountNo = -1;
	AcquireSRWLockExclusive(&srwSESSION);
	InsertLoginSession(newLoginSession);
	ReleaseSRWLockExclusive(&srwSESSION);
}

void CLoginServer::OnClientLeave(INT64 SessionID) {
	INT64 AccountNo;
	st_LOGINSESSION* pLoginSession= NULL;
	st_ACCOUNT* pAccount = NULL;
	//SessionMap���� ����
	AcquireSRWLockExclusive(&srwSESSION);
	pLoginSession = FindLoginSession(SessionID);
	if (pLoginSession != NULL) {
		AccountNo = pLoginSession->AccountNo;
		RemoveLoginSession(SessionID);
	}
	else {
		//�־ �ȵ�
		CCrashDump::Crash();
	}
	ReleaseSRWLockExclusive(&srwSESSION);
	_SessionPool.Free(pLoginSession);

	//AccountMap���� ����
	AcquireSRWLockExclusive(&srwACCOUNT);
	pAccount = FindAccount(AccountNo);
	//ã�� ���
	if (pAccount != NULL) {
		//���� ID�� ���� �� ���� ID�� ������ �ٲ��� ���� Account�̹Ƿ� ����
		if (SessionID == pAccount->SessionID) {
			RemoveAccount(AccountNo);
		}
	}
	ReleaseSRWLockExclusive(&srwACCOUNT);
	if (pAccount != NULL)
		_AccountPool.Free(pAccount);
	InterlockedDecrement(&_LoginLanServer->_LoginWaitCount);
}

bool CLoginServer::OnConnectRequest(SOCKADDR_IN clientaddr) {
	return true;
}

void CLoginServer::OnRecv(INT64 SessionID, CPacket* pRecvPacket) {
	//��Ŷ ������ �̻��� ���
	if (pRecvPacket->GetDataSize() != 74) {
		Disconnect(SessionID);
		return;
	}
	WORD Type;
	INT64 AccountNo;
	char Token[64];
	char DBToken[64];
	INT64 Parameter;
	*pRecvPacket >> Type >> AccountNo;
	pRecvPacket->GetData(Token, 64);
	if (Type != en_PACKET_CS_LOGIN_REQ_LOGIN) {
		Disconnect(SessionID);
		return;
	}
	/*
		DB�� ��ȸ�� �ش� Account�� Token�� ���� Token�� ��ġ�ϴ� �� Ȯ��
	*/

	AcquireSRWLockExclusive(&_srwMYSQL);
	char query[1000];
	sprintf_s(query, "SELECT `sessionkey` FROM `v_account` WHERE (`accountno` = \'%lld\')", AccountNo);
	int retval = mysql_query(_connection, query);
	//DB�� ����
	if (retval != 0) {
		CCrashDump::Crash();
	}
	//��� ��������
	MYSQL_RES* sql_result = mysql_use_result(_connection);
	MYSQL_ROW sql_row;
	while ((sql_row = mysql_fetch_row(sql_result)) != NULL) {
		//Token ���ϱ�
		if (sql_row[0] != NULL) {
			if (memcmp(Token, sql_row[0], 64) != 0) {
				//���� ��ġ���� �ʴ� ���
				_DBTokenMiss++;
				//Disconnect(SessionID);
			}
		}
	}
	mysql_free_result(sql_result);
	ReleaseSRWLockExclusive(&_srwMYSQL);

	AcquireSRWLockExclusive(&srwACCOUNT);
	st_ACCOUNT* pAccount = FindAccount(AccountNo);
	//�̹� AccountNo�� �����ϴ� ���
	if (pAccount!=NULL) {
		//�ߺ��α��� ��� x
		pAccount->SessionID = SessionID;
		pAccount->Parameter = InterlockedIncrement64(&_ParameterCnt);
		pAccount->RecvTime = timeGetTime();
		Parameter = pAccount->Parameter;
	}
	//AccountNo�� �������� �ʴ� ���
	else {
		st_ACCOUNT* newAccount = _AccountPool.Alloc();
		newAccount->SessionID = SessionID;
		newAccount->Parameter = InterlockedIncrement64(&_ParameterCnt);
		newAccount->RecvTime = timeGetTime();
		Parameter = newAccount->Parameter;
		InsertAccount(newAccount, AccountNo);
	}
	ReleaseSRWLockExclusive(&srwACCOUNT);

	//LoginSession ����
	AcquireSRWLockExclusive(&srwSESSION);
	st_LOGINSESSION* pLoginSession = FindLoginSession(SessionID);
	if (pLoginSession == NULL) {
		//���� �� ����
		CCrashDump::Crash();
	}
	else {
		pLoginSession->AccountNo = AccountNo;
		memcpy(pLoginSession->Token, Token, 64);
	}
	ReleaseSRWLockExclusive(&srwSESSION);

	//LanServer�� Parameter�� AccountNo�־ ��û�ϱ�
	//SendPacket ���� �� 
	INT64 LanSessionID = 0;
	CPacket* pSendPacket = CPacket::Alloc();
	MPReqNewClientLogin(pSendPacket, en_PACKET_SS_REQ_NEW_CLIENT_LOGIN, AccountNo, Token, Parameter);
	AcquireSRWLockShared(&CLoginLanServer::srwCLIENT);

	for (auto it = CLoginLanServer::_ClientMap.begin(); it != CLoginLanServer::_ClientMap.end();) {
		if (it->second->Type == dfSERVER_TYPE_CHAT) {
			LanSessionID = it->second->SessionID;
			break;
		}
		it++;
	}
	ReleaseSRWLockShared(&CLoginLanServer::srwCLIENT);
	if(LanSessionID !=0)
		_LoginLanServer->SendPacket(LanSessionID, pSendPacket, eLAN);
	pSendPacket->Free();
	InterlockedIncrement(&_LoginLanServer->_LoginWaitCount);
}

void CLoginServer::OnError(int errorcode, const WCHAR* Err) {
	return;
}

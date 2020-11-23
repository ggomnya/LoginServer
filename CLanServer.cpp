#include "CLanServer.h"

CCrashDump _Dump;

LONG CCrashDump::_DumpCount = 0;

CLanServer::CLanServer() {
	_SessionIDCnt = 0;
	_AcceptCount = 0;
	_AcceptTPS = 0;
	_SessionCnt = 0;
	_SendTPS = 0;
	_RecvTPS = 0;
	_DisCount = 0;
}
unsigned int WINAPI CLanServer::WorkerThread(LPVOID lParam) {
	int retval;
	DWORD cbTransferred = 0;
	stSESSION* pSession;
	stOVERLAPPED* Overlapped;
	int iSessionUseSize = 0;
	int iRecvTPS = 0;
	WORD wPacketCount;
	WORD Header;
	while (1) {
		retval = GetQueuedCompletionStatus(_hcp, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&Overlapped, INFINITE);

		//종료 처리
		if (Overlapped == NULL) {
			return 0;
		}
	/*	if (&pSession->RecvOverlapped != Overlapped && &pSession->SendOverlapped != Overlapped) {
			CCrashDump::Crash();
		}*/
		//연결 끊기
		if (cbTransferred == 0) {
			_Disconnect(pSession);
		}
		//Recv, Send 동작
		else {
			//recv인 경우
			//g_RecvCnt++;
			if (Overlapped->Type == RECV) {
				//DebugFunc(pSession, RECVCOM);
				//데이터 받아서 SendQ에 넣은 후 Send하기
				iRecvTPS = 0;
				pSession->RecvQ.MoveRear(cbTransferred);
				while (1) {
					iSessionUseSize = pSession->RecvQ.GetUseSize();
					//헤더 사이즈 이상이 있는지 확인
					if (iSessionUseSize < LANHEADER)
						break;
					//데이터가 있는지 확인
					pSession->RecvQ.Peek((char*)&Header, LANHEADER);
					if (iSessionUseSize < LANHEADER + Header)
						break;
					//pSession->RecvQ.MoveFront(sizeof(Header));
					CPacket* RecvPacket = CPacket::Alloc();
					retval = pSession->RecvQ.Dequeue(RecvPacket->GetBufferPtr() + 3, Header + LANHEADER);
					RecvPacket->MoveWritePos(Header);
					if (retval < Header + LANHEADER) {
						Disconnect(pSession->SessionID);
						RecvPacket->Free();
						break;
					}
					iRecvTPS++;
					OnRecv(pSession->SessionID, RecvPacket);
					//InterlockedIncrement(&pSession->_NewCount);
					RecvPacket->Free();
				}
				InterlockedAdd(&_RecvTPS, iRecvTPS);
				//Recv 요청하기
				RecvPost(pSession);

			}
			//send 완료 경우
			else {
				//DebugFunc(pSession, SENDCOM);
				wPacketCount = pSession->PacketCount;
				for (int i = 0; i < wPacketCount; i++) {
					pSession->PacketArray[i]->Free();
					//InterlockedIncrement(&pSession->_DeleteCount);
				}
				pSession->PacketCount = 0;
				InterlockedExchange(&(pSession->SendFlag), TRUE);
				if (pSession->SendQ.Size() > 0) {
					SendPost(pSession);
				}
				//if (pSession->SendQ.GetUseSize() > 0) {
				//	SendPost(pSession);
				//}
			}
		}
		ReleaseSession(pSession);
	}
}

unsigned int WINAPI CLanServer::AcceptThread(LPVOID lParam) {
	stSESSION* pSession;
	while (1) {
		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		memset(&clientaddr, 0, sizeof(clientaddr));

		SOCKET client_sock = accept(_Listen_sock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET) {
			wprintf(L"accept() %d\n", WSAGetLastError());
			return 0;
		}
		_AcceptCount++;
		_AcceptTPS++;
		OnConnectRequest(clientaddr);
		//빈 세션이 없을 경우 연결 끊기
		if (_IndexSession.Size() == 0) {
			wprintf(L"accept()_MaxSession\n");
			LINGER optval;
			optval.l_linger = 0;
			optval.l_onoff = 1;
			setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
			closesocket(client_sock);
			continue;
		}
		int curIdx;
		_IndexSession.Pop(&curIdx);
		pSession = &_SessionList[curIdx];
		//_IndexSession.Dequeue(&curIdx);
		pSession->sock = client_sock;
		pSession->clientaddr = clientaddr;
		pSession->SessionID = ++_SessionIDCnt;
		pSession->SessionIndex = curIdx;
		pSession->SessionID |= (pSession->SessionIndex << 48);
		memset(&pSession->RecvOverlapped, 0, sizeof(pSession->RecvOverlapped));
		memset(&pSession->SendOverlapped, 0, sizeof(pSession->SendOverlapped));
		pSession->RecvOverlapped.Type = RECV;
		pSession->SendOverlapped.Type = SEND;
		pSession->RecvOverlapped.SessionID = pSession->SessionID;
		pSession->SendOverlapped.SessionID = pSession->SessionID;
		pSession->IOCount = 1;
		pSession->RecvQ.ClearBuffer();
		pSession->SendQ.Clear();
		pSession->ReleaseFlag = TRUE;
		pSession->SendFlag = TRUE;
		pSession->PacketCount = 0;
		InterlockedIncrement(&_SessionCnt);
		//wprintf(L"Accept session %s:%d\n", szClientIP, ntohs(pSession->clientaddr.sin_port));
		CreateIoCompletionPort((HANDLE)pSession->sock, _hcp, (ULONG_PTR)pSession, 0);
		OnClientJoin(pSession->clientaddr, pSession->SessionID);
		RecvPost(pSession);
		ReleaseSession(pSession);

	}
}

bool CLanServer::Start(ULONG OpenIP, USHORT Port, int NumWorkerthread, int NumIOCP, int MaxSession, 
	int iBlockNum, bool bPlacementNew, bool Restart) {
	int retval;
	if (!Restart) {
		_wsetlocale(LC_ALL, L"Korean");
		timeBeginPeriod(1);
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			wprintf(L"WSAStartup() %d\n", WSAGetLastError());
			return false;
		}

		//IOCP 생성
		_hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, NumIOCP);
		if (_hcp == NULL) {
			wprintf(L"CreateIoCompletionPort() %d\n", GetLastError());
			return 1;
		}

		_MaxSession = MaxSession;
		_SessionList = new stSESSION[MaxSession];
		for (int i = 0; i < _MaxSession; i++) {
			_SessionList[i].SessionID = -1;
		}

		for (int i = _MaxSession - 1; i >= 0; i--)
			_IndexSession.Push(i);

		//PacketBuffer 초기화
		CPacket::Initial(iBlockNum, bPlacementNew);
	}
	_Listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (_Listen_sock == INVALID_SOCKET) {
		wprintf(L"socket() %d\n", WSAGetLastError());
		return false;
	}


	/*int optval = 0;
	setsockopt(_Listen_sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval));*/

	SOCKADDR_IN serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(OpenIP);
	serveraddr.sin_port = htons(Port);

	retval = bind(_Listen_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR) {
		wprintf(L"bind() %d\n", WSAGetLastError());
		return false;
	}

	retval = listen(_Listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR) {
		wprintf(L"listen() %d\n", WSAGetLastError()); 
	}
	_NumThread = NumWorkerthread;
	_hWorkerThread = new HANDLE[NumWorkerthread];
	for (int i = 0; i < NumWorkerthread; i++) {
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadFunc, this, 0, NULL);
		if (_hWorkerThread[i] == NULL) return 1;
	}
	_hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThreadFunc, this, 0, NULL);
}

void CLanServer::Stop() {
	for (int i = 0; i < _NumThread; i++)
		PostQueuedCompletionStatus(_hcp, 0, NULL, NULL);
	closesocket(_Listen_sock);
	for (int i = 0; i < _NumThread; i++)
		CloseHandle(_hWorkerThread[i]);
	CloseHandle(_hAcceptThread);

}

int CLanServer::GetClientCount() {
	return _SessionCnt;
}

bool CLanServer::_Disconnect(stSESSION* pSession) {
	int retval = InterlockedExchange(&pSession->sock, INVALID_SOCKET);
	if (retval != INVALID_SOCKET) {
		pSession->closeSock = retval;
		CancelIoEx((HANDLE)pSession->closeSock, NULL);
		/*LINGER optval;
		optval.l_linger = 0;
		optval.l_onoff = 1;
		setsockopt(retval, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
		closesocket(retval);*/
		InterlockedIncrement((LONG*)&_DisCount);
		return true;
	}
	return false;
}


bool CLanServer::Disconnect(INT64 SessionID) {
	stSESSION* pSession = FindSession(SessionID);
	if (pSession == NULL)
		return false;
	int retval = InterlockedExchange(&pSession->sock, INVALID_SOCKET);
	if (retval != INVALID_SOCKET) {
		pSession->closeSock = retval;
		CancelIoEx((HANDLE)pSession->closeSock, NULL);
		/*WCHAR szClientIP[16];
		LINGER optval;
		optval.l_linger = 0;
		optval.l_onoff = 1;
		setsockopt(retval, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
		closesocket(retval);*/
		InterlockedIncrement((LONG*)&_DisCount);
		ReleaseSession(pSession);
		return true;
	}
	ReleaseSession(pSession);
	return true;
}

bool CLanServer::_SendPacket(stSESSION* pSession, CPacket* pSendPacket, int type) {
	pSendPacket->AddRef();
	if (type == eLAN)
		pSendPacket->SetHeader_2();
	else if (type == eNET) {
		pSendPacket->SetHeader_5();
		pSendPacket->Encode();
	}
	pSession->SendQ.Enqueue(pSendPacket);
	SendPost(pSession);
	return true;
}


bool CLanServer::SendPacket(INT64 SessionID, CPacket* pSendPacket,int type) {
	stSESSION* pSession = FindSession(SessionID);
	if (pSession == NULL)
		return false;
	pSendPacket->AddRef();
	if(type == eLAN)
		pSendPacket->SetHeader_2();
	else if (type == eNET) {
		pSendPacket->SetHeader_5();
		pSendPacket->Encode();
	}
	pSession->SendQ.Enqueue(pSendPacket);
	SendPost(pSession);
	ReleaseSession(pSession);
	return true;
	//int retval = pSession->SendQ.Enqueue((char*)&pSendPacket, 8);
	/*if (retval < 8) {
		wprintf(L"SendPacket\n");
		Disconnect(SessionID);
		ReleaseSession(pSession);
		return false;
	}
	else {
		SendPost(pSession);
		ReleaseSession(pSession);
		return true;
	}*/
}

stSESSION* CLanServer::FindSession(INT64 SessionID) {
	stSESSION* pSession = NULL;
	WORD SessionIdx = SessionID >> 48;
	/*for (int i = 0; i < _MaxSession; i++) {
		if (_SessionList[i].SessionID == SessionID) {
			pSession = &_SessionList[i];
			break;
		}
	}*/
	if (_SessionList[SessionIdx].SessionID == SessionID) {
		pSession = &_SessionList[SessionIdx];
	}
	if (pSession == NULL)
		return pSession;
	int retval = InterlockedIncrement64(&pSession->IOCount);
	if (retval == 1) {
		ReleaseSession(pSession);
	}
	else {
		if (pSession->SessionID != SessionID) {
			ReleaseSession(pSession);
		}
		else {
			return pSession;
		}
	}
	return NULL;
	
}


void CLanServer::ReleaseSession(stSESSION* pSession) {
	int retval = InterlockedDecrement64(&pSession->IOCount);
	if (retval == 0) {
		if (pSession->ReleaseFlag == TRUE)
			Release(pSession);
	}
}

void CLanServer::Release(stSESSION* pSession) {
	stRELEASE temp;
	INT64 SessionID = pSession->SessionID;
	if (InterlockedCompareExchange128(&pSession->IOCount, (LONG64)FALSE, (LONG64)0, (LONG64*)&temp)) {
		pSession->SessionID = -1;
		InterlockedDecrement(&_SessionCnt);
		while (pSession->SendQ.Size() > 0 || pSession->PacketCount > 0) {
			while (pSession->SendQ.Size() > 0) {
				if (pSession->PacketCount >= dfPACKETNUM)
					break;
				pSession->SendQ.Dequeue(&(pSession->PacketArray[pSession->PacketCount]));
				if (pSession->PacketArray[pSession->PacketCount] != NULL)
					pSession->PacketCount++;
			}
			for (int j = 0; j < pSession->PacketCount; j++) {
				pSession->PacketArray[j]->Free();
			}
			pSession->PacketCount = 0;
		}
		LINGER optval;
		optval.l_linger = 0;
		optval.l_onoff = 1;
		setsockopt(pSession->closeSock, SOL_SOCKET, SO_LINGER, (char*)&optval, sizeof(optval));
		closesocket(pSession->closeSock);
		//_Disconnect(pSession);
		_IndexSession.Push(pSession->SessionIndex);
		OnClientLeave(SessionID);
	}
	
}

void CLanServer::RecvPost(stSESSION* pSession) {
	memset(&pSession->RecvOverlapped, 0, sizeof(pSession->RecvOverlapped.Overlapped));
	DWORD recvbyte = 0;
	DWORD lpFlags = 0;
	//DebugFunc(pSession, RECVPOST);
	//Recv 요청하기
	char* rearBufPtr = pSession->RecvQ.GetRearBufferPtr();
	char* bufPtr = pSession->RecvQ.GetBufferPtr();
	if ((rearBufPtr != bufPtr) && (pSession->RecvQ.GetFrontBufferPtr() <= rearBufPtr)) {
		int iDirEnqSize = pSession->RecvQ.DirectEnqueueSize();
		WSABUF recvbuf[2];
		recvbuf[0].buf = rearBufPtr;
		recvbuf[0].len = iDirEnqSize;
		recvbuf[1].buf = bufPtr;
		recvbuf[1].len = pSession->RecvQ.GetFreeSize() - iDirEnqSize;
		//pSession->recvLen[0] = recvbuf[0].len;
		//pSession->recvLen[1] = recvbuf[1].len;
		InterlockedIncrement64(&(pSession->IOCount));
		//pSession->recvsock = pSession->sock;
		int retval = WSARecv(pSession->sock, recvbuf, 2, &recvbyte, &lpFlags, (WSAOVERLAPPED*)&pSession->RecvOverlapped, NULL);
		if (retval == SOCKET_ERROR) {
			if (WSAGetLastError() != ERROR_IO_PENDING) {
				int WSA = WSAGetLastError();
				//pSession->recvErr = WSA;
				_Disconnect(pSession);
				ReleaseSession(pSession);
			}
		}
	}
	else {
		WSABUF recvbuf;
		recvbuf.buf = rearBufPtr;
		recvbuf.len = pSession->RecvQ.DirectEnqueueSize();
		InterlockedIncrement64(&(pSession->IOCount));
		int retval = WSARecv(pSession->sock, &recvbuf, 1, &recvbyte, &lpFlags, (WSAOVERLAPPED*)&pSession->RecvOverlapped, NULL);
		if (retval == SOCKET_ERROR) {
			if (WSAGetLastError() != ERROR_IO_PENDING) {
				int WSA = WSAGetLastError();
				//CCrashDump::Crash();
				_Disconnect(pSession);
				ReleaseSession(pSession);
			}
		}
	}
}

void CLanServer::SendPost(stSESSION* pSession) {
	int retval = InterlockedExchange(&(pSession->SendFlag), FALSE);
	if (retval == FALSE)
		return;
	if (pSession->SendQ.Size() <= 0) {
		InterlockedExchange(&(pSession->SendFlag), TRUE);
		return;
	}
	//DebugFunc(pSession, SENDPOST);
	memset(&pSession->SendOverlapped, 0, sizeof(pSession->SendOverlapped.Overlapped));
	DWORD sendbyte = 0;
	DWORD lpFlags = 0;
	WSABUF sendbuf[dfPACKETNUM];
	DWORD i = 0;
	while (pSession->SendQ.Size() > 0) {
		if (i >= dfPACKETNUM)
			break;
		pSession->SendQ.Dequeue(&(pSession->PacketArray[i]));
		sendbuf[i].buf = pSession->PacketArray[i]->GetHeaderPtr();
		sendbuf[i].len = pSession->PacketArray[i]->GetDataSize() + pSession->PacketArray[i]->GetHeaderSize();
		i++;
	}
	pSession->PacketCount = i;
	/*if (i > _SendCount)
		_SendCount = i;*/
	InterlockedAdd(&_SendTPS, i);
	InterlockedIncrement64(&(pSession->IOCount));
	retval = WSASend(pSession->sock, sendbuf, pSession->PacketCount, &sendbyte, lpFlags, (WSAOVERLAPPED*)&pSession->SendOverlapped, NULL);
	if (retval == SOCKET_ERROR) {
		if (WSAGetLastError() != ERROR_IO_PENDING) {
			int WSA = WSAGetLastError();
			//CCrashDump::Crash();
			_Disconnect(pSession);
			ReleaseSession(pSession);
		}
	}
}

//void CLanServer::DebugFunc(stSESSION* pSession, int FuncNum) {
//	return;
//	int idx = InterlockedIncrement(&pSession->debugCnt);
//	idx %= DEBUGNUM;
//	pSession->debug[idx].FuncNum = FuncNum;
//	pSession->debug[idx].IOCount = pSession->IOCount;
//	pSession->debug[idx].SessionID = pSession->SessionID;
//	pSession->debug[idx].ThreadID = GetCurrentThreadId();
//	pSession->debug[idx].Sock = pSession->sock;
//
//}

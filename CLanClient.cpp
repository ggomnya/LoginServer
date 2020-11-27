#include "CLanClient.h"

extern INT64 PacketNum;

CLanClient::CLanClient() {
	_SendTPS = 0;
	_RecvTPS = 0;
	_DisCount = 0;
	_ConnectFail = 0;
	_ConnectSuccess = 0;
}
unsigned int WINAPI CLanClient::WorkerThread(LPVOID lParam) {
	int retval;
	DWORD cbTransferred = 0;
	stSESSION* pSession;
	stOVERLAPPED* Overlapped;
	int iSessionUseSize = 0;
	WORD wPacketCount;
	WORD Header;
	while (1) {
		retval = GetQueuedCompletionStatus(_hcp, &cbTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&Overlapped, INFINITE);

		//���� ó��
		if (Overlapped == NULL) {
			return 0;
		}

		//���� ����
		if (cbTransferred == 0) {
			_Disconnect(pSession);
		}
		//Recv, Send ����
		else {
			//recv�� ���
			if (Overlapped->Type == RECV) {
				//������ �޾Ƽ� SendQ�� ���� �� Send�ϱ�
				pSession->RecvQ.MoveRear(cbTransferred);
				while (1) {
					//��� ������ �̻��� �ִ��� Ȯ��
					iSessionUseSize = pSession->RecvQ.GetUseSize();
					if (iSessionUseSize < 2)
						break;
					//�����Ͱ� �ִ��� Ȯ��
					pSession->RecvQ.Peek((char*)&Header, LANHEADER);
					if (iSessionUseSize < LANHEADER + Header)
						break;
					CPacket* RecvPacket = CPacket::Alloc();
					retval = pSession->RecvQ.Dequeue(RecvPacket->GetBufferPtr() + 3, Header+ LANHEADER);
					RecvPacket->MoveWritePos(Header);
					if (retval < Header + LANHEADER) {
						_Disconnect(pSession);
						RecvPacket->Free();
						break;
					}
					_RecvTPS++;
					OnRecv(pSession->SessionID, RecvPacket);
					RecvPacket->Free();
				}

				//Recv ��û�ϱ�
				RecvPost(pSession);

			}
			//send �Ϸ� ���
			else if(Overlapped->Type == SEND) {
				wPacketCount = pSession->PacketCount;
				for (int i = 0; i < wPacketCount; i++) {
					pSession->PacketArray[i]->Free();
				}
				pSession->PacketCount = 0;
				InterlockedExchange(&(pSession->SendFlag), TRUE);
				if (pSession->SendQ.Size() > 0) {
					SendPost(pSession);
				}
			}
			else if (Overlapped->Type == CONNECT) {
				//IOCount�� ���� �ʿ䰡 ����.
				Connect();
				continue;
			}
		}
		ReleaseSession(pSession);
	}
}

bool CLanClient::Start(WCHAR* ServerIP, USHORT Port, int NumWorkerthread, int NumIOCP, int iBlockNum, bool bPlacementNew) {
	int retval;
	
	_wsetlocale(LC_ALL, L"Korean");
	timeBeginPeriod(1);
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		wprintf(L"WSAStartup() %d\n", WSAGetLastError());
		return false;
	}

	//IOCP ����
	_hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, NumIOCP);
	if (_hcp == NULL) {
		wprintf(L"CreateIoCompletionPort() %d\n", GetLastError());
		return false;
	}


	//PacketBuffer �ʱ�ȭ
	CPacket::Initial(iBlockNum, bPlacementNew);
	//_ClientSession �ʱ�ȭ

	memset(&_ClientSession.RecvOverlapped, 0, sizeof(_ClientSession.RecvOverlapped));
	memset(&_ClientSession.SendOverlapped, 0, sizeof(_ClientSession.SendOverlapped));
	memset(&_ClientSession.ConnectOverlapped, 0, sizeof(_ClientSession.ConnectOverlapped));
	_ClientSession.RecvOverlapped.Type = RECV;
	_ClientSession.SendOverlapped.Type = SEND;
	_ClientSession.ConnectOverlapped.Type = CONNECT;
	_ClientSession.SessionID = -1;

	memset(&_ClientSession.serveraddr, 0, sizeof(_ClientSession.serveraddr));
	_ClientSession.serveraddr.sin_family = AF_INET;
	if (ServerIP == NULL) {
		_ClientSession.serveraddr.sin_addr.s_addr = htons(INADDR_ANY);
	}
	else {
		InetPton(AF_INET, ServerIP, &_ClientSession.serveraddr.sin_addr.s_addr);
	}
	_ClientSession.serveraddr.sin_port = htons(Port);

	_NumThread = NumWorkerthread;
	_hWorkerThread = new HANDLE[NumWorkerthread];
	for (int i = 0; i < NumWorkerthread; i++) {
		_hWorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThreadFunc, this, 0, NULL);
		if (_hWorkerThread[i] == NULL) return false;
	}
	PostQueuedCompletionStatus(_hcp, sizeof(stSESSION*), (ULONG_PTR)&_ClientSession, (WSAOVERLAPPED*)&_ClientSession.ConnectOverlapped);
	return true;
	/*int optval = 0;
	setsockopt(_Listen_sock, SOL_SOCKET, SO_SNDBUF, (char*)&optval, sizeof(optval));*/
	
}

bool CLanClient::Connect() {
	int retval;
	_ClientSession.RecvQ.ClearBuffer();
	_ClientSession.SendQ.Clear();
	_ClientSession.ReleaseFlag = TRUE;
	_ClientSession.SendFlag = TRUE;
	_ClientSession.PacketCount = 0;
	_ClientSession.IOCount = 1;

	FD_SET wset;
	FD_ZERO(&wset);
	timeval time;
	time = { 0, 300 * 1000 };
	while (1) {
		_ClientSession.sock = socket(AF_INET, SOCK_STREAM, 0);
		if (_ClientSession.sock == INVALID_SOCKET) {
			wprintf(L"socket() %d\n", WSAGetLastError());
			return false;
		}
		ULONG on = 1;
		retval = ioctlsocket(_ClientSession.sock, FIONBIO, &on);
		if (retval == SOCKET_ERROR) {
			wprintf(L"ioctlsocket() %d\n", WSAGetLastError());
			return false;
		}
		retval = connect(_ClientSession.sock, (SOCKADDR*)&_ClientSession.serveraddr, sizeof(_ClientSession.serveraddr));
		if (retval == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				wprintf(L"connect() %d\n", WSAGetLastError());
				return false;
			}
		}
		FD_SET(_ClientSession.sock, &wset);
		retval = select(0, NULL, &wset, NULL, &time);
		if (retval == SOCKET_ERROR) {
			wprintf(L"select() %d\n", WSAGetLastError());
			return false;
		}
		if (retval == 0) {
			closesocket(_ClientSession.sock);
			_ConnectFail++;
			/*if (_ConnectFail >= 100)
				return false;*/
		}
		else {
			_ClientSession.SessionID = ++_SessionIDCnt;
			_ConnectFail = 0;
			_ConnectSuccess++;
			break;
		}
	}
	CreateIoCompletionPort((HANDLE)_ClientSession.sock, _hcp, (ULONG_PTR)&_ClientSession, 0);
	OnEnterJoinServer(_ClientSession.SessionID);
	RecvPost(&_ClientSession);
	ReleaseSession(&_ClientSession);

}

void CLanClient::ReConnect() {
	PostQueuedCompletionStatus(_hcp, sizeof(stSESSION*), (ULONG_PTR)&_ClientSession, (WSAOVERLAPPED*)&_ClientSession.ConnectOverlapped);
}

bool CLanClient::_Disconnect(stSESSION* pSession) {
	int retval = InterlockedExchange(&pSession->sock, INVALID_SOCKET);
	if (retval != INVALID_SOCKET) {
		pSession->closeSock = retval;
		CancelIoEx((HANDLE)pSession->closeSock, NULL);
		InterlockedIncrement((LONG*)&_DisCount);
		return true;
	}
	return false;
}


bool CLanClient::Disconnect(INT64 SessionID) {
	InterlockedIncrement64(&_ClientSession.IOCount);
	stSESSION* pSession = &_ClientSession;
	if (pSession == NULL)
		return false;
	int retval = InterlockedExchange(&pSession->sock, INVALID_SOCKET);
	if (retval != INVALID_SOCKET) {
		pSession->closeSock = retval;
		CancelIoEx((HANDLE)pSession->closeSock, NULL);
		InterlockedIncrement((LONG*)&_DisCount);
		ReleaseSession(pSession);
		return true;
	}
	ReleaseSession(pSession);
	return true;
}

bool CLanClient::SendPacket(CPacket* pSendPacket) {
	if (_ClientSession.SessionID == -1)
		return true;
	InterlockedIncrement64(&_ClientSession.IOCount);
	pSendPacket->AddRef();
	pSendPacket->SetHeader_2();
	_ClientSession.SendQ.Enqueue(pSendPacket);
	SendPost(&_ClientSession);
	ReleaseSession(&_ClientSession);
	return true;
}

void CLanClient::ReleaseSession(stSESSION* pSession) {
	int retval = InterlockedDecrement64(&pSession->IOCount);
	if (retval == 0) {
		if (pSession->ReleaseFlag == TRUE)
			Release(pSession);
	}
	else if (retval < 0) {
		CCrashDump::Crash();
	}
}

void CLanClient::Release(stSESSION* pSession) {
	stRELEASE temp;
	INT64 SessionID = pSession->SessionID;
	if (InterlockedCompareExchange128(&pSession->IOCount, (LONG64)FALSE, (LONG64)0, (LONG64*)&temp)) {
		pSession->SessionID = -1;
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
		OnLeaveServer(SessionID);
	}
	
}

void CLanClient::RecvPost(stSESSION* pSession) {
	memset(&pSession->RecvOverlapped, 0, sizeof(pSession->RecvOverlapped.Overlapped));
	DWORD recvbyte = 0;
	DWORD lpFlags = 0;
	char* rearBufPtr = pSession->RecvQ.GetRearBufferPtr();
	char* bufPtr = pSession->RecvQ.GetBufferPtr();
	//Recv ��û�ϱ�
	if ((rearBufPtr != bufPtr) && (pSession->RecvQ.GetFrontBufferPtr() <= rearBufPtr)) {
		int iDirEnqSize = pSession->RecvQ.DirectEnqueueSize();
		WSABUF recvbuf[2];
		recvbuf[0].buf = rearBufPtr;
		recvbuf[0].len = iDirEnqSize;
		recvbuf[1].buf = bufPtr;
		recvbuf[1].len = pSession->RecvQ.GetFreeSize() - iDirEnqSize;
		InterlockedIncrement64(&(pSession->IOCount));
		int retval = WSARecv(pSession->sock, recvbuf, 2, &recvbyte, &lpFlags, (WSAOVERLAPPED*)&pSession->RecvOverlapped, NULL);
		if (retval == SOCKET_ERROR) {
			if (WSAGetLastError() != ERROR_IO_PENDING) {
				int WSA = WSAGetLastError();
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
				_Disconnect(pSession);
				ReleaseSession(pSession);
			}
		}
	}
}

void CLanClient::SendPost(stSESSION* pSession) {
	int retval = InterlockedExchange(&(pSession->SendFlag), FALSE);
	if (retval == FALSE)
		return;
	if (pSession->SendQ.Size() <= 0) {
		InterlockedExchange(&(pSession->SendFlag), TRUE);
		return;
	}
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
	_SendTPS += i;
	InterlockedIncrement64(&(pSession->IOCount));
	retval = WSASend(pSession->sock, sendbuf, pSession->PacketCount, &sendbyte, lpFlags, (WSAOVERLAPPED*)&pSession->SendOverlapped, NULL);
	if (retval == SOCKET_ERROR) {
		if (WSAGetLastError() != ERROR_IO_PENDING) {
			int WSA = WSAGetLastError();
			_Disconnect(pSession);
			ReleaseSession(pSession);
		}
	}
}



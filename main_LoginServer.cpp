#include "CLoginServer.h"
#include "CLoginLanServer.h"
#include "textparser.h"
#include <conio.h>
#include "CCpuUsage.h"
#include "CProcessUsage.h"

WCHAR ChatServerIP[16];
USHORT ChatServerPort;
WCHAR GameServerIP[16];
USHORT GameServerPort;

int wmain() {
	CCpuUsage CpuUsage;
	CProcessUsage ProcessUsage;
	CINIParse Parse;
	WCHAR NetServerIP[16];
	USHORT NetServerPort;
	DWORD NetServerThreadNum;
	DWORD NetServerIOCPNum;
	DWORD NetServerMaxSession;
	WCHAR LanServerIP[16];
	USHORT LanServerPort;
	DWORD LanServerThreadNum;
	DWORD LanServerIOCPNum;
	DWORD LanServerMaxSession;
	Parse.LoadFile(L"LoginServer_Config.ini");

	Parse.GetValue(L"IP", NetServerIP);
	Parse.GetValue(L"PORT", (DWORD*)&NetServerPort);
	Parse.GetValue(L"THREAD_NUMBER", &NetServerThreadNum);
	Parse.GetValue(L"IOCP_NUMBER", &NetServerIOCPNum);
	Parse.GetValue(L"MAX_SESSION", &NetServerMaxSession);

	Parse.GetValue(L"IP", LanServerIP);
	Parse.GetValue(L"PORT", (DWORD*)&LanServerPort);
	Parse.GetValue(L"THREAD_NUMBER", &LanServerThreadNum);
	Parse.GetValue(L"IOCP_NUMBER", &LanServerIOCPNum);
	Parse.GetValue(L"MAX_SESSION", &LanServerMaxSession);

	Parse.GetValue(L"CHAT_IP", ChatServerIP);
	Parse.GetValue(L"CHAT_PORT", (DWORD*)&ChatServerPort);

	Parse.GetValue(L"GAME_IP", GameServerIP);
	Parse.GetValue(L"GAME_PORT", (DWORD*)&GameServerPort);

	CLoginServer* server = new CLoginServer;
	server->Start(INADDR_ANY, NetServerPort, NetServerThreadNum, NetServerIOCPNum, NetServerMaxSession);
	server->_LoginLanServer->Start(INADDR_ANY, LanServerPort, LanServerThreadNum, LanServerIOCPNum, LanServerMaxSession);
	DWORD curTime = timeGetTime();
	while (1) {
		//1초마다 List 현황 출력
		if (timeGetTime() - curTime >= 1000) {
			wprintf(L"================LoginServer================\n");
			wprintf(L"[Session List Size: %d]\n", server->GetClientCount());
			wprintf(L"[Accept Count: %d]\n", server->_AcceptCount);
			wprintf(L"[AcceptTPS: %d]\n", server->_AcceptTPS);
			wprintf(L"[Disconnect Count: %d]\n", server->_DisCount);
			wprintf(L"[Packet Alloc Count: %lld]\n", CPacket::GetAllocCount());
			wprintf(L"[Send TPS: %d]\n", server->_SendTPS);
			wprintf(L"[Recv TPS: %d]\n", server->_RecvTPS);
			wprintf(L"[LoginSessionPool Alloc Count: %lld]\n", server->GetSessionPoolAlloc());
			wprintf(L"[LoginSession Count: %lld]\n", server->GetSessionCount());
			wprintf(L"[AccountPool Alloc Count: %lld]\n", server->GetAccountPoolAlloc());
			wprintf(L"[Account Count: %lld]\n", server->GetAccountCount());
			wprintf(L"================LoginLanServer=============\n");
			wprintf(L"[Lan Session List Size: %d]\n", server->_LoginLanServer->GetClientCount());
			wprintf(L"[Accept Count: %d]\n", server->_LoginLanServer->_AcceptCount);
			wprintf(L"[Disconnect Count: %d]\n", server->_LoginLanServer->_DisCount);
			wprintf(L"[Lan Send TPS: %d]\n", server->_LoginLanServer->_SendTPS);
			wprintf(L"[Lan Recv TPS: %d]\n", server->_LoginLanServer->_RecvTPS);
			wprintf(L"===========================================\n");
			wprintf(L"LoginSuccessTPS: %d]\n", server->_LoginLanServer->_LoginSuccessTPS);
			wprintf(L"LoginWaitCount: %d]\n", server->_LoginLanServer->_LoginWaitCount);
			if (server->_LoginLanServer->_LoginSuccessTPS > 0)
				wprintf(L"Avg LoginTime: %dms]\n", server->_LoginLanServer->_TotalLoginTime / server->_LoginLanServer->_LoginSuccessTPS);
			wprintf(L"\n");

			CpuUsage.UpdateCpuTime();
			ProcessUsage.UpdateProcessTime();
			CpuUsage.PrintCPUInfo();
			ProcessUsage.PrintProcessInfo();
			wprintf(L"\n\n");

			server->_AcceptTPS = 0;
			server->_SendTPS = 0;
			server->_RecvTPS = 0;
			server->_LoginLanServer->_SendTPS = 0;
			server->_LoginLanServer->_RecvTPS = 0;
			server->_LoginLanServer->_LoginSuccessTPS = 0;
			server->_LoginLanServer->_TotalLoginTime = 0;
			curTime = timeGetTime();
		}
		if (_kbhit()) {
			WCHAR cmd = _getch();
			if (cmd == L'q' || cmd == L'Q')
				CCrashDump::Crash();
		}
		Sleep(10);
	}
}
#pragma once
#include <unordered_set>
#include "CCrashDump.h"
#include "CDBConnector.h"
#include "mysql/include/mysql.h"
#include "mysql/include/errmsg.h"
#pragma comment(lib, "mysql/lib/libmysql.lib")
using namespace std;

class CDBConnector_TLS {
private:
	unordered_set<CDBConnector*> _DBConnect;
	char _IP[16];
	char _ID[16];
	char _pw[16];
	LONG _TlsIndex;
public:
	CDBConnector_TLS(char* IP, char* ID, char* pw) {
		memcpy(_IP, IP, 16);
		memcpy(_ID, ID, 16);
		memcpy(_pw, pw, 16);
		_TlsIndex = TlsAlloc();
		if (_TlsIndex == TLS_OUT_OF_INDEXES) {
			CCrashDump::Crash();
		}
	}

	void Connect() {
		CDBConnector* newConnector = new CDBConnector(_IP, _ID, _pw);
		_DBConnect.insert(newConnector);
		TlsSetValue(_TlsIndex, newConnector);
		newConnector->Connect();
	}

	void Query(char* query) {
		CDBConnector* pConnector = (CDBConnector*)TlsGetValue(_TlsIndex);
		if (pConnector == NULL) {
			Connect();
			pConnector = (CDBConnector*)TlsGetValue(_TlsIndex);
		}
		pConnector->Query(query);
	}

	MYSQL_ROW Fetch() {
		CDBConnector* pConnector = (CDBConnector*)TlsGetValue(_TlsIndex);
		if (pConnector == NULL)
			CCrashDump::Crash();
		return pConnector->Fetch();
	}

	void FreeResult() {
		CDBConnector* pConnector = (CDBConnector*)TlsGetValue(_TlsIndex);
		pConnector->FreeResult();
	}

	~CDBConnector_TLS() {
		for (auto it = _DBConnect.begin(); it != _DBConnect.end();) {
			delete *it;
			it = _DBConnect.erase(it);
		}
	}
};
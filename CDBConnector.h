#pragma once
#include "mysql/include/mysql.h"
#include "mysql/include/errmsg.h"
#pragma comment(lib, "mysql/lib/libmysql.lib")

class CDBConnector {
	MYSQL _conn;
	char _IP[16];
	char _ID[16];
	char _pw[16];
	MYSQL_RES* _sql_result;
public:
	CDBConnector(char* IP, char* ID, char* pw) {
		mysql_init(&_conn);
		memcpy(_IP, IP, 16);
		memcpy(_ID, ID, 16);
		memcpy(_pw, pw, 16);
	}
	void Connect() {
		my_bool reconnect = 1;
		mysql_options(&_conn, MYSQL_OPT_RECONNECT, &reconnect);
		mysql_real_connect(&_conn, _IP, _ID, _pw, "accountdb", 3306, (char*)NULL, 0);
		mysql_set_character_set(&_conn, "utf8");
	}
	void Query(char* query) {
		int retval = mysql_query(&_conn, query);
		_sql_result = mysql_use_result(&_conn);
	}
	MYSQL_ROW Fetch() {
		MYSQL_ROW sql_row = mysql_fetch_row(_sql_result);
		return sql_row;
	}

	void FreeResult() {
		mysql_free_result(_sql_result);
	}
	
	~CDBConnector() {
		mysql_close(&_conn);
	}

};
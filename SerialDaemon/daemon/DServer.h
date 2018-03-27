#ifndef D_SERVER_H
#define D_SERVER_H

#include <stdint.h>
#include <list>
#include <iostream>
#include <fstream>
#include <string.h>

class DClient;

class DServer
{
public :
	static void CreateAndStartDaemonServer(uint16_t p_port, int argc, char** argv);

	virtual ~DServer();

	void handleDisconnect();
	bool halt(std::string &p_cause);

	std::ofstream &logFile() {return _logFile;}

private :
	DServer(uint16_t p_port);
	bool startServer();
	void eventLoop();

	uint16_t            _port;
	int                 _serverSocketFD;
	std::list<DClient*> _clients;

	std::ofstream       _logFile;
};

#endif // D_SERVER_H

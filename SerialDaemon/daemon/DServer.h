#ifndef D_SERVER_H
#define D_SERVER_H

#include <stdint.h>
#include <list>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <mutex>

class DClient;
class DConnexion;

class DServer
{
public :
	static void CreateAndStartDaemonServer(uint16_t p_port, int argc, char** argv);

	virtual ~DServer();

	void handleDisconnect();
	void handleConnexionClosed();
	void connectClient(DClient *p_client);
	bool halt(std::string &p_cause);

	std::ofstream &logFile() {return _logFile;}
	std::string getStatus();

private :
	DServer(uint16_t p_port);
	bool startServer();
	void openLog();
	void eventLoop();

	uint16_t                                _port;
	int                                     _serverSocketFD;
	std::list<std::unique_ptr<DClient>>     _clients;
	std::list<std::unique_ptr<DConnexion>>  _connexions;
	std::mutex                              _clientMutex;
	std::mutex                              _connexionMutex;

	std::ofstream                           _logFile;
};

#endif // D_SERVER_H


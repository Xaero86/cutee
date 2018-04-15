#ifndef D_SERVER_H
#define D_SERVER_H

#include <stdint.h>

#include <list>
#include <iostream>
#include <fstream>
#include <string>

class DClient;
class DConnexion;

class DServer
{
public :
	static void CreateAndStartDaemonServer(uint16_t p_port, int argc, char** argv);
	static void SignalHandler(int p_signo);
	static DServer *G_ServerInstance;

	virtual ~DServer();

	void handleDisconnect();
	void handleConnexionClosed();
	void connectClient(DClient *p_client);
	void halt();

	std::ofstream &logFile() {return _logFile;}
	std::string getStatus();

private :
	DServer(uint16_t p_port);
	bool startServer();
	void openLog();
	void eventLoop();

	uint16_t                      _port;
	int                           _serverSocketFD;
	std::list<DClient*>           _clients;
	std::list<DConnexion*>        _connexions;
	pthread_mutex_t               _clientMutex;
	pthread_mutex_t               _connexionMutex;

	std::ofstream                 _logFile;
};

#endif // D_SERVER_H


#ifndef D_CONNEXION_H
#define D_CONNEXION_H

#include <string>
#include <list>
#include <pthread.h>

class DServer;
class DClient;

#define DCON_CLIENT_NOT_ADDED 0
#define DCON_CLIENT_ADDED     1
#define DCON_INVALID_RESS     -1
#define DCON_INCOMPAT_CONF    -2
#define DCON_USER_CONNECTED   -3

class DConnexion
{
public:
	DConnexion(DServer *p_server, std::string &p_line, std::string& p_speed);
	DConnexion(DServer *p_server);
	virtual ~DConnexion();

	bool isValid() {return _valid;}
	bool isAlive() {return _alive;}

	std::string &getLine() {return _line;}
	int getNbClients() {return _clientsList.size();}

	int tryAddClient(DClient *p_client);
	void handleDisconnect();

	std::string getStatus();

private:
	static int readSpeed(std::string &p_speed);

	static void* StaticCommLoop(void *p_connexion);
	void inputLoop();
	void monitoringLoop();
	static void* StaticCloseConnexion(void *p_connexion);
	void closeConnexion();

	void addClient(DClient *p_client);
	bool openConnexion();

	DServer*            _server;

	std::string         _line;
	int                 _speed;
	bool                _isMonitoring;

	int                 _serialFD;

	bool                _valid;
	bool                _alive;

	std::list<DClient*> _clientsList;
	pthread_mutex_t     _clientMutex;

	std::string         _monitoringFifoName;
	int                 _monitoringFifoFD;

	pthread_t           _commThreadId;
	pthread_t           _closeThreadId;

	/* utilises pour debloquer read lorsque la liaison n'ecrit rien */
	int                 _internalPipeFDs[2];
};

#endif // D_CONNEXION_H


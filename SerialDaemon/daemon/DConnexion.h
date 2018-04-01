#ifndef D_CONNEXION_H
#define D_CONNEXION_H

#include <string>
#include <list>
#include <pthread.h>
#include <mutex>

class DServer;
class DClient;

class DConnexion
{
public:
	DConnexion(DClient *p_client, DServer *p_server, unsigned int p_connexionId);
	virtual ~DConnexion();

	bool isValid() {return _validity;}

	bool tryAddClient(DClient *p_client);
	void handleDisconnect();

private:
	static void* StaticInputLoop(void *p_connexion);
	void inputLoop();
	void addClient(DClient *p_client);
	void readParameters(std::string &p_parametersStr);
	bool openConnexion();
	void closeConnexion();
	void sendConnectedMessage(DClient *p_client);

	unsigned int        _connexionId;
	unsigned int        _nbCreatedFifo;
	std::string         _line;
	bool                _isDummy;
	bool                _validity;
	std::list<DClient*> _clientsList;
	pthread_t           _inputThreadId;
	DServer*            _server;
	bool                _connected;
	std::mutex          _clientMutex;
};

#endif // D_CONNEXION_H


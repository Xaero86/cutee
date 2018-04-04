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
	DConnexion(DServer *p_server, std::string &p_line, std::string& p_speed);
	virtual ~DConnexion();

	bool isValid() {return _valid;}
	bool isAlive() {return _alive;}

	std::string &getLine() {return _line;}
	int getNbClients() {return _clientsList.size();}

	int tryAddClient(DClient *p_client);
	void handleDisconnect();

private:
	static void* StaticInputLoop(void *p_connexion);
	void inputLoop();
	static void* StaticOutputLoop(void *p_connexion);
	void outputLoop();
	static void* StaticCloseConnexion(void *p_connexion);
	void closeConnexion();

	void addClient(DClient *p_client);
	static int readSpeed(std::string &p_speed);

	bool openConnexion();

	DServer*            _server;
	unsigned int        _connexionId;

	std::string         _line;
	int                 _speed;
	bool                _isDummy;
	bool                _dummyLoopback;
	int                 _serialFD;

	bool                _valid;
	bool                _alive;

	std::list<DClient*> _clientsList;
	std::mutex          _clientMutex;
	unsigned int        _nbCreatedFifo;

	pthread_t           _inputThreadId;

	std::string         _outputFifoName;
	int                 _outputFifoFD;
	pthread_t           _outputThreadId;

	pthread_t           _closeThreadId;

	/* utilises pour debloquer read lorsque la liaison n'ecrit rien */
	int                 _internalPipeFDs[2];
};

#endif // D_CONNEXION_H


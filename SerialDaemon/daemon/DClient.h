#ifndef D_CLIENT_H
#define D_CLIENT_H

#include <pthread.h>
#include <map>
#include <string>
#include <fstream>

class DServer;

class DClient
{
public :
	DClient(int p_clientFD, DServer* p_server);
        virtual ~DClient();

	bool isValid() {return _validity;}

	std::string &getConnexionParam() {return _connexionParam;}
	std::ofstream &fifoInput() {return _fifoInput;}

	bool setInputFifo(std::string &p_fifoPath);

	bool sendInfo(std::string &p_msg);
	bool sendError(std::string &p_msg);
	bool sendFatal(std::string &p_msg);

private :
	static void* StaticEventLoop(void *p_client);
	void eventLoop();
	bool receiveMessage(std::map<std::string, std::string> *p_dataExpected = nullptr);
	bool sendMessage(std::map<std::string, std::string> &p_data);

	static void* StaticOpenInputFifo(void *p_client);
	void openInputFifo();

	bool           _validity;
        int            _clientSocketFD;
	pthread_t      _threadId;
	DServer*       _server;
	std::string    _connexionParam;

	std::string    _fifoInputPath;
	std::ofstream  _fifoInput;
	pthread_t      _openInputThreadId;
};

#endif // D_CLIENT_H


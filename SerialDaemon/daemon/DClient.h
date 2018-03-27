#ifndef D_CLIENT_H
#define D_CLIENT_H

#include <pthread.h>
#include <map>
#include <string.h>

class DServer;

class DClient
{
public :
	DClient(int p_clientFD, DServer* p_server);
        virtual ~DClient();

	static void* StaticEventLoop(void *p_client);
	bool isValid() {return _validity;}

	bool sendInfo(std::string &p_msg);
	bool sendError(std::string &p_msg);
	bool sendFatal(std::string &p_msg);

private :
	void eventLoop();
	bool receiveMessage(std::map<std::string, std::string> *p_dataExpected = nullptr);
	bool sendMessage(std::map<std::string, std::string> &p_data);

	bool      _validity;
        int       _clientSocketFD;
	pthread_t _threadId;
	DServer*  _server;
};

#endif // D_CLIENT_H


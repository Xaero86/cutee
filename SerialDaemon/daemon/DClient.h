#ifndef D_CLIENT_H
#define D_CLIENT_H

#include <pthread.h>
#include <map>
#include <string>

class DServer;

class DClient
{
public :
	DClient(int p_clientFD, DServer* p_server);
        virtual ~DClient();

	bool isValid() {return _validity;}

	std::string &getLine() {return _line;}
	std::string &getSpeed() {return _speed;}
	bool isMonitoring() {return _monitoring;}

	void writeToInputFifo(const char* p_data, size_t p_size);

	bool setFifos(std::string &p_fifoInputPath, std::string &p_fifoOutputPath);

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
	std::string    _line;
	std::string    _speed;
	bool           _monitoring;

	std::string    _fifoInputPath;
	int            _fifoInputFD;
	pthread_t      _openInputThreadId;
};

#endif // D_CLIENT_H


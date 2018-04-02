#ifndef A_CLIENT_H
#define A_CLIENT_H

#include <stdint.h>
#include <string>
#include <map>
#include <pthread.h>

#include "definition.h"

class AClient
{
public:
	static void CreateAndConnecteClient(uint16_t p_port, int argc, char** argv);
	static void UserSignalHandler(int p_signo);
	static AClient *G_ClientInstance;

	virtual ~AClient();

	void sendServerHalt();

private:
	AClient(uint16_t p_port, std::string &p_line, std::string &p_speed);
	bool connectToServer();
	void eventLoop();
	bool receiveMessage(std::map<std::string, std::string> *p_dataExpected = nullptr);
	bool sendMessage(std::map<std::string, std::string> &p_data);

	bool startInputLoop(std::string &p_fifoPath);
	static void* StaticInputLoop(void *p_client);
	void inputLoop();

	uint16_t            _port;
	std::string         _line;
	std::string         _speed;
	int                 _clientSocketFD;

	std::string         _fifoInputPath;
	int                 _fifoInputFD;
	pthread_t           _inputThreadId;
};

#endif // A_CLIENT_H


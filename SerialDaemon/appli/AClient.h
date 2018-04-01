#ifndef A_CLIENT_H
#define A_CLIENT_H

#include <stdint.h>
#include <string>
#include <map>
#include <fstream>
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
	AClient(uint16_t p_port, int argc, char** argv);
	bool connectToServer();
	void eventLoop();
	bool receiveMessage(std::map<std::string, std::string> *p_dataExpected = nullptr);
	bool sendMessage(std::map<std::string, std::string> &p_data);

	bool startInputLoop(std::string &p_fifoPath);
	static void* StaticInputLoop(void *p_client);
	void inputLoop();

	uint16_t            _port;
	int                 _argc;
	char**              _argv;
	int                 _clientSocketFD;

	std::string         _fifoInputPath;
	std::ifstream       _fifoInput;
	pthread_t           _inputThreadId;
};

#endif // A_CLIENT_H


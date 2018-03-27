#ifndef A_CLIENT_H
#define A_CLIENT_H

#include <stdint.h>
#include <string.h>
#include <map>

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

	uint16_t            _port;
	int                 _argc;
	char**              _argv;
	int                 _clientSocketFD;
};

#endif // A_CLIENT_H


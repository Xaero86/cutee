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
	static void CreateAndConnecteClient(uint16_t p_port, std::string p_line, std::string p_speed, bool p_monitoring, bool p_onePerUser);
	static void reinitTerm();
	static struct termios G_NormalTerm;

	virtual ~AClient();

private:
	AClient(uint16_t p_port, std::string &p_line, std::string &p_speed, bool p_monitoring, bool p_onePerUser);

	bool connectToServer();
	void eventLoop();

	bool receiveMessage(std::map<std::string, std::string> *p_dataExpected = NULL);
	bool sendMessage(std::map<std::string, std::string> &p_data);

	static inline std::string getUser();

	static void* StaticInputLoop(void *p_client);
	void inputLoop();

	static void* StaticOutputLoop(void *p_client);
	void outputLoop();

	uint16_t            _port;
	std::string         _line;
	std::string         _speed;
	bool                _monitoring;
	bool                _onePerUser;

	int                 _clientSocketFD;

	std::string         _fifoInputPath;
	int                 _fifoInputFD;
	pthread_t           _inputThreadId;

	std::string         _fifoOutputPath;
	int                 _fifoOutputFD;
	pthread_t           _outputThreadId;
};

#endif // A_CLIENT_H


#include "AClient.h"

#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <cstdio>
#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pwd.h>
#include <fcntl.h>
#include <termios.h>

#include "ClientServerComm.h"

/* Liste des versions de serveur incompatible, a completer en cas de rupture d'interface client/serveur */
static const std::string G_IncompatibleServer[] = {  };

AClient *AClient::G_ClientInstance = NULL;
struct termios AClient::G_NormalTerm;

void AClient::CreateAndConnecteClient(uint16_t p_port, std::string p_line, std::string p_speed, bool p_monitoring, bool p_onePerUser)
{
	AClient client(p_port, p_line, p_speed, p_monitoring, p_onePerUser);
	if (client.connectToServer())
	{
		struct termios newTerm;

		/* Gestion des signaux */
		G_ClientInstance = &client;
		signal(SIGUSR1, SignalHandler);
		signal(SIGINT,  SignalHandler);
		signal(SIGPIPE, SIG_IGN);

		/* Gestion du terminal: stdin */
		atexit(reinitTerm);
		/* Empeche echo des caracteres entres */
		tcgetattr(STDIN_FILENO, &G_NormalTerm);
		newTerm = G_NormalTerm;
		newTerm.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newTerm);

		client.eventLoop();
		G_ClientInstance = NULL;
	}
	else
	{
		std::cerr << "Internal error: cannot connect to server" << std::endl;
	}
}

void AClient::SignalHandler(int p_signo)
{
	if (G_ClientInstance == NULL)
	{
		exit(-1);
	}
	if (p_signo == SIGUSR1)
	{
		G_ClientInstance->sendServerHalt();
	}
	if (p_signo == SIGINT)
	{
		if (G_ClientInstance->_fifoOutputFD != -1)
		{
			char ch = 0x03;
			write(G_ClientInstance->_fifoOutputFD, &ch, 1);
		}
		else
		{
			exit(-1);
		}
	}
}

void AClient::reinitTerm()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &G_NormalTerm);
}

AClient::AClient(uint16_t p_port, std::string &p_line, std::string &p_speed, bool p_monitoring, bool p_onePerUser)
	: _port(p_port), _line(p_line), _speed(p_speed), _monitoring(p_monitoring), _onePerUser(p_onePerUser),
	  _clientSocketFD(-1), _fifoInputPath(), _fifoInputFD(-1), _inputThreadId(-1),
	  _fifoOutputPath(), _fifoOutputFD(-1), _outputThreadId(-1)
{
}

AClient::~AClient()
{
	if (_clientSocketFD != -1)
	{
		close(_clientSocketFD);
		_clientSocketFD = -1;
	}
	if (_fifoInputFD != -1)
	{
		close(_fifoInputFD);
		_fifoInputFD = -1;
	}
	if (_fifoOutputFD != -1)
	{
		close(_fifoOutputFD);
		_fifoOutputFD = -1;
	}
}

bool AClient::connectToServer()
{
	int readyCheck = -1;
	if (_clientSocketFD == -1)
	{
		_clientSocketFD = socket(AF_INET, SOCK_STREAM, 0);
		if (_clientSocketFD < 0)
		{
			std::cerr << "Unable to create socket on port " << _port << std::endl;
		}
		else
		{
			int nbTryLeft = 5;
			struct sockaddr_in socketProp;
                        socketProp.sin_family = AF_INET;
                        socketProp.sin_port = htons(_port);
                        socketProp.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			while (((readyCheck = connect(_clientSocketFD, (struct sockaddr *)&socketProp, sizeof(sockaddr_in))) < 0) &&
				(nbTryLeft > 0))
			{
				nbTryLeft--;
				usleep(200000);
			}
		}
	}
	return (_clientSocketFD != -1 && readyCheck == 0);
}

void AClient::eventLoop()
{
	std::map<std::string, std::string> msgData;

	/* Le serveur envoie sa version au client */
	msgData[KEY_VERSION] = "";
	if (!receiveMessage(&msgData))
	{
		return;
	}
	std::string senderVersion = msgData[KEY_VERSION];

	/* Test si la version de serveur fait partie de la liste des versions incompatibles */
	bool incompatible = false;
	size_t nbIncompatible = sizeof(G_IncompatibleServer) / sizeof(std::string);
	for (int i = 0; (i < sizeof(G_IncompatibleServer) / sizeof(std::string)) && !incompatible; i++)
	{
		if (senderVersion.compare(G_IncompatibleServer[i]) == 0)
		{
			incompatible = true;
		}
	}
	if (incompatible)
	{
		std::cerr << "Unable to connect. Incompatible daemon already started" << std::endl;
		return;
	}

	msgData.clear();

	/* Le client envoie sa version au serveur avec les parametres */
	msgData[KEY_VERSION] = VERSION;
	if (!_monitoring)
	{
		msgData[KEY_LINE] = _line;
		msgData[KEY_SPEED] = _speed;
	}
	else
	{
		msgData[KEY_MONITOR] = "YES";
	}
	if (_onePerUser)
	{
		msgData[KEY_USER] = getUser();
	}

	if (!sendMessage(msgData))
	{
		return;
	}

	msgData.clear();

	/* Le serveur envoie au client la ressource pour communiquer les donnees venant de la connexion */
	msgData[KEY_INPATH] = "";
	msgData[KEY_OUTPATH] = "";
	if (!receiveMessage(&msgData))
	{
		return;
	}
	_fifoInputPath = msgData[KEY_INPATH];
	if (0 != pthread_create(&_inputThreadId, NULL, AClient::StaticInputLoop, this))
	{
		std::cerr << "Internal error" << std::endl;
		return;
	}
	_fifoOutputPath = msgData[KEY_OUTPATH];
	if (0 != pthread_create(&_outputThreadId, NULL, AClient::StaticOutputLoop, this))
	{
		std::cerr << "Internal error" << std::endl;
		return;
	}

	/* boucle d'attente des info du serveur */
	while (receiveMessage());

	std::cout << "Terminate" << std::endl;
}

bool AClient::receiveMessage(std::map<std::string, std::string> *p_dataExpected)
{
	ssize_t msgLength;
	char msgBuffer[CLI_SER_BUFFER_SIZE];
	std::map<std::string, std::string> msgData;

	if (_clientSocketFD == -1)
	{
		return false;
	}

	memset(msgBuffer, 0, CLI_SER_BUFFER_SIZE);
	msgLength = recv(_clientSocketFD, msgBuffer, CLI_SER_BUFFER_SIZE, 0);
	msgBuffer[CLI_SER_BUFFER_SIZE-1] = '\0';

	if (msgLength <= 0)
	{
		/* connection interrompue */
		return false;
	}

	bool readResult = readMessage(msgBuffer, msgLength, SERVER_NAME, msgData);

        if (!readResult)
        {
                std::cerr << "Invalid Message received from server: " << msgBuffer << std::endl;
                return false;
        }

	if (msgData.find(KEY_INFO) != msgData.end())
	{
		/* Message d'info du serveur */
		std::cout << msgData[KEY_INFO] << std::endl;
	}
	if (msgData.find(KEY_ERROR) != msgData.end())
	{
		/* Message d'erreur du serveur */
		std::cerr << msgData[KEY_ERROR] << std::endl;
	}
	if (msgData.find(KEY_FATAL) != msgData.end())
	{
		/* Erreur fatal du serveur: arret du client */
		std::cerr << msgData[KEY_FATAL] << std::endl;
		return false;
	}

	if (p_dataExpected != NULL)
	{
		/* Des donnees sont attendues sur le message, on les recupere */
		std::map<std::string, std::string>::iterator iter;

		for (iter = p_dataExpected->begin(); iter != p_dataExpected->end(); iter++)
		{
			if (msgData.find(iter->first) != msgData.end())
			{
				iter->second = msgData[iter->first];
			}
		}
	}
	return true;
}

bool AClient::sendMessage(std::map<std::string, std::string> &p_data)
{
	ssize_t msgLength;
	char msgBuffer[CLI_SER_BUFFER_SIZE];

	if (_clientSocketFD == -1)
	{
		return false;
	}

	msgLength = createMessage(msgBuffer, CLI_SER_BUFFER_SIZE, CLIENT_NAME, p_data);
	return (write(_clientSocketFD, msgBuffer, msgLength) >= 0);
}

std::string AClient::getUser()
{
	struct passwd *pw = getpwuid(getuid());
	if (pw)
	{
		return std::string(pw->pw_name);
	}
	else
	{
		return std::string();
	}
}

/* Envoi d'un message d'arret au serveur, avec le nom du user en parametre si possible */
void AClient::sendServerHalt()
{
	std::map<std::string, std::string> msgData;

	msgData[KEY_HALTSER] = getUser();
	sendMessage(msgData);
}

void* AClient::StaticInputLoop(void *p_client)
{
	((AClient*)p_client)->inputLoop();
}

void AClient::inputLoop()
{
	_fifoInputFD = open(_fifoInputPath.c_str(), O_RDONLY);

	if (_fifoInputFD != -1)
	{
		char inputBuffer[FIFO_BUFFER];
		unsigned int nbRead;
		memset(inputBuffer, 0, FIFO_BUFFER);

		while (true)
		{
			nbRead = read(_fifoInputFD, inputBuffer, FIFO_BUFFER-1);
			if (nbRead > 0)
			{
				std::cout.write(inputBuffer, nbRead);
			}
			else
			{
				break;
			}
			std::cout.flush();
		}
		if (_fifoInputFD != -1)
		{
			close(_fifoInputFD);
			_fifoInputFD = -1;
		}
	}
	if (_clientSocketFD != -1)
	{
		shutdown(_clientSocketFD, SHUT_RDWR);
	}
}

void* AClient::StaticOutputLoop(void *p_client)
{
	((AClient*)p_client)->outputLoop();
}

void AClient::outputLoop()
{
	_fifoOutputFD = open(_fifoOutputPath.c_str(), O_WRONLY);

	if (_fifoOutputFD != -1)
	{
		char ch;
		bool escaping = false;
		bool stop = false;
		while (!stop)
		{
			ch = getchar();
			if (escaping)
			{
				escaping = false;
				switch (ch)
				{
					case '.':
						stop = true;
						break;
					case '~':
						write(_fifoOutputFD, &ch, 1);
						break;
				}
			}
			else if (ch == '~')
			{
				escaping = true;
			}
			else
			{
				write(_fifoOutputFD, &ch, 1);
			}
		}
		if (_fifoOutputFD != -1)
		{
			close(_fifoOutputFD);
			_fifoOutputFD = -1;
		}
	}
	if (_clientSocketFD != -1)
	{
		shutdown(_clientSocketFD, SHUT_RDWR);
	}
}


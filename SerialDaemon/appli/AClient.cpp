#include "AClient.h"

#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pwd.h>

#include <iostream>
#include <sstream>

#include "ClientServerComm.h"

AClient *AClient::G_ClientInstance = nullptr;

void AClient::CreateAndConnecteClient(uint16_t p_port, int argc, char** argv)
{
	AClient client(p_port, argc, argv);
	if (client.connectToServer())
	{
		G_ClientInstance = &client;
		signal(SIGUSR1, UserSignalHandler);
		client.eventLoop();
	}
	else
	{
		std::cerr << "Fail to connect" << std::endl;
	}
}

void AClient::UserSignalHandler(int p_signo)
{
	if (p_signo == SIGUSR1)
	{
		if (G_ClientInstance != nullptr)
		{
			G_ClientInstance->sendServerHalt();
		}
	}
}

AClient::AClient(uint16_t p_port, int argc, char** argv)
	: _port(p_port), _clientSocketFD(-1), _argc(argc), _argv(argv)
{
}

AClient::~AClient()
{
	if (_clientSocketFD != -1)
	{
		close(_clientSocketFD);
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
				usleep(500000);
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

	/* TODO check version compatible */
	std::cout << "Connected to server version " << senderVersion << std::endl;

	msgData.clear();

	/* Le client envoie sa version au serveur avec les parametres */
	msgData[KEY_VERSION] = VERSION;
	std::stringstream streamWrite;
	for (int i=1; i<_argc; i++)
	{
		streamWrite << _argv[i];
		if (i!=_argc-1)
		{
			streamWrite << " ";
		}
	}
	msgData[KEY_PARAM] = streamWrite.str();

	if (!sendMessage(msgData))
	{
		return;
	}

	/* boucle d'attente des info du serveur */
	while (receiveMessage());

	std::cout << "Terminate" << std::endl;
}

bool AClient::receiveMessage(std::map<std::string, std::string> *p_dataExpected)
{
	ssize_t msgLength;
	char msgBuffer[BUFFER_SIZE];
	std::map<std::string, std::string> msgData;

	if (_clientSocketFD == -1)
	{
		return false;
	}

	memset(msgBuffer, 0, BUFFER_SIZE);
	msgLength = recv(_clientSocketFD, msgBuffer, BUFFER_SIZE, 0);
	msgBuffer[BUFFER_SIZE-1] = '\0';

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

	if (p_dataExpected != nullptr)
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
	char msgBuffer[BUFFER_SIZE];

	if (_clientSocketFD == -1)
	{
		return false;
	}

	msgLength = createMessage(msgBuffer, BUFFER_SIZE, CLIENT_NAME, p_data);
	return (write(_clientSocketFD, msgBuffer, msgLength) >= 0);
}

void AClient::sendServerHalt()
{
	std::map<std::string, std::string> msgData;
	struct passwd *pw = getpwuid(getuid());

	if (pw)
	{
		msgData[KEY_HALTSER] = std::string(pw->pw_name);
	}
	else
	{
		msgData[KEY_HALTSER] = "";
	}

	sendMessage(msgData);
}


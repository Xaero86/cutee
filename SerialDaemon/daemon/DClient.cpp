#include "DClient.h"

#include <unistd.h>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "DServer.h"
#include "definition.h"
#include "ClientServerComm.h"

static const std::string G_IncompatibleClient[] = {  };

DClient::DClient(int p_clientFD, DServer* p_server)
	: _validity(false), _clientSocketFD(p_clientFD), _threadId(), _server(p_server),
	  _line(), _speed(), _monitoring(false), _fifoInputPath(), _fifoInputFD(-1), _openInputThreadId(-1)
{
	if (0 == pthread_create(&_threadId, NULL, DClient::StaticEventLoop, this))
	{
		_validity = true;
	}
	else
	{
		_server->logFile() << "Internal error: fail to create client thread" << std::endl;
	}
}

DClient::~DClient()
{
	if (_clientSocketFD != -1)
	{
		close(_clientSocketFD);
	}
	if (_fifoInputFD != -1)
	{
		close(_fifoInputFD);
	}
	if (!_fifoInputPath.empty())
	{
		unlink(_fifoInputPath.c_str());
	}
}

void* DClient::StaticEventLoop(void *p_client)
{
	((DClient*)p_client)->eventLoop();
	((DClient*)p_client)->_validity = false;
	/* Le serveur va detruire l'objet p_client lors de cet appel */
	((DClient*)p_client)->_server->handleDisconnect();
}

void DClient::eventLoop()
{
	std::map<std::string, std::string> msgData;

	/* Le serveur envoie sa version au client */
	msgData[KEY_VERSION] = VERSION;
	if (!sendMessage(msgData))
	{
		return;
	}

	msgData.clear();

	/* Le client envoie sa version au serveur avec les parametres */
	msgData[KEY_VERSION] = "";
	msgData[KEY_LINE] = "";
	msgData[KEY_SPEED] = "";
	msgData[KEY_USER] = "";
	msgData[KEY_MONITOR] = "";
	if (!receiveMessage(&msgData))
	{
		return;
	}

	std::string senderVersion = msgData[KEY_VERSION];
	if (msgData[KEY_MONITOR].empty())
	{
		_line = msgData[KEY_LINE];
		_speed = msgData[KEY_SPEED];
		_user = msgData[KEY_USER];
	}
	else
	{
		_monitoring = true;
	}

	/* Test si la version de client fait partie de la liste des versions incompatibles */
	bool incompatible = false;
	size_t nbIncompatible = sizeof(G_IncompatibleClient) / sizeof(std::string);
	for (int i = 0; (i < sizeof(G_IncompatibleClient) / sizeof(std::string)) && !incompatible; i++)
	{
		if (senderVersion.compare(G_IncompatibleClient[i]) == 0)
		{
			incompatible = true;
		}
	}
	if (incompatible)
	{
		std::string message("Unable to connect. Incompatible daemon already started");
		sendFatal(message);
	}
	else
	{
		_server->logFile() << "Connection of client to line " << _line << std::endl;

		/* Creation de la connexion avec les parametres */
		_server->connectClient(this);
	}

	/* boucle d'attente des messages du client */
	while (receiveMessage());

	_server->logFile() << "Client disconnected" << std::endl;
}

bool DClient::receiveMessage(std::map<std::string, std::string> *p_dataExpected)
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

	bool readResult = readMessage(msgBuffer, msgLength, CLIENT_NAME, msgData);

	if (!readResult)
	{
		std::cerr << "Invalid Message received from client: " << msgBuffer << std::endl;
		return false;
	}

	if (msgData.find(KEY_HALTSER) != msgData.end())
	{
		/* Message d'arret du serveur */
		_server->halt(msgData[KEY_HALTSER]);
		return true;
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

bool DClient::sendMessage(std::map<std::string, std::string> &p_data)
{
	ssize_t msgLength;
	char msgBuffer[CLI_SER_BUFFER_SIZE];

	if (_clientSocketFD == -1)
	{
		return false;
	}

	msgLength = createMessage(msgBuffer, CLI_SER_BUFFER_SIZE, SERVER_NAME, p_data);
	return (write(_clientSocketFD, msgBuffer, msgLength) >= 0);
}

bool DClient::sendInfo(std::string &p_msg)
{
	std::map<std::string, std::string> p_data;

	p_data[KEY_INFO] = p_msg;
	return sendMessage(p_data);
}

bool DClient::sendError(std::string &p_msg)
{
	std::map<std::string, std::string> p_data;

	p_data[KEY_ERROR] = p_msg;
	return sendMessage(p_data);
}

bool DClient::sendFatal(std::string &p_msg)
{
	std::map<std::string, std::string> p_data;

	p_data[KEY_FATAL] = p_msg;
	return sendMessage(p_data);
}

bool DClient::setFifos(std::string &p_fifoInputPath, std::string &p_fifoOutputPath)
{
	_fifoInputPath = p_fifoInputPath;

	if (0 != pthread_create(&_openInputThreadId, NULL, DClient::StaticOpenInputFifo, this))
	{
		return false;
	}

	std::map<std::string, std::string> msgData;
	/* Le serveur envoie au client les ressources pour communiquer avec la connexion */
	msgData[KEY_INPATH] = _fifoInputPath;
	msgData[KEY_OUTPATH] = p_fifoOutputPath;
	return sendMessage(msgData);
}

void* DClient::StaticOpenInputFifo(void *p_client)
{
	((DClient*)p_client)->openInputFifo();
}

void DClient::openInputFifo()
{
	/* l'ouverture d'une fifo en ecriture est bloquante, d'ou le thread */
	_fifoInputFD = open(_fifoInputPath.c_str(), O_WRONLY);
	if (_fifoInputFD == -1)
	{
		std::string message("Unable to open fifo");
		sendFatal(message);
	}
}

void DClient::writeToInputFifo(const char* p_data, size_t p_size)
{
	if (_fifoInputFD != -1)
	{
		if (write(_fifoInputFD, p_data, p_size) < 0)
		{
			_fifoInputFD = -1;
		}
	}
}


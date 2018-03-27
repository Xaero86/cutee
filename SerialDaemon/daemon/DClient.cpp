#include "DClient.h"

#include <unistd.h>
#include <sstream>
#include <sys/socket.h>

#include "DServer.h"
#include "definition.h"
#include "ClientServerComm.h"

DClient::DClient(int p_clientFD, DServer* p_server)
	: _validity(false), _clientSocketFD(p_clientFD), _threadId(), _server(p_server)
{
	int result = pthread_create(&_threadId, nullptr, DClient::StaticEventLoop, this);
	if (result == 0)
	{
		_validity = true;
	}
	else
	{
		_server->logFile() << "DClient::DClient : Error creating thread" << std::endl;
	}
}

DClient::~DClient()
{
	close(_clientSocketFD);
}

void* DClient::StaticEventLoop(void *p_client)
{
	((DClient*)p_client)->eventLoop();
	((DClient*)p_client)->_validity = false;
	((DClient*)p_client)->_server->handleDisconnect();
}

void DClient::eventLoop()
{
	std::map<std::string, std::string> msgData;

	/* Le serveur envoie sa version au client */
	msgData["version"] = VERSION;
	if (!sendMessage(msgData))
	{
		return;
	}

	msgData.clear();

	/* Le client envoie sa version au serveur avec les parametres */
	msgData[KEY_VERSION] = "";
	msgData[KEY_PARAM] = "";
	if (!receiveMessage(&msgData))
	{
		return;
	}

	std::string senderVersion = msgData[KEY_VERSION];
	std::string requestParameters = msgData[KEY_PARAM];

	/* TODO check version compatible */
	_server->logFile() << "Connection of client version " << senderVersion << " with parameters=" << requestParameters << std::endl;

	/* boucle d'attente des messages du client */
	while (receiveMessage());

	_server->logFile() << "Client disconnected" << std::endl;
}

bool DClient::receiveMessage(std::map<std::string, std::string> *p_dataExpected)
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

bool DClient::sendMessage(std::map<std::string, std::string> &p_data)
{
	ssize_t msgLength;
	char msgBuffer[BUFFER_SIZE];

	if (_clientSocketFD == -1)
	{
		return false;
	}

	msgLength = createMessage(msgBuffer, BUFFER_SIZE, SERVER_NAME, p_data);
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


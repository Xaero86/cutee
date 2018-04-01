#include "DConnexion.h"

#include "definition.h"
#include "DServer.h"
#include "DClient.h"

#include <unistd.h>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

DConnexion::DConnexion(DClient* p_client, DServer* p_server, unsigned int p_connexionId)
	: _connexionId(p_connexionId), _line(), _isDummy(false), _validity(false), _inputThreadId(), 
	  _server(p_server), _nbCreatedFifo(0), _connected(false), _clientMutex()
{
	std::string parameters = p_client->getConnexionParam();
	if (parameters.empty())
	{
		_isDummy = true;
		_line = "dummy connexion";
	}
	else
	{
		_line = parameters; /* TODO */
	}

	int result = pthread_create(&_inputThreadId, nullptr, DConnexion::StaticInputLoop, this);
	if (result == 0)
	{
		_validity = true;
		addClient(p_client);
	}
	else
	{
		_server->logFile() << "Internal error: fail to create connexion thread" << std::endl;
	}
}

DConnexion::~DConnexion()
{
	_validity = false;
	_connected = false;
	_clientMutex.lock();
	std::list<DClient*>::iterator it = _clientsList.begin();
	while(it != _clientsList.end())
	{
		it = _clientsList.erase(it);
	}
	_clientMutex.unlock();
}

void DConnexion::readParameters(std::string &p_parametersStr)
{
}

void* DConnexion::StaticInputLoop(void *p_connexion)
{
	((DConnexion*)p_connexion)->inputLoop();
	((DConnexion*)p_connexion)->_validity = false;
	((DConnexion*)p_connexion)->_server->handleConnexionClosed();
}

void DConnexion::inputLoop()
{
	std::list<DClient*>::iterator itClient;
	_connected = openConnexion();
	if (!_connected)
	{
		_validity = false;
		return;
	}

	_clientMutex.lock();
	itClient = _clientsList.begin();
	while(itClient != _clientsList.end())
	{
		sendConnectedMessage(*itClient);
	}
	_clientMutex.unlock();

	int cycle = 0;
	int dummyMsgSize = sizeof(DUMMY_CONNEC_MSG);
	std::string dataInput;
	while (_validity)
	{
		if (_isDummy)
		{
			sleep(1);
			dataInput = std::string(&DUMMY_CONNEC_MSG[cycle],1);
			cycle = (cycle+1) % dummyMsgSize;
		}
		else
		{
			/* TODO read connec */
			sleep(1);
			dataInput = std::string(".");
		}
_server->logFile() << dataInput << std::endl;
		_clientMutex.lock();
		itClient = _clientsList.begin();
		while(itClient != _clientsList.end())
		{
			(*itClient)->fifoInput() << dataInput << std::endl;
			//(*itClient)->fifoInput().flush();
			itClient++;
		}
		_clientMutex.unlock();
	}
	_connected = false;
	closeConnexion();
}

bool DConnexion::tryAddClient(DClient *p_client)
{
	if (!_validity)
	{
		return false;
	}
	std::string parameters = p_client->getConnexionParam();
	if ((_isDummy && parameters.empty()) ||
	    (_line.compare(parameters) == 0)) /* TODO */
	{
		addClient(p_client);
		return true;
	}
	else
	{
		return false;
	}
}

void DConnexion::addClient(DClient *p_client)
{
	std::string fifoNameStr;
	bool fifoCreated = false;
	do {
		std::stringstream fifoName;
		fifoName << WORKING_DIRECTORY << "/" << "cnx" << _connexionId << "fifo" << _nbCreatedFifo;
		fifoNameStr = fifoName.str();
		_nbCreatedFifo++;

		fifoCreated = (mkfifo(fifoNameStr.c_str(), 0666) == 0);

		if (!fifoCreated && ((errno != EEXIST) || (_nbCreatedFifo > 1000)))
		{
			std::string messageStr("Internal error: Unable to create fifo");
			p_client->sendFatal(messageStr);
			return;
		}
	} while (!fifoCreated);

	p_client->setInputFifo(fifoNameStr);

	_clientMutex.lock();
	_clientsList.push_back(p_client);
	_clientMutex.unlock();
	sendConnectedMessage(p_client);
}

void DConnexion::sendConnectedMessage(DClient *p_client)
{
	if (_connected)
	{
		std::stringstream message;
		message << "Connected to " << _line;
		std::string messageStr = message.str();
		p_client->sendInfo(messageStr);
	}
}

bool DConnexion::openConnexion()
{
	if (_isDummy)
	{
		return true;
	}
	
	/* TODO */
	return false;
}

void DConnexion::closeConnexion()
{
	if (_isDummy)
	{
		return;
	}
	/* TODO */
}

void DConnexion::handleDisconnect()
{
	_clientMutex.lock();
	std::list<DClient*>::iterator it = _clientsList.begin();
	while(it != _clientsList.end())
	{
		if ((*it)->isValid())
		{
			it++;
		}
		else
		{
			it = _clientsList.erase(it);
		}
	}
	if (_clientsList.size() == 0)
	{
		_validity = false;
	}
	_clientMutex.unlock();
}


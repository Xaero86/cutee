#include "DConnexion.h"

#include "definition.h"
#include "DServer.h"
#include "DClient.h"

#include <unistd.h>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <errno.h>
#include <termios.h>

DConnexion::DConnexion(DClient* p_client, DServer* p_server, unsigned int p_connexionId)
	: _connexionId(p_connexionId), _isDummy(false), _validity(false), _inputThreadId(), 
	  _server(p_server), _nbCreatedFifo(0), _connected(false), _clientMutex(), _serialFileDesc(-1)
{
	_internalPipeFDs[0] = -1;
	_internalPipeFDs[1] = -1;
	_line = p_client->getLine();
	std::string speed = p_client->getSpeed();
	if (_line.empty())
	{
		_isDummy = true;
		_line = "dummy connexion";
	}
	else
	{
		_speed = readSpeed(speed);
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
	closeConnexion();
}

int DConnexion::readSpeed(std::string &p_speed)
{
	if (p_speed.compare("230400") == 0)
		return B230400;
	else if (p_speed.compare("115200") == 0)
		return B115200;
	else if (p_speed.compare("57600") == 0)
		return B57600;
	else if (p_speed.compare("38400") == 0)
		return B38400;
	else if (p_speed.compare("19200") == 0)
		return B19200;
	else if (p_speed.compare("9600") == 0)
		return B9600;
	else if (p_speed.compare("4800") == 0)
		return B4800;
	else if (p_speed.compare("2400") == 0)
		return B2400;
	else if (p_speed.compare("1200") == 0)
		return B1200;
	else
		return B115200; /* par defaut */
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
		itClient++;
	}
	_clientMutex.unlock();

	char inputBuffer[INPUT_BUFFER];
	int nbRead;
	memset(inputBuffer, 0, INPUT_BUFFER);

	/* Specific dummy */
	int cycle = 0;
	int dummyMsgSize = sizeof(DUMMY_CONNEC_MSG);

	/* pour connexion serie */
	fd_set setOfFDs;
	FD_ZERO(&setOfFDs);
	FD_SET(_serialFileDesc, &setOfFDs);
	FD_SET(_internalPipeFDs[0], &setOfFDs);
	int fdmax = ((_serialFileDesc < _internalPipeFDs[0])?_internalPipeFDs[0]:_serialFileDesc)+1;

	while (_validity)
	{
		if (_isDummy)
		{
			sleep(1);
			inputBuffer[0] = DUMMY_CONNEC_MSG[cycle];
			nbRead = 1;
			cycle = (cycle+1) % dummyMsgSize;
		}
		else
		{
			select(fdmax, &setOfFDs, nullptr, nullptr, nullptr);
			nbRead = read(_serialFileDesc, inputBuffer, INPUT_BUFFER-1);
			if (FD_ISSET(_internalPipeFDs[0], &setOfFDs))
			{
				_validity = false;
			}
			/* supprimer les 8 lignes ?... */
			if (nbRead > 0)
			{
				inputBuffer[nbRead] = '\0';
			}
			else if (nbRead < 0)
			{
				_validity = false;
			}
		}
		if (nbRead > 0)
		{
			_clientMutex.lock();
			itClient = _clientsList.begin();
			while(itClient != _clientsList.end())
			{
				(*itClient)->writeToInputFifo(inputBuffer, nbRead);
				itClient++;
			}
			_clientMutex.unlock();
		}
	}
	_connected = false;
	closeConnexion();
	std::string messageStr("Rupture");
	_clientMutex.lock();
	itClient = _clientsList.begin();
	while(itClient != _clientsList.end())
	{
		(*itClient)->sendFatal(messageStr);
		itClient++;
	}
	_clientMutex.unlock();	
}

int DConnexion::tryAddClient(DClient *p_client)
{
	if (!_validity)
	{
		return 0;
	}
	std::string line = p_client->getLine();
	int speed = readSpeed(p_client->getSpeed());

	if ((_isDummy && line.empty()) ||
	    ((_line.compare(line) == 0) && (_speed == speed)))
	{
		addClient(p_client);
		return 1;
	}
	else if ((_line.compare(line) == 0) && (_speed != speed))
	{
		return -1;
	}
	else
	{
		return 0;
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
//	_serialFileDesc = open(_line.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	_serialFileDesc = open(_line.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
	if (_serialFileDesc < 0)
	{
		return false;
	}
	struct termios ttyConf;
	memset(&ttyConf, 0, sizeof ttyConf);
	if (tcgetattr(_serialFileDesc, &ttyConf) != 0)
	{
		return false;
	}
	cfsetospeed(&ttyConf, (speed_t) _speed);
	cfsetispeed(&ttyConf, (speed_t) _speed);

	ttyConf.c_cflag |= (CLOCAL | CREAD);
	ttyConf.c_cflag &= ~CSIZE;
	ttyConf.c_cflag |= CS8;
	ttyConf.c_cflag &= ~PARENB;
	ttyConf.c_cflag &= ~CSTOPB;
	ttyConf.c_cflag &= ~CRTSCTS;

	ttyConf.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	ttyConf.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	ttyConf.c_oflag &= ~OPOST;

	ttyConf.c_cc[VMIN] = 1;
	ttyConf.c_cc[VTIME] = 1;

	if (tcsetattr(_serialFileDesc, TCSANOW, &ttyConf) != 0)
	{
		return false;
	}

	pipe2(_internalPipeFDs, O_NONBLOCK);
	
	return true;
}

void DConnexion::closeConnexion()
{
	if (_isDummy)
	{
		return;
	}
	if (_serialFileDesc != -1)
	{
		close(_serialFileDesc);
		_serialFileDesc = -1;
	}
	if (_internalPipeFDs[0] != -1)
	{
		close(_internalPipeFDs[0]);
		_internalPipeFDs[0] = -1;
	}
	if (_internalPipeFDs[1] != -1)
	{
		close(_internalPipeFDs[1]);
		_internalPipeFDs[1] = -1;
	}
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
		if (_internalPipeFDs[1] != -1)
		{
			/* Debloquer le thread de lecture */
			char poke = '.';
			write(_internalPipeFDs[1], &poke, 1);
		}
		if (_isDummy)
		{
			_server->logFile() << "No more client. Dummy connexion closed" << std::endl;
		}
		else
		{
			_server->logFile() << "No more client. Connexion " << _line << " closed" << std::endl;
		}
	}
	_clientMutex.unlock();
}


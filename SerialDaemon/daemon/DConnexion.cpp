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

DConnexion::DConnexion(DServer* p_server, std::string &p_line, std::string &p_speed)
	: _server(p_server), _valid(false), _alive(false), _dummyLoopback(false), _serialFD(-1),
	  _clientMutex(), _nbCreatedFifo(0), _inputThreadId(), 
	  _outputFifoName(), _outputFifoFD(-1), _outputThreadId()
{
	_internalPipeFDs[0] = -1;
	_internalPipeFDs[1] = -1;

	if (p_line.empty())
	{
		_line = "serial daemon monitoring";
		_speed = 0;
		_isDummy = true;
	}
	else
	{
		_line = p_line;
		_speed = readSpeed(p_speed);
		_isDummy = false;
	}

	if (!openConnexion())
	{
		_server->logFile() << "Unable to open connection" << std::endl;
		return;
	}

	/* Creation de la fifo output */
	_connexionId = 0;
	bool fifoCreated = false;
	do {
		_connexionId++;
		std::stringstream fifoName;
		fifoName << WORKING_DIRECTORY << "/" << "cnx" << _connexionId << "fifoOutput";
		_outputFifoName = fifoName.str();

		fifoCreated = (mkfifo(_outputFifoName.c_str(), 0666) == 0);

		if (!fifoCreated && ((errno != EEXIST) || (_connexionId > 1000)))
		{
			_server->logFile() << "Internal error: Unable to create output fifo" << std::endl;
			return;
		}
	} while (!fifoCreated);

	_valid = true;
	_alive = true;

	if (!_isDummy)
	{
		if (pthread_create(&_inputThreadId, nullptr, DConnexion::StaticInputLoop, this) != 0)
		{
			_server->logFile() << "Internal error: Unable to create connexion thread" << std::endl;
			_valid = false;
			return;
		}
	}

	if (pthread_create(&_outputThreadId, nullptr, DConnexion::StaticOutputLoop, this) != 0)
	{
		_server->logFile() << "Internal error: Unable to create connexion thread" << std::endl;
		_valid = false;
		return;
	}
}

DConnexion::~DConnexion()
{
	_valid = false;
	_clientMutex.lock();
	std::list<DClient*>::iterator it = _clientsList.begin();
	while(it != _clientsList.end())
	{
		it = _clientsList.erase(it);
	}
	_clientMutex.unlock();

	if (_serialFD != -1)
	{
		close(_serialFD);
		_serialFD = -1;
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
	if (_outputFifoFD != -1)
	{
		close(_outputFifoFD);
		_outputFifoFD = -1;
	}
	if (!_outputFifoName.empty())
	{
		unlink(_outputFifoName.c_str());
	}
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
	((DConnexion*) p_connexion)->inputLoop();
}

void DConnexion::inputLoop()
{
	if (_isDummy)
	{
		/* on a rien a faire la */
		return;
	}

	std::list<DClient*>::iterator itClient;

	char inputBuffer[INPUT_BUFFER];
	int nbRead;
	memset(inputBuffer, 0, INPUT_BUFFER);

	fd_set setOfFDs;
	int fdmax = (_serialFD < _internalPipeFDs[0])?_internalPipeFDs[0]:_serialFD;

	while (_valid)
	{
		FD_ZERO(&setOfFDs);
		FD_SET(_serialFD, &setOfFDs);
		FD_SET(_internalPipeFDs[0], &setOfFDs);
		select(fdmax+1, &setOfFDs, nullptr, nullptr, nullptr);
		nbRead = read(_serialFD, inputBuffer, INPUT_BUFFER-1);

		if (FD_ISSET(_internalPipeFDs[0], &setOfFDs) || (nbRead <= 0))
		{
			/* on positionne pas la validite a faux ici, seulement quand on a plus de clients */
			break;
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

	/* On informe les clients de la rupture. Ils se deconnecteront */
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

void* DConnexion::StaticOutputLoop(void *p_connexion)
{
	((DConnexion*) p_connexion)->outputLoop();
}

void DConnexion::outputLoop()
{
	_outputFifoFD = open(_outputFifoName.c_str(), O_RDONLY);
	if (_outputFifoFD == -1)
	{
		_server->logFile() << "Internal error. Unable to open output fifo" << std::endl;
		return;
	}

	char outputBuffer[INPUT_BUFFER];
	int nbRead;
	memset(outputBuffer, 0, INPUT_BUFFER);

	std::list<DClient*>::iterator itClient;

	while (_valid)
	{
		nbRead = read(_outputFifoFD, outputBuffer, INPUT_BUFFER-1);
		if (nbRead > 0)
		{
			if (!_isDummy)
			{
				write(_serialFD, outputBuffer, nbRead);
			}
			else
			{
				/* TODO changer 2 en 1 ... */
				std::string response;
				if (_dummyLoopback)
				{
					response = std::string(outputBuffer, nbRead);
				}
				else if ((nbRead == 2) && (outputBuffer[0] == 's'))
				{
					response = _server->getStatus();
				}
				else if ((nbRead == 2) && (outputBuffer[0] == 'l'))
				{
					_dummyLoopback = true;
					response = "Loopback activated. Ctrl+C to stop\n";
				}
				else if ((nbRead == 2) && (outputBuffer[0] == 0x03))
				{
					/* Ctrl+c => break => 0x03 */
					_dummyLoopback = false;
					response = "Loopback stopped.\n";
				}
				if (response.length() > 0)
				{
					_clientMutex.lock();
					itClient = _clientsList.begin();
					while(itClient != _clientsList.end())
					{
						(*itClient)->writeToInputFifo(response.c_str(), response.length());
						itClient++;
					}
					_clientMutex.unlock();
				}
			}
		}
		else
		{
			/* on positionne pas la validite a faux ici, seulement quand on a plus de clients */
			break;
		}
	}
}

int DConnexion::tryAddClient(DClient *p_client)
{
	if (!_valid)
	{
		return 0;
	}
	/* On regarde si cette connexion peut gerer ce client */
	std::string line = p_client->getLine();
	int speed = readSpeed(p_client->getSpeed());

	/* Cas de la connexion par defaut */
	if (_isDummy && line.empty())
	{
		addClient(p_client);
		return 1;
	}
	else
	{
		struct stat infoNewCli;
		struct stat infoCurCnx;

		if (0 != stat(line.c_str(), &infoNewCli))
		{
			/* Probleme d'acces a la ressource, le client est supprime */
			return -1;
		}
		if (0 != stat(_line.c_str(), &infoCurCnx))
		{
			/* La ressource de la connexion devrait etre valide */
			/* Si elle a ete rendue invalide depuis la creation de la connexion... temps pis */
			return -1;
		}

		/* Comparaison des inodes des ressources */
		if ((infoNewCli.st_dev == infoCurCnx.st_dev) &&
		    (infoNewCli.st_ino == infoCurCnx.st_ino))
		{
			/* la meme ressource */
			if (speed == _speed)
			{
				/* Parametre de connexion identique => OK */
				addClient(p_client);
				return 1;
			}
			else
			{
				/* Parametre de connexion different => client supprime */
				return -1;
			}
		}
		else
		{
			/* pas la meme ressource, cette connexion ne gere pas ce client */
			return 0;
		}
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

	p_client->setFifos(fifoNameStr, _outputFifoName);

	_clientMutex.lock();
	_clientsList.push_back(p_client);
	_clientMutex.unlock();

	std::stringstream message;
	message << "Connected to " << _line;
	std::string messageStr = message.str();
	p_client->sendInfo(messageStr);
}

bool DConnexion::openConnexion()
{
	if (_isDummy)
	{
		return true;
	}
//	_serialFD = open(_line.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	_serialFD = open(_line.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
	if (_serialFD < 0)
	{
		return false;
	}
	struct termios ttyConf;
	memset(&ttyConf, 0, sizeof ttyConf);
	if (tcgetattr(_serialFD, &ttyConf) != 0)
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

	if (tcsetattr(_serialFD, TCSANOW, &ttyConf) != 0)
	{
		return false;
	}

	pipe2(_internalPipeFDs, O_NONBLOCK);
	
	return true;
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
		_valid = false;
		/* Thread pour attendre la fin des thread input et output et declarer au serveur la fin de la connexion */
		if (pthread_create(&_closeThreadId, nullptr, DConnexion::StaticCloseConnexion, this) != 0)
		{
			/* on arrive pas a creer le thread pour fermer proprement... on ferme comme on peut... */
			_server->logFile() << "Internal error: Unable to create disconnection thread" << std::endl;
			_alive = false;
			_clientMutex.unlock();
			_server->handleConnexionClosed();
			return;
		}
	}
	_clientMutex.unlock();
}

void* DConnexion::StaticCloseConnexion(void *p_connexion)
{
	((DConnexion*) p_connexion)->closeConnexion();
	/* handleConnexionClosed va detruire l'objet connexion, autant l'appeler ici */
	((DConnexion*) p_connexion)->_server->handleConnexionClosed();
}

void DConnexion::closeConnexion()
{
	if (_internalPipeFDs[1] != -1)
	{
		/* Debloquer le thread de lecture */
		char poke = '.';
		write(_internalPipeFDs[1], &poke, 1);
	}

	struct timespec date;
	int result;
	clock_gettime(CLOCK_REALTIME, &date);
	/* on laisse 1 seconde aux threads pour finir */
	date.tv_sec += 1;

	if (!_isDummy)
	{
		result = pthread_timedjoin_np(_inputThreadId, nullptr, &date);
		if (result != 0)
		{
			pthread_cancel(_inputThreadId);
			_server->logFile() << "Warning: Force close input thread of " << _line << std::endl;
		}
	}
	result = pthread_timedjoin_np(_outputThreadId, nullptr, &date);
	if (result != 0)
	{
		pthread_cancel(_outputThreadId);
		_server->logFile() << "Warning: Force close output thread of " << _line << std::endl;
	}
	_alive = false;
	_server->logFile() << "No more client. Connexion to " << _line << " closed" << std::endl;	
}


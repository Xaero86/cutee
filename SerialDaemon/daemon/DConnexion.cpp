#include "DConnexion.h"

#include "definition.h"
#include "DServer.h"
#include "DClient.h"

#include <unistd.h>
#include <sstream>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>

DConnexion::DConnexion(DServer* p_server, std::string &p_line, std::string &p_speed)
	: _server(p_server), _line(p_line), _isMonitoring(false),
	  _valid(false), _alive(false), _monitoringLoopback(false),
	  _serialFD(-1), _commThreadId(),
	  _monitoringFifoName(), _monitoringFifoFD(-1)
{
	pthread_mutex_init(&_clientMutex, NULL);
	_internalPipeFDs[0] = -1;
	_internalPipeFDs[1] = -1;

	_speed = readSpeed(p_speed);

	if (!openConnexion())
	{
		_server->logFile() << "Unable to open connection" << std::endl;
		return;
	}

	_valid = true;
	_alive = true;

	if (pthread_create(&_commThreadId, NULL, DConnexion::StaticCommLoop, this) != 0)
	{
		_server->logFile() << "Internal error: Unable to create communication thread" << std::endl;
		_valid = false;
	}
}

DConnexion::DConnexion(DServer* p_server)
	: _server(p_server), _speed(0), _isMonitoring(true),
	  _valid(false), _alive(false), _monitoringLoopback(false),
	  _serialFD(-1), _commThreadId(),
	  _monitoringFifoName(), _monitoringFifoFD(-1)
{
	pthread_mutex_init(&_clientMutex, NULL);
	_internalPipeFDs[0] = -1;
	_internalPipeFDs[1] = -1;

	_line = "serial daemon monitoring";


	/* Creation de la fifo de monitoring */
	int fifoId = 0;
	bool fifoCreated = false;
	do {
		std::stringstream fifoName;
		fifoName << WORKING_DIRECTORY << "/" << "fifoMonitoring" << fifoId;
		_monitoringFifoName = fifoName.str();

		fifoCreated = (mkfifo(_monitoringFifoName.c_str(), 0666) == 0);

		if (!fifoCreated && ((errno != EEXIST) || (fifoId > 1000)))
		{
			_server->logFile() << "Internal error: Unable to create monitoring fifo" << std::endl;
			return;
		}
		fifoId++;
	} while (!fifoCreated);

	_valid = true;
	_alive = true;

	if (pthread_create(&_commThreadId, NULL, DConnexion::StaticCommLoop, this) != 0)
	{
		_server->logFile() << "Internal error: Unable to create communication thread" << std::endl;
		_valid = false;
	}
}

DConnexion::~DConnexion()
{
	_valid = false;
	pthread_mutex_lock(&_clientMutex);
	std::list<DClient*>::iterator it = _clientsList.begin();
	while(it != _clientsList.end())
	{
		it = _clientsList.erase(it);
	}
	pthread_mutex_unlock(&_clientMutex);

	pthread_mutex_destroy(&_clientMutex);

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
	if (_monitoringFifoFD != -1)
	{
		close(_monitoringFifoFD);
		_monitoringFifoFD = -1;
	}
	if (!_monitoringFifoName.empty())
	{
		unlink(_monitoringFifoName.c_str());
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

void* DConnexion::StaticCommLoop(void *p_connexion)
{
	DConnexion* connexion = (DConnexion*) p_connexion;
	if (!(connexion->_isMonitoring))
	{
		connexion->inputLoop();
	}
	else
	{
		connexion->monitoringLoop();
	}
}

void DConnexion::inputLoop()
{
	std::list<DClient*>::iterator itClient;

	char inputBuffer[FIFO_BUFFER];
	int nbRead;
	memset(inputBuffer, 0, FIFO_BUFFER);

	fd_set setOfFDs;
	int fdmax = (_serialFD < _internalPipeFDs[0])?_internalPipeFDs[0]:_serialFD;

	while (_valid)
	{
		FD_ZERO(&setOfFDs);
		FD_SET(_serialFD, &setOfFDs);
		FD_SET(_internalPipeFDs[0], &setOfFDs);
		select(fdmax+1, &setOfFDs, NULL, NULL, NULL);
		nbRead = read(_serialFD, inputBuffer, FIFO_BUFFER-1);

		if (FD_ISSET(_internalPipeFDs[0], &setOfFDs) || (nbRead <= 0))
		{
			/* on positionne pas la validite a faux ici, seulement quand on a plus de clients */
			break;
		}
		if (nbRead > 0)
		{
_server->logFile() << "nbRead: " << nbRead << std::endl;
			pthread_mutex_lock(&_clientMutex);
			itClient = _clientsList.begin();
			while(itClient != _clientsList.end())
			{
				(*itClient)->writeToInputFifo(inputBuffer, nbRead);
				itClient++;
			}
			pthread_mutex_unlock(&_clientMutex);
		}
	}

	/* On informe les clients de la rupture. Ils se deconnecteront */
	std::string messageStr("Rupture");
	pthread_mutex_lock(&_clientMutex);
	itClient = _clientsList.begin();
	while(itClient != _clientsList.end())
	{
		(*itClient)->sendFatal(messageStr);
		itClient++;
	}
	pthread_mutex_unlock(&_clientMutex);
}

void DConnexion::monitoringLoop()
{
	_monitoringFifoFD = open(_monitoringFifoName.c_str(), O_RDONLY);
	if (_monitoringFifoFD == -1)
	{
		_server->logFile() << "Internal error. Unable to open monitoring fifo" << std::endl;
		return;
	}

	char monitoringBuffer[FIFO_BUFFER];
	int nbRead;
	memset(monitoringBuffer, 0, FIFO_BUFFER);

	std::list<DClient*>::iterator itClient;

	while (_valid)
	{
		nbRead = read(_monitoringFifoFD, monitoringBuffer, FIFO_BUFFER-1);
		if (nbRead > 0)
		{
			std::string response;
			if (_monitoringLoopback)
			{
				if ((nbRead == 1) && (monitoringBuffer[0] == 0x03))
				{
					/* Ctrl+c => break => 0x03 */
					_monitoringLoopback = false;
					response = "Loopback stopped.\n";
				}
				else
				{
					response = std::string(monitoringBuffer, nbRead);
				}
			}
			else if ((nbRead == 1) && (monitoringBuffer[0] == 's'))
			{
				response = _server->getStatus();
			}
			else if ((nbRead == 1) && (monitoringBuffer[0] == 'l'))
			{
				_monitoringLoopback = true;
				response = "Loopback activated. Ctrl+C to stop\n";
			}
			if (response.length() > 0)
			{
				pthread_mutex_lock(&_clientMutex);
				itClient = _clientsList.begin();
				while(itClient != _clientsList.end())
				{
					(*itClient)->writeToInputFifo(response.c_str(), response.length());
					itClient++;
				}
				pthread_mutex_unlock(&_clientMutex);
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
	/* On regarde si cette connexion peut gerer ce client */
	if (!_valid)
	{
		return DCON_CLIENT_NOT_ADDED;
	}
	if (_isMonitoring && p_client->isMonitoring())
	{
		addClient(p_client);
		return DCON_CLIENT_ADDED;
	}
	else if (!_isMonitoring && !(p_client->isMonitoring()))
	{
		std::string line = p_client->getLine();
		int speed = readSpeed(p_client->getSpeed());
		std::string user = p_client->getUser();

		struct stat infoNewCli;
		struct stat infoCurCnx;

		if (0 != stat(line.c_str(), &infoNewCli))
		{
			/* Probleme d'acces a la ressource, le client est supprime */
			return DCON_INVALID_RESS;
		}
		if (0 != stat(_line.c_str(), &infoCurCnx))
		{
			/* La ressource de la connexion devrait etre valide */
			/* Si elle a ete rendue invalide depuis la creation de la connexion, pas de nouveaux clients */
			return DCON_CLIENT_NOT_ADDED;
		}

		/* Comparaison des inodes des ressources */
		if ((infoNewCli.st_dev == infoCurCnx.st_dev) &&
		    (infoNewCli.st_ino == infoCurCnx.st_ino))
		{
			/* la meme ressource */
			if (speed == _speed)
			{
				/* Parametre de connexion identique => OK */
				if (!user.empty())
				{
					/* si le client a un user identifie, refuser la connexion si le user est deja connecte */
					pthread_mutex_lock(&_clientMutex);
					std::list<DClient*>::iterator it = _clientsList.begin();
					bool found = false;
					while ((it != _clientsList.end()) && (!found))
					{
						if ((*it)->isValid())
						{
							if (0 == user.compare((*it)->getUser()))
							{
								found = true;
							}
						}
						it++;
					}
					pthread_mutex_unlock(&_clientMutex);
					if (found)
					{
						return DCON_USER_CONNECTED;
					}
				}
				addClient(p_client);
				return DCON_CLIENT_ADDED;
			}
			else
			{
				/* Parametre de connexion different => client supprime */
				return DCON_INCOMPAT_CONF;
			}
		}
		else
		{
			/* pas la meme ressource, cette connexion ne gere pas ce client */
			return DCON_CLIENT_NOT_ADDED;
		}
	}
	else
	{
		return DCON_CLIENT_NOT_ADDED;
	}
}

void DConnexion::addClient(DClient *p_client)
{
	std::string fifoNameStr;
	int fifoId = 0;
	bool fifoCreated = false;
	do {
		std::stringstream fifoName;
		fifoName << WORKING_DIRECTORY << "/" << "inputfifo" << fifoId;
		fifoNameStr = fifoName.str();

		fifoCreated = (mkfifo(fifoNameStr.c_str(), 0666) == 0);

		if (!fifoCreated && ((errno != EEXIST) || (fifoId > 512)))
		{
			std::string messageStr("Internal error: Unable to create fifo");
			p_client->sendFatal(messageStr);
			return;
		}
		fifoId++;
	} while (!fifoCreated);

	if (!_isMonitoring)
	{
		/* le client va lire sur fifoNameStr et ecrire directement sur la liaison _line */
		p_client->setFifos(fifoNameStr, _line);
	}
	else
	{
		/* le client va lire sur fifoNameStr et ecrire sur la fifo de monitoring */
		p_client->setFifos(fifoNameStr, _monitoringFifoName);
	}

	pthread_mutex_lock(&_clientMutex);
	_clientsList.push_back(p_client);
	pthread_mutex_unlock(&_clientMutex);

	std::stringstream message;
	message << "Connected to " << _line;
	std::string messageStr = message.str();
	p_client->sendInfo(messageStr);
}

bool DConnexion::openConnexion()
{
	if (_isMonitoring)
	{
		return true;
	}
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
	pthread_mutex_lock(&_clientMutex);
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
		if (pthread_create(&_closeThreadId, NULL, DConnexion::StaticCloseConnexion, this) != 0)
		{
			/* on arrive pas a creer le thread pour fermer proprement... on ferme comme on peut... */
			_server->logFile() << "Internal error: Unable to create disconnection thread" << std::endl;
			_alive = false;
			pthread_mutex_unlock(&_clientMutex);
			_server->handleConnexionClosed();
			return;
		}
	}
	pthread_mutex_unlock(&_clientMutex);
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
	/* on laisse 1 seconde au thread pour finir */
	date.tv_sec += 1;

	result = pthread_timedjoin_np(_commThreadId, NULL, &date);
	if (result != 0)
	{
		pthread_cancel(_commThreadId);
		_server->logFile() << "Warning: Force close communication thread of " << _line << std::endl;
	}
	_server->logFile() << "No more client. Connexion to " << _line << " closed" << std::endl;	
	_alive = false;
}

std::string DConnexion::getStatus()
{
	std::stringstream streamStatus;

	streamStatus << _line << ": ";
	if (_valid)
	{
		streamStatus << _clientsList.size() << " client(s): ";

		int nbAnonymes = 0;
		pthread_mutex_lock(&_clientMutex);
		std::list<DClient*>::iterator it = _clientsList.begin();
		while (it != _clientsList.end())
		{
			if ((*it)->isValid())
			{
				if (!((*it)->getUser()).empty())
				{
					streamStatus << (*it)->getUser() << ", ";
				}
				else
				{
					nbAnonymes++;
				}
			}
			it++;
		}
		pthread_mutex_unlock(&_clientMutex);
		streamStatus << nbAnonymes << " anonymous" << std::endl;
	}
	else
	{
		streamStatus << " closing..." << std::endl;
	}
	return streamStatus.str();
}

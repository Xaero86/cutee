#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>

#include "definition.h"
#include "DServer.h"
#include "AClient.h"

int main(int argc, char** argv)
{
	pid_t pid;

	/* Creation du repertoire de travail: mandatory */
	if (access(WORKING_DIRECTORY "/", R_OK | W_OK | X_OK) == -1)
	{
		if (errno != ENOENT)
		{
			std::cerr << "Unable to access working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
		else if (mkdir(WORKING_DIRECTORY, R_OK | W_OK | X_OK) == -1)
		{
			std::cerr << "Unable to create working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
		/* A cause du umask, on est oblige de rajouter les droits en deux temps */
		else if (chmod(WORKING_DIRECTORY, S_IRWXU | S_IRWXG | S_IRWXO))
		{
			std::cerr << "Unable to configure working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	/* Pour ne pas garder un processus zombie lors de la fin du processus intermediaire */
	signal(SIGCHLD, SIG_IGN);

	pid = fork();
	
	if (pid == 0)
	{
		/* process fils */
		/* Creation du daemon si necessaire */
		DServer::CreateAndStartDaemonServer(DAEMON_PORT, argc, argv);
	}
	else if (pid > 0)
	{
		/* process pere */
		/* connexion au daemon */
		AClient::CreateAndConnecteClient(DAEMON_PORT, argc, argv);
	}
	else
	{
		std::cerr << "Unable to fork" << std::endl;
		exit(EXIT_FAILURE);
	}
	return EXIT_SUCCESS;
}


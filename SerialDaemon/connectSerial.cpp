#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "definition.h"
#include "DServer.h"
#include "AClient.h"

int main(int argc, char** argv)
{
	pid_t pid;

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
		std::cerr << "Failed to fork" << std::endl;
		exit(EXIT_FAILURE);
	}
	return EXIT_SUCCESS;
}


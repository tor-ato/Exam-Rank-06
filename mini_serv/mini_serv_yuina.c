#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <netinet/in.h>

int	server_fd;
int id = 0;

typedef struct s_client
{
	int fd;
	int id;
	struct s_client *next;
}	t_client;

void	write_error(char *str)
{
	write(2, str, strlen(str));
}

void	Fatal_Error(void)
{
	write_error("Fatal error\n");
	exit(1);
}

void	Free_Close_All(t_client **client)
{
	close(server_fd);
	t_client	*current = *client;
	while (current)
	{
		t_client	*next = current->next;
		close(current->fd);
		free(current);
		current = next;
	}
}

t_client	*lastClient(t_client *client)
{
	t_client	*current = client;

	while (current && current->next)
		current = current->next;
	
	return current;
}

t_client	*createClient(t_client **client, int fd)
{
	t_client	*newClient = (t_client *)malloc(sizeof(t_client));
	if (newClient == NULL)
	{
		Free_Close_All(client);
		Fatal_Error();
		return (NULL);
	}

	newClient->fd = fd;
	newClient->next = NULL;
	if (*client == NULL)
	{
		newClient->id = id;
		id++;
		*client = newClient;
	}
	else
	{
		t_client	*last = lastClient(*client);
		newClient->id = id;
		id++;
		last->next = newClient;
	}

	return (newClient);
}

void	deleteClient(t_client **client, t_client *target)
{
	close(target->fd);
	t_client	*current = *client;
	t_client	*prev = NULL;

	while (current != NULL)
	{
		if(current->fd == target->fd)
		{
			if(prev ==  NULL)
			{
				*client = current->next;
			}
			else
			{
				prev->next = current->next;
			}
		}
		prev = current;
		current = current->next;
	}

	free(target);
}

void	init(char **argv)
{
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		Fatal_Error();
		return ;
	}

	struct sockaddr_in servaddr; 
	bzero(&servaddr, sizeof(servaddr)); 
	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1]));
	if (bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
	{
		close(server_fd);
		Fatal_Error();
		return ;
	} 

	if (listen(server_fd, 10) == -1)
	{
		close(server_fd);
		Fatal_Error();
		return ;
	}
}

int	getMaxFd(t_client *client)
{
	t_client	*current = client;
	int	maxFd = server_fd;

	while (current != NULL)
	{
		if (maxFd < current->fd)
			maxFd = current->fd;
		current = current->next;
	}

	return (maxFd);
}

void	sendClients(t_client *client, t_client *target, char *str)
{
	t_client	*current = client;
	while (current != NULL)
	{
		if (current->id != target->id && send(current->fd, str, strlen(str), 0) == -1)
		{
			Free_Close_All(&client);
			Fatal_Error();
			return ;
		}
		current = current->next;
	}
}

void	accept_client(t_client **client, fd_set *rfds)
{
	int client_fd = accept(server_fd, 0, 0);
	if (client_fd == -1)
	{
		Free_Close_All(client);
		Fatal_Error();
		return ;
	}
	FD_SET(client_fd, rfds);
	t_client *newClient = createClient(client, client_fd);
	char buff[50];
	sprintf(buff, "server: client %d just arrived\n", newClient->id);
	sendClients(*client, newClient, buff);
}

void	handle_client(t_client **client, t_client *target, fd_set *rfds)
{
	char	buff[1000000];
	memset(buff, 0, sizeof(buff));
	int bytes = 1000;
	int total_bytes = 0;
	while (bytes == 1000 || buff[strlen(buff) - 1] != '\n')
	{
		bytes = recv(target->fd, buff + strlen(buff), 1000, 0);
		total_bytes += bytes;
		if (bytes <= 0)
			break ;
	}

	if (total_bytes == -1)
	{
		Free_Close_All(client);
		Fatal_Error();
		return ;
	}
	else if (total_bytes == 0)
	{
		char	buffer[50];
		sprintf(buffer, "server: client %d just left\n", target->id);
		sendClients(*client, target, buffer);
		FD_CLR(target->fd, rfds);
		deleteClient(client, target);
	}
	else
	{
		buff[total_bytes] = '\0';
		char buffer[total_bytes + 16];
		memset(buffer, 0, total_bytes + 16);
		int i = 0;
		int	index = 0;
		while (i <= total_bytes)
		{
			if (buff[i] == '\n')
			{
				char	str[strlen(buffer) + 16];
				sprintf(str, "client %d: %s\n", target->id, buffer);
				sendClients(*client, target, str);
				memset(buffer, 0, total_bytes + 16);
				index = 0;
			}
			else
			{
				buffer[index] = buff[i];
				index++;
			}
			i++;
		}
		if (strlen(buffer) != 0)
		{
			char	str[strlen(buffer) + 16];
			sprintf(str, "client %d: %s\n", target->id, buffer);
			sendClients(*client, target, str);
		}
	}
}

void	run(void)
{
	fd_set	rfds;
	FD_ZERO(&rfds);
	FD_SET(server_fd, &rfds);
	t_client	*client = NULL;

	while (true)
	{
		fd_set	copy_rfds = rfds;
		if (select(getMaxFd(client) + 1, &copy_rfds, NULL, NULL, NULL) == -1)
		{
			Free_Close_All(&client);
			Fatal_Error();
			return ;
		}
		if (FD_ISSET(server_fd, &copy_rfds))
		{
			accept_client(&client, &rfds);
		}
		else
		{
			t_client	*current = client;
			while (current != NULL)
			{
				t_client *next = current->next;
				if (FD_ISSET(current->fd, &copy_rfds))
					handle_client(&client, current, &rfds);
				current = next;
			}
		}
	}
	Free_Close_All(&client);
}

int	main(int argc, char **argv)
{
	if (argc != 2)
	{
		write_error("Wrong number of arguments\n");
		exit(1);
		return (1);
	}

	init(argv);
	run();
	return (0);
}

#include "common.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <thread>
#include <pthread.h>
#include "NRC.h"
using namespace std;

/* client constructor */
NRC::NRC(const string hostName, const string portNumber)
{
	struct addrinfo hints, *res;

	// first, load up address structs with getaddrinfo():
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int status;
	//getaddrinfo("www.example.com", "3490", &hints, &res);
	if ((status = getaddrinfo(hostName.c_str(), portNumber.c_str(), &hints, &res)) != 0) {
        cerr << "getaddrinfo: " << gai_strerror(status) << endl;
		exit(0);
    }

	// make a socket:
	this->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd < 0)
	{
		perror ("Cannot create socket");
		exit(0);
	}

	// connect!
	if (connect(this->sockfd, res->ai_addr, res->ai_addrlen)<0)
	{
		perror ("Cannot Connect");
		exit(0);
	}
//	cout << "Successfully connected to " << hostName << endl;
//	cout << "Now Attempting to send a message to "<< hostName << endl;
	char buf [1024];
	sprintf (buf, "hello");
	send (sockfd, buf, strlen (buf)+1, 0);
	recv (sockfd, buf, 1024, 0);
	//cout << "Received " << buf << " from the server" << endl;
}

/* server constructor */
NRC::NRC(const string portNumber, void (*handle_process_loop)(NRC *))
{
    struct addrinfo hints, *serv;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, portNumber.c_str(), &hints, &serv)) != 0) {
        cerr  << "getaddrinfo: " << gai_strerror(rv) << endl;
        exit(0);
    }
	if ((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
        perror("server: socket");
        exit(0);
    }
    if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
		close(sockfd);
		perror("server: bind");
        exit(0);
	}
    freeaddrinfo(serv); // all done with this structure

    if (listen(sockfd, 20) == -1) {
        perror("listen");
        exit(1);
    }

	cout << "server: waiting for connections..." << endl;
	char buf [1024];
	while(1)
	{  // main accept() loop
        sin_size = sizeof their_addr;
        int slave_socket = accept(this->sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (slave_socket == -1) {
            perror("accept");
            continue;
        }
		NRC* slaveChannel = new NRC (slave_socket);
        thread client_thread (handle_process_loop, slaveChannel);
		client_thread.detach();
    }
}

/* special constructor */
NRC::NRC(int fd)
{
	this->sockfd = fd;
}

NRC::~NRC()
{
	close(sockfd);
}

char* NRC::cread(int *len)
{
	char * buf = new char [MAX_MESSAGE];
	int length;
	length = recv(this->sockfd, buf, MAX_MESSAGE, NULL);
	if (len)	// the caller wants to know the length
		*len = length;
	return buf;
}

int NRC::cwrite(char* msg, int len)
{
	if (len > MAX_MESSAGE){
		EXITONERROR("cwrite");
	}
	if (send(sockfd, msg, len, NULL) < 0){
		EXITONERROR("cwrite");
	}
	return len;
}


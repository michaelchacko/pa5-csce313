
#ifndef _NRC_H_
#define _NRC_H_

#include "common.h"

class NRC
{
private:
	/* Socket file descriptor */
	int sockfd;

public:

	/* Client Constructor */
	NRC(const string hostName, const string portNumber); //NRC(localhost, 80)

	/* Server Constructor */
	NRC(const string portNumber, void (*handle_process_loop)(NRC *));

	/* special constructor */
	NRC(int fd);

	/* Destructor of the local copy of the bus. By default, the Server Side deletes any IPC
	 mechanisms associated with the channel. */
	~NRC();

	char* cread(int *len=NULL);
	/* Blocking read of data from the channel. Returns a string of characters
	 read from the channel. Returns NULL if read failed. */

	int cwrite(char *msg, int msglen);
	/* Write the data to the channel. The function returns the number of characters written
	 to the channel. */
};

#endif

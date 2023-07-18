/* Client side implementation of UDP client-server model with Go-Back-N protocol for the
exchange of messages 
Author: Giovanni Pica */

#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <sys/mman.h>
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/time.h>

#define PORT	 8080 // default port for client-server connection
#define LOSS 25 // loss probability
#define TIMEOUT 400 // seconds between retransmit
#define WINDOWSIZE 5 // is the window size
#define MSS 536 // minimum Maximum segment size 


int n; // Variable for the control of sendto
int sockfd; // Socket file descriptor
char buffer[MSS + 1]; // Buffer for messages
char **buff_file; // buffer for contain packets for upload the new file in the directory
struct sockaddr_in servaddr; // Support struct for the socket
int pkts; // number of packets to send and receive
int base; // oldest packet sent but not acked
int nextseqnum; //  next packet to send
int size; // size of the file to open
double sampleRTT; // time between the transmission of one segment and the reception of ACK
double estimatedRTT; // estimated round trip time
double alpha; // shift right R >> 3 so 1/8
double devRTT; // estimate how much sampleRTT deviates from estimatedRTT
double beta; // shift right R >> 2 so 1/4
double Tout; // Is the timeout interval that is equals to mu + z-value * sigma, with mu = estimatedRTT, z-value = 4, sigma = devRTT, in the begin it is the prefixed timeout

// Declare functions
void list_files();
void get();
void upload();
void read_content_file();
void gbnRecv();
void gbnSend();
void timeout();
void stopTimeout();

int main() { 
	int choice;
	int len;

	// Creating socket file descriptor 
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 

	// Reset servaddr
	memset((void *) &servaddr, 0, sizeof(servaddr)); 
	
	// Filling server information 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_port = htons(PORT); 
	servaddr.sin_addr.s_addr = INADDR_ANY; // Any address or use specific address 

	// Reset buffer
	memset(buffer, 0, sizeof(buffer)); 

	// Cicle for the options of client
	while(1){

		// Reset buffer
		memset(buffer, 0, sizeof(buffer));
		Tout = TIMEOUT;
		alpha = 0.125;
		beta = 0.25;

		// Possible choices
		printf("\nHi Client! What can i do for you?\n1) Exit;\n2) List;\n3) Get;\n4) Upload;\n");
		printf("\nOption: ");
		scanf("%d", &choice);
		
		// Switch based to the choice 
		switch(choice){
			case 1:

				// Write message to buffer and send to client
				snprintf(buffer, sizeof(buffer), "%s", "Exit");
				n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr*) &servaddr, sizeof(servaddr));
		
				// Check if sendto fails
				if (n < 0){
					perror("Sending Exit message failure from the Client\n");
					exit(-1);
				} 
				WAIT:

				// Reset buffer
				memset(buffer, 0, sizeof(buffer)); 

				// Read returned message from Server (ACK)
				n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

				// Check if recvfrom fails
				if (n < 0){
					if (errno == EAGAIN){
					goto WAIT;
					}
					else{
						perror("recvfrom gbn client fails\n");
						exit(-1);
					}
				}
				if (n > 0){

					// Null terminator
					buffer[n] = 0;

					// Check if ack is Exit, close connection
					if ((strncmp(buffer, "Exit", 4)) == 0){
						printf("Server: Closing connection.\n");
						printf("Client: Exiting...\n");

						// Close socket
						close(sockfd);
						exit(0);
					}
					else{
						printf("Client: Error Server did not acknowledge exit. Force closing connection...\n");
						exit(0);
					}
				}
			case 2:

				// Write list message on buffer
				snprintf(buffer, sizeof(buffer), "%s", "List");
				list_files();
				break;
			case 3:

				// Write get message on buffer
				snprintf(buffer, sizeof(buffer), "%s", "Get");
				get();
				break;
			case 4:

				// Write upload message on buffer
				snprintf(buffer, sizeof(buffer), "%s", "Upload");
				upload();
				break;
			default:
				printf("Input error, you must insert one of these options\n");
				break;
		}
	}
	return 0; 
} 

// Function list_files that send the message list to the server and print its files
void list_files(){
	printf("Client: Request list...\n");

	//Send to server what client wanna do
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if(n < 0){
		perror("Sending List message failure from the Client\n");
		exit(-1);
	}

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Wait the response from the server and it will return the list of files
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		perror("recvfrom error list client\n");
		return;
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}

	// Ack list files
	printf("Server: List of files:\n----------------------------------\n%s\n----------------------------------\n", buffer);
}

// Function get that send the message get with the name of the file to the server and print its content 
void get(){
	printf("Client: Download a file...\n");
	char fileName[MSS];

	// Sending get message to the server
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr*) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if (n < 0){
		perror("Sending get message client failure\n");
		exit(-1);
	}

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Insert the name of the file
	WRITE:
	printf("Please insert the name of the file: \n");
	scanf("%s", buffer);

	// Send to the server the name of the file
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if(n < 0){
		perror("Sending file message failure from the Client\n");
		exit(-1);
	}

	// Save the file name
	snprintf(fileName, MSS, "%s", buffer);

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Receive the response message from the server
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if rcvfrom fails
	if (n < 0){
		perror("rcvfrom get client error\n");
		exit(-1);
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;	
	}
	// Case file not exists
	if ((strncmp(buffer, "NotExists", 9)) == 0){
		printf("Server: File doesn't exists\n");

		// Reset buffer
		memset(buffer, 0, sizeof(buffer));
		goto WRITE;
	}
	printf("Server: The file %s exists\n", buffer);

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));
	printf("Client: Request the size of the file...\n");

	// Send to the server the request of the size of the file
	snprintf(buffer, sizeof(buffer), "%s", "Size?");
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
	
	// Check if sendto fails
	if (n < 0){
		perror("Sending size message client failed\n");
	}

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));

	// Receive the response message with the size of the file
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		perror("recvfrom size fails client\n");
		exit(-1);
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}
	size = atoi(buffer);
	printf("Server: The size of the file is %d\n", size);

	// Allocate memory for contain packet in buff_file
	buff_file = malloc(size);

	// Shared memory for contain the message of the packet
	pkts = (ceil((double) (atoi(buffer)/MSS)))  + 1;
	for (int i = 0; i < pkts; i++){
		buff_file[i] = mmap(NULL, MSS, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, 0, 0);
		if (buff_file[i] == NULL){
			perror("mmap error\n");
			exit(-1);
		}
	}

	// Copy content of the file when packets gbn have received
	int file = open(fileName, O_CREAT|O_RDWR, 0666);
	if (file == -1){
		perror("open file error\n");
		exit(-1);
	}

	// Now gbnReceive function
	gbnRecv();

	// Now write all packets in the file
	for(int i = 0; i < pkts; i++){
		int ret = write(file, buff_file[i], strlen(buff_file[i]));
		if (ret == -1){
			perror("write file error\n");
			exit(-1);
		}
	}
	printf("Download successful!\n");
}

void upload(){
	printf("Client: Upload a file...\n");

	// Sending upload message to the server
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr*) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if (n < 0){
		perror("Sending get message client failure\n");
		exit(-1);
	}

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Insert the name of the file
	WRITE:
	printf("Please insert the name of the file: \n");
	scanf("%s", buffer);

	// Open file
	int fd = open(buffer, O_RDONLY, 0666);

	// Check if file exists
	if (fd == -1){
		printf("file doesn't exists!\n");
		goto WRITE;
	}

	// Send to the server the name of the file
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if(n < 0){
		perror("Sending file message failure from the Client\n");
		exit(-1);
	}
	printf("File %s exists\n", buffer);

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Receive the size message from the client
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		perror("recvfrom size message fails\n");
		exit(-1);
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}
	printf("Server: Requesting -> %s\n", buffer);

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Calculate size of the file
	size = lseek(fd, 0, SEEK_END);
	pkts = (ceil((double) (size/MSS))) + 1;
	printf("Number of packets to send -> %d\n", pkts);
	printf("Size of the file -> %d\n", size);
	lseek(fd, 0, SEEK_SET);

	// Send the size to the server
	snprintf(buffer, sizeof(buffer), "%d", size);
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if (n < 0){
		perror("sendto size message error\n");
		exit(-1);
	}
	
	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Now gbnSend function
	gbnSend(fd);
	printf("Upload successful!\n");
}

// Function that manage the reception of file packets
void gbnRecv(){
	printf("Client: Receiving file packets...\n");
	nextseqnum = 0;
	printf("---------------------------------\n");
	while(1){
		WAIT:

		// Reset buffer 
		memset(buffer, 0, sizeof(buffer));

		// Receive the number of packet
		n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

		// Check if recvfrom fails
		if (n < 0){
			if (errno == EAGAIN){
				goto WAIT;
			}
			else{
				perror("recvfrom gbn client fails\n");
				exit(-1);
			}
		}
		if (n > 0){

			// Null terminator
			buffer[n] = 0;
		}
		int num = atoi(buffer);
		WAIT2:

		// Reset buffer 
		memset(buffer, 0, sizeof(buffer));

		// Receive the content of packet
		n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

		// Check if recvfrom fails
		if (n < 0){
			if (errno == EAGAIN){
				goto WAIT2;
			}
			else{
				perror("recvfrom gbn client fails\n");
				exit(-1);
			}
		}
		if (n > 0){

			// Null terminator
			buffer[n] = 0;
			printf("Server: Send packet %d that contains:\n-------------------------------\n%s\n-------------------------------\n", num, buffer);
			
			// Write the content on buff_file
			snprintf(buff_file[num], MSS, "%s", buffer);
		}

		// Expected frame
		if (num == nextseqnum){
			
			// Reset buffer 
			memset(buffer, 0, sizeof(buffer));
		
			// Send the ACK to the server with loss probability
			int random = rand()%100 + 1;
			if (random > LOSS){				
				printf("Client: Sending ack %d\n", nextseqnum);
				snprintf(buffer, sizeof(buffer), "%d", nextseqnum);
				n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

				// Check if sendto fails
				if (n < 0){
					perror("Sending packet number ACK failure\n");
					exit(-1);
				}
				
				// Last frame
				if (nextseqnum >= pkts - 1){
					break;
				}
				
				// Reset buffer 
				memset(buffer, 0, sizeof(buffer));	
			}
			else{
				printf("Client: Lost Ack for packet %d\n", nextseqnum);
			}
			nextseqnum++;		
		}

		// Out of order
		else{
			printf("Out of Order\n");
			printf("Client: Sending ack %d\n", nextseqnum - 1);

			// Reset buffer 
			memset(buffer, 0, sizeof(buffer));
		
			// Send the ACK to the server with loss probability
			int random = rand()%100 + 1;
			if (random > LOSS){				
				snprintf(buffer, sizeof(buffer), "%d", nextseqnum - 1);
				n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

				// Check if sendto fails
				if (n < 0){
					perror("Sending packet number ACK failure\n");
					exit(-1);
				}

				// Last frame
				if (nextseqnum - 1 >= pkts - 1){
					break;
				}

				// Reset buffer 
				memset(buffer, 0, sizeof(buffer));
			}
			else{
				printf("Client: Lost Ack for packet %d\n", nextseqnum - 1);
			}
		}
	}

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));
	printf("---------------------------------\n");
}

// Function that manage the sending of the file packets with the protocol GoBackN
void gbnSend(int f){
	printf("Client: Sending file packets...\n");
	
	// Call these for measure the RTT and also the time of the entire operation
	struct timeval begin, end, RTTstart, RTTend;

	// Array of strings to contain the content of the chunks
	char send_pkts[pkts][MSS];

	// buffer for contain rentrasmission packets that they are not in the calculation of the sampleRTT
	int retransmissions[pkts];

	// buffer for contain ack received for control if an ack is received twice and so it is useless for the calculation of sampleRTT
	int received_ack[pkts];

	// Buffer for contain file content and it has the size of a packet
	char *temp;
	temp = malloc(MSS);

	// Fill the struct
	for(int i = 0; i < pkts; i++){
		memset(temp, 0, MSS);
		read(f, temp, MSS - 1);
		snprintf(send_pkts[i], MSS, "%s", temp); // fill the struct with the message
		retransmissions[i] = -1; // set -1 if isn't a retransmission
		received_ack[i] = 0; // set ack received to 0, it increment when an ack is received
	}
	base = 0;
	nextseqnum = 0;
	printf("---------------------------------\n");
	gettimeofday(&begin, NULL); // start timer for performance
	while(1){

		// Send all the packets in the window
		while(nextseqnum < base + WINDOWSIZE && nextseqnum < pkts){

			// Reset buffer
			memset(buffer, 0, sizeof(buffer));

			// Sending packet with loss probability
			int random = rand()%100 + 1;
			if(random > LOSS){

				gettimeofday(&RTTstart, NULL); // start timer RTT

				// Send the number of the packet
				snprintf(buffer, sizeof(buffer), "%d", nextseqnum);
				n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

				// Check if sendto fails
				if (n < 0){
					perror("sendto packet message fails\n");
					exit(-1);
				}
				
				// Send message 
				n = sendto(sockfd, send_pkts[nextseqnum], MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
				printf("Client: Sending packet %d\n", nextseqnum);

				// Check if sendto fails
				if (n < 0){
					perror("sendto packet message fails\n");
					exit(-1);
				}
			}
			else{
				printf("Client: Lost packet %d\n", nextseqnum);
			}
			nextseqnum++;
		}

		// Reset buffer
		memset(buffer, 0, sizeof(buffer));

		// Start timer
		timeout();

		//Received Ack
		n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

		// Check if recvfrom fails
		if (n < 0){ 

			// EAGAIN if no data is received in the fixed timeout 
			if (errno == EAGAIN){
				
				// Reset buffer
				memset(buffer, 0, sizeof(buffer));

				// Start timer
				timeout();

				// Send all the sent but not acked packets
				for(int i = base; i < nextseqnum; i++){
					printf("Client: Timeout packet %d\n", i);
					int random = rand()%100 + 1;
					if (random > LOSS){

						// Reset buffer
						memset(buffer, 0, sizeof(buffer));
						
						// Send the number of the packet
						snprintf(buffer, sizeof(buffer), "%d", i);
						int n1;
						n1 = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

						// Check if sendto fails
						if (n1 < 0){
							perror("sendto packet message fails\n");
							exit(-1);
						}

						// Send the content
						n1 = sendto(sockfd, send_pkts[i], MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

						// Check if sendto fails
						if (n1 < 0){
							perror("sendto packet message fails\n");
							exit(-1);
						}
					}
					else{
						printf("Client: Lost packet %d\n", i);
					}
					printf("Client: Resending packet %d\n", i);
					retransmissions[i] = i;
				} 
			}
		}
		if (n > 0){

			// Null terminator
			buffer[n] = 0;
			printf("Server: Ack packet %s\n", buffer);
			
			// increment the ack received
			received_ack[atoi(buffer)]+=1;

			// Calculate sampleRTT only for no retransmission packet
			if (retransmissions[atoi(buffer)] == -1 && received_ack[atoi(buffer)] == 1){
				gettimeofday(&RTTend, NULL); // stop timer when ack received
				sampleRTT = ((double)RTTend.tv_sec - RTTstart.tv_sec) * 1000000 + ((double)RTTend.tv_usec - RTTstart.tv_usec); // microseconds
			}

			// Now calculate the timeout
			estimatedRTT = (1 - alpha) * estimatedRTT + alpha * sampleRTT;
			devRTT = (1 - beta) * devRTT + beta * (abs(sampleRTT - estimatedRTT));
			Tout = estimatedRTT + 4 * devRTT;
			printf("TIMEOUT IS -> %f us\n", Tout);
			base = atoi(buffer) + 1;

			// If this is the last ack, stop
			if (base >= pkts){
				break;
			}
		}
		if (base == nextseqnum){
			
			// Stop timer
			stopTimeout();
		}
		else{

			// Start timer
			timeout();
		}
	}
	gettimeofday(&end, NULL);

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));
	printf("---------------------------------\n");
	printf("Upload total time: %f s\n", ((double) end.tv_usec - begin.tv_usec) / 1000000 + ((double) end.tv_sec - begin.tv_sec));
}

// Function that start the timer of the packet
void timeout(){
	struct timeval timeout;
    timeout.tv_sec = 0;
    //timeout.tv_usec = TIMEOUT;
    timeout.tv_usec = Tout; // adaptive case
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))){
    	perror("Error in getsockOpt\n");
    	exit(-1);
    }
}

// Function that stop the timer of the packet
void stopTimeout(){
	struct timeval time_val_struct;
	time_val_struct.tv_sec = 0;
	time_val_struct.tv_usec = 0;
	if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &time_val_struct, sizeof(time_val_struct))){
    	perror("Error in getsockOpt\n");
    	exit(-1);
    }
}
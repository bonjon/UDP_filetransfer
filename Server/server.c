/* Server side implementation of UDP client-server model with Go-Back-N protocol for the
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
#define TIMEOUT 400 // seconds (micro) between retransmit
#define WINDOWSIZE 5 // is the window size
#define MSS 536 // minimum Maximum segment size 

int n; // Variable for the control of sendto and recvfrom
int sockfd; // Socket file descriptor
char buffer[MSS + 1]; // Buffer for messages
char **buff_file; // buffer for contain packets for upload the new file in the directory
struct sockaddr_in servaddr; // Support struct for the socket
int size; // File size to transfer
int pkts; // number of packets
int base; // oldest packet sent but not acked
int nextseqnum; // next packet to send
double sampleRTT; // time between the transmission of one segment and the reception of ACK
double estimatedRTT; // estimated round trip time
double alpha; // shift right R >> 3 so 1/8
double devRTT; // estimate how much sampleRTT deviates from estimatedRTT
double beta; // shift right R >> 2 so 1/4
double Tout; // Is the timeout interval that is equals to mu + z-value * sigma, with mu = estimatedRTT, z-value = 4, sigma = devRTT, in the begin it is the prefixed timeout

// Declare functions
void list();
void sendFile();
void putFile();
void gbnSend();
void gbnRecv();
void timeout();
void stopTimeout();

int main() { 

	// Creating socket file descriptor 
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 

	// Filling server information 
	servaddr.sin_family = AF_INET; // IPv4 
	servaddr.sin_addr.s_addr = INADDR_ANY; 
	servaddr.sin_port = htons(PORT); 
	
	// Bind the socket with the server address 
	if (bind(sockfd, (const struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) { 
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	} 
	while(1){
		printf("Hi Server!\n");
		int len = sizeof(servaddr);
		Tout = TIMEOUT;
		alpha = 0.125;
		beta = 0.25;	

		// Reset buffer
		memset(buffer, 0, sizeof(buffer)); 
		CLEAN:
		fflush(stdout);
		memset(buffer, 0, sizeof(buffer));

		// Receive request from client 
		n = recvfrom(sockfd, buffer, MSS, 0, (struct sockaddr *) &servaddr, &len);

		// Check if recvfrom failed
		if (n < 0){
			if (errno == EAGAIN){
				goto CLEAN;
			}
			else{
				perror("error recvfrom in while server\n");
				exit(-1);
			}
		}

		// Copy to buffer
		buffer[n] = 0;
		printf("Client: Requesting %s\n", buffer);

		// Exit client
		if ((strncmp(buffer, "Exit", 4)) == 0){

			// Send exit message
			n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
			
			// Check if sendto fails
			if (n < 0){
				perror("Server sendto exit fails\n");
				exit(-1);
			}
			printf("Server: Exiting...\n");
			exit(0);
		}

		// List client
		if((strncmp(buffer, "List", 4)) == 0){
			list();
		}

		// Get client
		if ((strncmp(buffer, "Get", 3)) == 0){
			sendFile();
		}

		// Upload client
		if ((strncmp(buffer, "Upload", 6)) == 0){
			putFile();
		}
	}
	return 0; 
} 

// Function list that send the list at client
void list(){

	// File descriptor
	int fd;

	// Open file ReadOnly
	fd = open("list.txt", O_RDONLY, 0666);

	// Check if open fails
	if (fd == -1){
		printf("Open list file error\n");
		return;
	}

	// Dim file
	size = lseek(fd, 0, SEEK_END);
	if (size < 0){
		printf("Size list file error\n");
		return;
	}

	// Riposition of the file head
	lseek(fd, 0, SEEK_SET);

	// Write on buff the content of the file
	while((read(fd, buffer, size - 1)) == -1){
		if (errno != EINTR){
			printf("Error content list file\n");
			return;
		}
	}

	// Send the list at client
	int len = sizeof(servaddr);
	while((sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, len)) == -1){
		if (errno != EINTR){
			printf("Error socket list file\n");
			return;
		}
	}
	printf("Server: Sending list...\n");
}

void sendFile(){
	WAIT:

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Receive from client the file to open
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		if (errno == EAGAIN){
			goto WAIT;
		}
		else{
			perror("recvfrom file name error\n");
			exit(-1);
		}
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}
	printf("Client: Request filename -> %s\n", buffer);

	// Now open the file
	int fd = open(buffer, O_RDONLY, 0666);

	// Check if filename not found
	if (fd == -1){
		printf("Client send a file that doesn't exists!\n");

		// Reset buffer
		memset(buffer, 0, sizeof(buffer));

		// Send not exists message to client
		snprintf(buffer, sizeof(buffer), "%s", "NotExists");
		n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

		// Check if sendto fails
		if (n < 0){
			perror("notexists send to fails\n");
			exit(-1);
		}

		// Reset buffer
		memset(buffer, 0, sizeof(buffer));
		goto WAIT;
	}

	// Send the file name to client
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));

	// Check if sendto fails
	if (n < 0){
		perror("filename sendto fails\n");
		exit(-1);
	}

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
	printf("Client: Requesting -> %s\n", buffer);

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Calculate size of the file
	size = lseek(fd, 0, SEEK_END);
	pkts = (ceil((double) (size/MSS))) + 1;
	printf("Number of packets to send -> %d\n", pkts);
	printf("Size of the file -> %d\n", size);
	lseek(fd, 0, SEEK_SET);

	// Send the size to the client
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
	printf("Download successful!\n");
}

void putFile(){
	char fileName[MSS];
	WAIT: 

	// Reset buffer
	memset(buffer, 0, sizeof(buffer));

	// Receive from client the name of the file to upload
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		if (errno == EAGAIN){
			goto WAIT;
		}
		else{
			perror("recvfrom file name error\n");
			exit(-1);
		}	
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}
	printf("Client: Request filename -> %s\n", buffer);

	// Save the file name
	snprintf(fileName, MSS, "%s", buffer);

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));
	printf("Server: Request the size of the file...\n");

	// Send to the client the request of the size of the file
	snprintf(buffer, sizeof(buffer), "%s", "Size?");
	n = sendto(sockfd, buffer, MSS, 0, (const struct sockaddr *) &servaddr, sizeof(servaddr));
	
	// Check if sendto fails
	if (n < 0){
		perror("Sending size message server failed\n");
	}

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));

	// Receive the response message with the size of the file
	n = recvfrom(sockfd, buffer, MSS, 0, NULL, NULL);

	// Check if recvfrom fails
	if (n < 0){
		perror("recvfrom size fails server\n");
		exit(-1);
	}
	if (n > 0){

		// Null terminator
		buffer[n] = 0;
	}
	size = atoi(buffer);
	printf("Client: The size of the file is %d\n", size);
	pkts = (ceil((double) (atoi(buffer)/MSS))) + 1;
	printf("Packets are %d\n", pkts);

	// Allocate memory for contain packet in buff_file
	buff_file = malloc(size);

	// Shared memory for contain the message of the packet
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

	// Now gbnRecv funtion
	gbnRecv();

	// Now write all packets in the file
	for(int i = 0; i < pkts; i++){
		int ret = write(file, buff_file[i], strlen(buff_file[i]));
		if (ret == -1){
			perror("write file error\n");
			exit(-1);
		}
	}

	// Update list file
	FILE *f;
	f = fopen("list.txt", "a");
	if (f == NULL){
		perror("fopen error\n");
		exit(-1);
	}
	char t[MSS];
	while(fscanf(f, "%s", t) == 1){
		if(strstr(t, fileName) == 0)	
			fprintf(f, "%s\n", fileName);
	}
	fclose(f);
	printf("File list updated\n");
	printf("Upload successful!\n");
}

// Function that manage the sending of the file packets with the protocol GoBackN
void gbnSend(int f){
	printf("Server: Sending file packets...\n");
	
	// Call these for measure the RTT and also the time of the entire operation
	struct timeval begin, end, RTTstart, RTTend;

	// buffer for contain rentrasmission packets that they are not in the calculation of the sampleRTT
	int retransmissions[pkts]; 

	// buffer for contain ack received for control if an ack is received twice and so it is useless for the calculation of sampleRTT
	int received_ack[pkts];

	// Array of strings to contain the content of the chunks
	char send_pkts[pkts][MSS];

	// Buffer for contain file content and it has the size of a packet
	char *temp;
	temp = malloc(MSS);

	// Fill the struct
	for(int i = 0; i < pkts; i++){
		memset(temp, 0, MSS);
		read(f, temp, MSS - 1);
		snprintf(send_pkts[i], MSS, "%s", temp); // fill the struct with the message
		retransmissions[i] = -1; // setting no retransmission to -1
		received_ack[i] = 0; // setting ack received to 0 and it increment when the ack is received
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
				printf("Server: Sending packet %d\n", nextseqnum);

				// Check if sendto fails
				if (n < 0){
					perror("sendto packet message fails\n");
					exit(-1);
				}
			}
			else{
				printf("Server: Lost packet %d\n", nextseqnum);
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
					printf("Server: Timeout packet %d\n", i);
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
						printf("Server: Lost packet %d\n", i);
					}
					printf("Server: Resending packet %d\n", i);
					retransmissions[i] = i;
				} 
			}
		}
		if (n > 0){

			// Null terminator
			buffer[n] = 0;
			printf("Client: Ack packet %s\n", buffer);

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
	printf("Download total time: %f s\n", ((double) end.tv_usec - begin.tv_usec) / 1000000 + ((double) end.tv_sec - begin.tv_sec));
}

// Function that manage the reception of file packets
void gbnRecv(){
	printf("Server: Receiving file packets...\n");
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
			printf("Client: Send packet %d that contains:\n-------------------------------\n%s\n-------------------------------\n", num, buffer);
		
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
				printf("Server: Sending ack %d\n", nextseqnum);
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
				printf("Server: Lost Ack for packet %d\n", nextseqnum);
			}
			nextseqnum++;		
		}

		// Out of order
		else{
			printf("Out of order\n");
			printf("Server: Sending ack %d\n", nextseqnum - 1);

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
				printf("Server: Lost Ack for packet %d\n", nextseqnum - 1);
			}
		}
	}

	// Reset buffer 
	memset(buffer, 0, sizeof(buffer));
	printf("---------------------------------\n");
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

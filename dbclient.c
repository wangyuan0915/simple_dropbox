/***** inetclient.c *****/ 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <netdb.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>


#include "message.h"
#include "wrapsock.h"


ssize_t Readn(int fd, void *ptr, size_t nbytes);
void Writen(int fd, void *ptr, size_t nbytes);


//received file and change its modfoed time
int receive_file(int sockfd, char *path, int size, long int modifiedTime){

	int readen = 0; //how long have already read
	char buffer[CHUNKSIZE + 1];
	FILE *fp;
	struct stat filestatus;
    struct utimbuf newtime;

	//Open the file. Overwite if it already exists.
	// if((fp = fopen(path, "w+")) == NULL) {
	// 	perror("fopen");
	// 	printf("update error: %s\n", path);
	// 	printf("%lu\n", strlen(path));
	// 	exit(1);
 	//    }
	fp = fopen(path, "w+");
	int bufsize = size;

	//write max chunksize one time;
	if(bufsize > CHUNKSIZE) bufsize = CHUNKSIZE;

	//length of every time read
	int length;
	while(readen < size)
	{

		bufsize = size - readen;
		if (bufsize > CHUNKSIZE) bufsize = CHUNKSIZE;

		length = Readn(sockfd, &buffer, bufsize);	
		readen += length;
		fwrite(buffer, length, 1, fp);
		if(ferror(fp))
		{
    		fprintf(stderr, "write error\n");
			Close(sockfd);
			exit(1);
		}
	
	}

		
	
	//get file status
	if(stat(path, &filestatus) < 0) 
	{
    	perror("stat");
    	exit(1);
    }	
	
	//Update the the modified the time make it same as in the server
	//Access time.
	newtime.actime = filestatus.st_atime; 
	newtime.modtime = (time_t) modifiedTime;
	
	//set time
	if(utime(path, &newtime) < 0) 
	{
    	perror("utime");
    	exit(1);
    }

    //for save so close must close here??
	fclose(fp);		

    return 0;
}


//send the file to server
int send_file(int sockfd, char *path)
{
    FILE *fp;
    char buf[CHUNKSIZE + 1];
    int bits;

    if ((fp = fopen(path, "r+")) == NULL)
    {
        perror("fopen");
        exit(1);
    }

    
    //read buf and write to socket
    while((bits = fread(buf, 1, CHUNKSIZE,fp)) > 0)
    {
        Writen(sockfd,&buf, bits);
    }

    if((fclose(fp))) 
    {
        perror("fclose");
        exit(1);
    }

    return 0;
}



//need to receive new file from server
int new_file(int sockfd, char *filepath)
{
	

	struct sync_message sendEmptyMessage, serverEmptyEessage;
	char path[MAXNAME + 1];


	while(1)
	{
		
		//send empty message
		sendEmptyMessage.mtime = -1;
		sendEmptyMessage.size = 0;
		strncpy(sendEmptyMessage.filename, "\0", 1);
		
		Writen(sockfd, &sendEmptyMessage, sizeof(sendEmptyMessage));
		
		if((Readn(sockfd, &serverEmptyEessage, sizeof(serverEmptyEessage))) != sizeof(serverEmptyEessage))
		{
			fprintf(stderr, "Read Error\n");
			exit(1);
		}
        
		if (serverEmptyEessage.mtime == -1){
			printf("no more new files\n");
			return 0;
		}
		
		strncpy(path, filepath, MAXNAME);
		strncat(path, "/",2);
		strncat(path, serverEmptyEessage.filename, sizeof(serverEmptyEessage.filename) + 1);

		
		printf("%s is ready to download from server\n", serverEmptyEessage.filename);
		
		receive_file(sockfd, path, serverEmptyEessage.size, serverEmptyEessage.mtime);
		
		printf("%s is downloaded.\n",serverEmptyEessage.filename);
	}
	return 0;

}




int synchronize(int sockfd, char *pwd)
{
	int filenum = 0;
	char path[MAXNAME + 1];
	struct stat filestatus;
	struct sync_message clientMessage, serverMessage;
	struct dirent *file;

	//directory
	DIR *dir;
	dir = opendir(pwd);

	
	//open dir
	if ((dir = opendir(pwd)) == NULL)
	{
		perror("directory error");
		exit(1);
	}

	while (filenum < MAXFILES)
	{
			
		if((file = readdir(dir)) == NULL)
		{
			//no more file
			break;
		}
		
		//get filepath
		strncpy(path, pwd, MAXNAME);
		strncat(path, "/",2);
		strncat(path, file->d_name, strlen(file->d_name) + 1);
            

		if(stat(path, &filestatus) < 0) 
		{
			perror("stat");
			exit(1);
		}

		//check if it is regular file
		if(S_ISREG(filestatus.st_mode))
		{
	    	
			//set file message(name, modified time and size)
			strncpy(clientMessage.filename, file->d_name, MAXNAME);
	    	clientMessage.mtime = (long int) filestatus.st_mtime;
			clientMessage.size = (int) filestatus.st_size;
	
			//send message to server
			Writen(sockfd, &clientMessage, sizeof(clientMessage));
			
			//receive message from server
			if(Readn(sockfd, &serverMessage, sizeof(serverMessage)) != sizeof(serverMessage))
			{
				fprintf(stderr, "Error message reading from server\n");
				exit(1);
			}   

			//when the file in server is newly revised
			//need to receive file from server	
			if (clientMessage.mtime < serverMessage.mtime)
			{
				
				printf("%s is receiving from server\n", file->d_name);
				
				//receive function
				receive_file(sockfd, path, serverMessage.size, serverMessage.mtime);					
				printf("%s has received from server successfully\n", file->d_name);
			}
	
			
			//when the file in client is newly revised
			//need to send to the server
			else if (clientMessage.mtime > serverMessage.mtime)
			{
				printf("%s is send to the server\n", file->d_name);
				
				//send function
				send_file(sockfd, path);
				printf("%s has sent to server successfully\n", file->d_name);

			}
		}
	filenum++;
	}
    
	//check new file out of the loop
    new_file(sockfd, pwd);
	closedir(dir);
	return 0;
    
}


int main(int argc, char* argv[])
{ 
    int sockfd;
    struct hostent *he;
    struct sockaddr_in their;
    struct login_message user;
    //char path[MAXNAME + 1];

    
    if ( argc != 4 ){  
        fprintf(stderr,"usage: client hostname userid dir\n");
        exit(1);
    }

    if ((he = gethostbyname(argv[1])) == NULL) 
    {  /* get the host info  */
        perror("gethostbyname");
        exit(1);
    }

    memset((char*)&their, 0,sizeof(their));
    their.sin_family = AF_INET;
    their.sin_port = htons(PORT);
    their.sin_addr = *((struct in_addr *)he->h_addr);  
 

    sockfd = Socket(AF_INET, SOCK_STREAM, 0);
    if (Connect(sockfd, (struct sockaddr *)&their, sizeof(their)) == -1)
    {  
        exit(1); 
    }


    /*store the user info to client*/
    strncpy(user.userid, argv[3], strlen(argv[3]) + 1);
    strncpy(user.dir, argv[2], strlen(argv[2]) + 1);

    //login
    Writen(sockfd, &user, sizeof(user));
    printf("user: %s is logined in\n", user.userid);
    
    
    //suppose update 
    while(1)
    {
    	synchronize(sockfd, argv[2]);
    	printf("waiting for 10 sec\n");
    	sleep(10);

    }

  	Close(sockfd);

    return 0;

}



#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>    
#include "wrapsock.h"
#include "filedata.h"


ssize_t Readn(int fd, void *ptr, size_t nbytes);
void Writen(int fd, void *ptr, size_t nbytes);



//send the file to client
//same as file in dbclient
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

    while((bits = fread(buf, 1, CHUNKSIZE,fp)) > 0)
    {
        
 		//writen the file
        Writen(sockfd, &buf, bits);
    }


    if((fclose(fp))) 
    {
        perror("fclose");
        exit(1);
    }

    return 0;
}


//received file and change its modfoed time
//similar as receive_file in dbclient
int receive_file(int sockfd, char *path, int size, long int modifiedTime){

	int bufsize;
	int readen = 0;
	char buffer[CHUNKSIZE + 1];
	FILE *fp;

	struct stat filestatus;
    struct utimbuf newtime;


	fp = fopen(path, "w");
	//Open the file. Overwite if it already exists.
	// if((fp = fopen(path, "w+")) == NULL) {
	// 	perror("fopen on get file: ");
	// 	printf("update error: %s\n", path);
	// 	printf("%lu\n", strlen(path));
	// 	exit(1);
 	//    }

	
	//write max chunksize one time;
	if(stat(path, &filestatus) < 0) 
	{
    	perror("stat");
    	exit(1);
    }

    if(S_ISREG(filestatus.st_mode))
    {

		if((bufsize = size) > CHUNKSIZE) bufsize = CHUNKSIZE;

	
		int length;
		while( readen < size )
		{
			bufsize = size - readen;
			if (bufsize > CHUNKSIZE) bufsize = CHUNKSIZE;

			length = Readn(sockfd, &buffer, bufsize);
			readen += length;

			fwrite(buffer, length, 1, fp);
	
		
		if(ferror(fp))
		{
    		fprintf(stderr, "fwrite\n");
			Close(sockfd);
			exit(1);
		}
	}
	
	//for the hidden properity file so fclose must close here
	fclose(fp);
			
	
	//Update the the modified the time make it same as in the server
	//Access time.
	newtime.actime = filestatus.st_atime; //Access time.
	newtime.modtime = (time_t) modifiedTime;
	
	if(utime(path, &newtime) < 0) {
    	perror("utime");
    	exit(1);
    	}
    }
    //set time
    return 0;
}



//send new file to the client
int new_file(int sockfd, int index, char *path)
{

	int filenum = 0;
	DIR *dir;
	struct dirent *file;
	char filepath[MAXNAME + 1];
	struct stat filestatus;
	struct sync_message sync_message;

	//open directory
	if ((dir = opendir(path)) == NULL)
	{
		perror("opendir");
		exit(1);
	}

	int filesend = 0;
	while ( MAXFILES > filenum)
	{
		
		if ((file = readdir(dir)) == NULL) break;

		//copy the file path
		strncpy(filepath, path, MAXNAME);
		strncat(filepath, "/",2);
		strncat(filepath, file->d_name, sizeof(file->d_name) + 1);

       
        //if the file is not exist in client path
        if (file_exist(clients[index].files, file->d_name) == 0)
        {
           
            //add to the file list
            check_file(clients[index].files, file->d_name);
			
			if(stat(filepath, &filestatus) < 0) 
			{
		    	perror("stat");
		    	exit(1);
		    }

		    //regular file
		    if(S_ISREG(filestatus.st_mode))
		    {
		    	strncpy(sync_message.filename,file->d_name, MAXNAME);

				//sent the sync_message of file modified time and size
				sync_message.mtime = (long int) filestatus.st_mtime;
				sync_message.size = (int) filestatus.st_size;
				Writen(sockfd, &sync_message, sizeof(sync_message));

	        	
	        	printf("%s is ready to send to the client\n", file->d_name);
	  			
	        	//send file func
	  			send_file(sockfd, filepath);
	  			printf("send successfully\n");
	  			

	  			filesend++;
	  			break;
		    }
        }  
       	
       	filenum++;

    }

    
    //no file send to client
    if (filesend == 0)
    { 		
    	closedir(dir);
	    sync_message.mtime = -1;
	    Writen(sockfd, &sync_message, sizeof(sync_message));

    }

    return 0;

}


//set time of the files
int modifiedTime(int index, char* path)
{
	
	struct stat filestatus;
	struct dirent *file;
	char filedir[MAXNAME + 1];

	
	//open dir
	DIR *dir;
	if ((dir = opendir(path)) == NULL)
	{
		perror("opendir");
		exit(1);
	}
	
	int filenum = 0;
	while (filenum < MAXFILES)
	{
		//no more file 
		if((file = readdir(dir)) == NULL) break; 

		//copy the path
		strncpy(filedir, path, MAXNAME);
		strncat(filedir, "/",1);
		strncat(filedir, file->d_name, sizeof(file->d_name) + 1);
        
		//get status
		if(stat(filedir, &filestatus) < 0) 
		{
	    	perror("stat");
	    	exit(1);
	    }

	    if(S_ISREG(filestatus.st_mode))
	    { 	
	    	
	    	//checkfile is not working here,so need a new func to judge whether file in file list
	    	if(file_exist(clients[index].files, file->d_name))
	    	{
	    		struct file_info * fp;
	    		fp = check_file(clients[index].files, file->d_name);
                fp->mtime = (long int) filestatus.st_mtime;
  
	    	}	    	
        }

        filenum++;

    }
    closedir(dir);
    return 0;
        
}


//control syncmessage
int message_control(int index, int sockfd, char *path, struct sync_message sync_message)
{
    struct sync_message reply;
    char file_path[MAXNAME];
    struct file_info * fp;

	 
    //file not exist in the client
	 if(sync_message.mtime == -1)
	 {
	 	new_file(sockfd, index, path);
	 } 

	 

	 else
	 {
		//get file ptr
		fp = check_file(clients[index].files,sync_message.filename);
        
		//for setting up the time of file
        modifiedTime(index, path);

		strncpy(reply.filename, fp->filename, MAXNAME);
		reply.mtime = fp->mtime;
		
		//writen the reply message to client
		Writen(sockfd, &reply, sizeof(reply));

		//get file path
		strncpy(file_path, path, MAXNAME);
		strncat(file_path, "/",2);
		strncat(file_path, fp->filename, sizeof(fp->filename));

		if (fp->mtime > sync_message.mtime)
		{

			printf("%s is send to client\n", fp->filename);
  			
			//send function
  			send_file(sockfd, file_path);
  			printf("sent successfully\n");

		}

		else if (fp->mtime < sync_message.mtime)
		{
			printf("%s is received from client\n", sync_message.filename);
	        
			//receive function
	        receive_file(sockfd, file_path, sync_message.size, sync_message.mtime);
	        printf("received successfully\n");
		
		}

	 }

	 return 0;
}


int main(int argc, char const *argv[])
{

	
	char path[MAXNAME + 1];
	int maxfd, listenfd, connfd;
	int nready;
	//Client client[MAXCLIENTS];
	//ssize_t len;
	fd_set rset, allset;
	struct sockaddr_in cliaddr, servaddr;
	struct login_message login_message;
   	struct sync_message sync_message;
	int yes = 1;

	socklen_t clilen;

    memset((char*)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    printf("PORT = %d\n", PORT);

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
    }

    Bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
    Listen(listenfd, LISTENQ);
   
   	
   	maxfd = listenfd;   /* initialize */
	// maxi = -1;      /* index into client[] array */
	// for (i = 0; i < MAXCLIENTS; i++) 
	// {
	// 	client[i].soc = -1; /* -1 indicates available entry */
	// 	client[i].curpos = 0;
	// }

    FD_ZERO(&allset);//init the setsss
    FD_SET(listenfd, &allset);


    
    //initialize clients and dirs 
    init(); 
    

    int clientnum;
    for ( ; ; ) 
    {
    	rset = allset;

    	nready = Select(maxfd+1, &rset, NULL, NULL, NULL);
        
		if (FD_ISSET(listenfd, &rset)) 
		{	
			clilen = sizeof(cliaddr);
			connfd = Accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
			

			//read login message first.
			if((Readn(connfd, &login_message, sizeof(login_message))) != sizeof(login_message)){
				fprintf(stderr, "login error\n");
				exit(1);
			}    


			//to much client
			if ((clientnum = add_client(login_message)) == -1)
			{
				exit(1);
			}

			// if (clientnum > MAXCLIENTS)
			// {
			// 	printf("No more client!\n");
			// 	break;
			// }	

			clients[clientnum].sock = connfd;
			clients[clientnum].STATE = SYNC;

			strncpy(path, login_message.dir, MAXNAME - 8);
			strncat(path, "_server", 8);
               
            
			//need to create the file if it is not exist
			//available to read write and excute
            mkdir(path, S_IRWXU);
			printf("Welcome %s!\n", login_message.userid);

			
			FD_SET(connfd,&allset);
			if (connfd > maxfd) maxfd = connfd;	
        }

	    

	    if (FD_ISSET(connfd, &rset)){

            //set sync state
			if (clients[clientnum].STATE == SYNC)
			{
				
				if (Readn(connfd, &sync_message, sizeof(sync_message)) <= 0)
				{
			        exit(1);
				}

	            //control the sync_message
	            message_control(clientnum, connfd, path, sync_message);
	            sleep(3);
            }         

        }


	}


	Close(connfd);
	return(0); 

}

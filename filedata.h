#include <time.h>
#include "message.h"

struct file_info {
    char filename[MAXNAME];
    time_t mtime;   /* Last modified time as reporte by the client */
};

struct client_info {
    int sock;
    char userid[MAXNAME];
    char dirname[MAXNAME];
    struct file_info files[MAXFILES];
    int STATE;
};


extern struct client_info clients[MAXCLIENTS];


void init();
int add_client(struct login_message s);
struct file_info *check_file(struct file_info *files, char *filename);


void display_clients();

int file_exist(struct file_info *fp, char *name);


/*
 * 
 * ASKDaemon v0.0.3.5
 * null@student.agh.edu.pl
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <mysql/mysql.h>
#include <mysql/errmsg.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*struct passwd {
	char   *pw_name;       // username
    char   *pw_passwd;     // user password
    uid_t   pw_uid;        // user ID
    gid_t   pw_gid;        // group ID
    char   *pw_gecos;      // user information
    char   *pw_dir;        // home directory
    char   *pw_shell;      // shell program
};
struct group {
	char *gr_name; char *gr_passwd; gid_t gr_gid; char **gr_mem;
};*/

struct server_info {
	int id; char *hostname; int port;
} server;
MYSQL mysql;
/* DAEMON */
#define RUNNING_DIR	"/tmp"
#define LOCK_FILE		"ask_daemon.lock"
#define LOG_FILE		"ask_daemon.log"
/* SYSTEM */
#define PASSWD			"/etc/passwd"
#define GROUP			"/etc/group"
/* DATABASE */
#define DBSERVER		"lapix"
#define DBUSER			"ask"
#define DBPASS			"ask"
#define DBNAME			"usman"
/* USER COMMANDS */
#define USER_ADD		1;
#define USER_MOD 		2;
#define USER_DEL		3;
#define USER_PASS		4;
#define USER_QUOTA	5; 
/* GROUP COMMANDS */
#define GROUP_ADD		11;
#define GROUP_MOD		12;
#define GROUP_DEL 	13;
/* CUSTOM */
#define CUSTOM_CMD 	101;
/* INTEGRITY */
const int ITG_NONE 	= 0;
const int ITG_OK 	 	= 1;
const int ITG_DEL		= 2;
/* IGNORED */
const int IGN_USER	= 0;
const int IGN_GROUP 	= 1;
/* SOCKET CONN */
#define EXECUTE		0
#define IMP_USR		1
#define IMP_GRP		2
#define QUIT			9

// FUNCTION DECLARATION
void log_message(char*, char*);
void signal_handler(int);
void daemonize();
void stop_daemon();
void db_connect(char*, char*, char*, char*);
void db_add_user(struct passwd*);
void db_add_group(struct group*);
void db_truncate(char*);
void db_query(char*);
void exec(char*);
int get_count(char*);
void users_system_db();
void user_db_system(int);
void users_db_system(int);
void user_system_del(int);
void groups_system_db();
void group_system_mod(int);
void group_system_del(int);
void group_db_system(int);
void groups_db_system(int);
void execute_commands();
void socket_server(int);
void get_server_data();
// END OF FUNCTION DELCLARATION

// ERROR: marking users/groups as integrated even if shell command fails !?

void log_message(char *filename, char *message) {
	
	FILE *logfile;
	time_t act_time;
	struct tm *ts;
	char buf[20];

	logfile=fopen(filename,"a");
	if(!logfile) return;

	act_time = time(0);
	ts = localtime(&act_time);
	strftime(buf, sizeof(buf), "%d-%m-%Y %H:%M:%S", ts);

	fprintf(logfile,"%s: %s\n", buf, message);
	printf("%s: %s\n", buf, message);
	fclose(logfile);
}

void stop_daemon() {
	log_message(LOG_FILE,"ASKDaemon STOP");
	exit(EXIT_SUCCESS);
}

void signal_handler(int sig) {
	
	switch(sig) {
	case SIGHUP:
		log_message(LOG_FILE,"Przechwycono SIGHUP");
		break;
	case SIGTERM:
		log_message(LOG_FILE,"Przechwycono SIGTERM");
		stop_daemon();
		break;
	}
}

void daemonize() {
	
	int lfp;
	char str[10];
	char *msg;
	msg=(char*)malloc(50*sizeof(char));
	
	if(getppid() == 1) return; // juz daemon
	
	if(daemon(1,0) < 0) {
		log_message(LOG_FILE, "Error while trying to run the daemon in background!");
		exit(-1);
	 }

	umask(027); // prawa dostepu do plikow / 750

	if(chdir(RUNNING_DIR) < 0) { // zmiana sciezki roboczej
		sprintf(msg, "Error while changing working directory to: %s!", RUNNING_DIR);
		log_message(LOG_FILE, msg);
		exit(-1);
	}

	lfp = open(LOCK_FILE,O_RDWR|O_CREAT,0640);
	if (lfp < 0) { // blad uchwytu
		sprintf(msg, "Error while creating lock file: %s!", LOCK_FILE);
		log_message(LOG_FILE, msg);
		exit(-1); 
	} 
	
	if (lockf(lfp,F_TLOCK,0) < 0) { // blad blokady
		sprintf(msg, "Error while locking the file: %s", LOCK_FILE);
		log_message(LOG_FILE, msg);
		exit(-1);
	}

	sprintf(str,"%d\n",getpid());
	if(write(lfp,str,strlen(str)) < 0) { // zapis pid
		sprintf(msg, "Error while writing pid to lock file: %s", LOCK_FILE);
		log_message(LOG_FILE, msg);
		exit(-1);
	}
	
	// Obsluga sygnalow / ignorowanie
	signal(SIGCHLD,SIG_IGN); // zatrzymanie potomka
	signal(SIGTSTP,SIG_IGN); // zatrzymanie z tty
	signal(SIGTTOU,SIG_IGN); // wyjscie tty
	signal(SIGTTIN,SIG_IGN); // wejscie tty
	signal(SIGHUP,signal_handler); // przechwyt hup
	signal(SIGTERM,signal_handler); // przechwyt term
	
	free(msg);

	log_message(LOG_FILE,"ASKDaemon Start");
}

void db_connect(char *host, char *login, char *pass, char *dbase) {
	char *msg;
	msg=(char*)malloc(100*sizeof(char));
	if(mysql_init(&mysql) == NULL)
	{
		log_message(LOG_FILE, "MySql Initialization Error!");
      stop_daemon();
    } else {
		if(mysql_real_connect(&mysql, host, login, pass, dbase, 0, NULL, 0) == NULL) {
			sprintf(msg, "MySql Server Error: %s", mysql_error(&mysql));
			log_message(LOG_FILE, msg);
			stop_daemon();
		} else {
			sprintf(msg, "MySql Server Version: %s", mysql_get_server_info(&mysql));
			log_message(LOG_FILE, msg);
		}
	}
	free(msg);
}

void db_add_user(struct passwd *u) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *query;
	int group_id;
	query = (char*)malloc(200*sizeof(char));
	sprintf(query, "select id from %s.group where gid=%d", DBNAME, u->pw_gid);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	group_id = atoi(row[0]);
	sprintf(query, "insert into %s.user(login, password, home, shell, uid, server_id, group_id, integrity_status) values('%s', '%s', '%s', '%s', %d, %d, %d, %d)", DBNAME, u->pw_name, u->pw_passwd, u->pw_dir, u->pw_shell, u->pw_uid, server.id, group_id, ITG_OK);
	printf("%s\n", query);
	mysql_query(&mysql, query);
	free(query);
}

void db_add_group(struct group *g) {
	char *query;
	query = (char*)malloc(200*sizeof(char));
	sprintf(query, "insert into %s.group(name, gid, server_id, integrity_status) values('%s', %d, %d, %d)", DBNAME, g->gr_name, g->gr_gid, server.id, ITG_OK);
	printf("%s\n", query);
	mysql_query(&mysql, query);
	free(query);
}

void db_truncate(char *table) {
	char *query;
	query = (char *) malloc(100*sizeof(char));
	sprintf(query, "truncate table %s", table);
	mysql_query(&mysql, query);
}

void db_query(char *query) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	int i, num_fields;
	
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	num_fields = mysql_num_fields(result);
	while ((row = mysql_fetch_row(result)))
	{
		for(i = 0; i < num_fields; i++)
		{
			printf("%s ", row[i] ? row[i] : "NULL");
		}
		printf("\n");
	}
    mysql_free_result(result);
}

void db_disconnect() {
    mysql_close(&mysql);
}

void exec(char *command) {
	FILE *cmd;
	cmd = popen(command, "w");
	if(cmd == NULL)
		log_message(LOG_FILE, "Error: File is NULL! Probably trying to execute wrong command!");
	pclose(cmd);
}

int get_count(char *path) {
	FILE *file;
	char sign;
	char *msg;
	int c = 0;
		
	msg=(char*)malloc(50*sizeof(char));
	file = fopen(path, "r");
   if (file == NULL) {
		sprintf(msg, "Error: No such file: %s", path);
		log_message(LOG_FILE, msg);
		exit(EXIT_FAILURE);
	}
   while((sign=getc(file))!=EOF) 
   {
		if(sign == '\n') c++;
	}
	fclose(file);
	free(msg);
	return c;
}

void users_system_db() {
   struct passwd *u = NULL;
	while((u = getpwent()) != NULL)
	{
		db_add_user(u);
	}
}

void user_system_mod(int user_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(300*sizeof(char));
	sprintf(query, "select login, password, uid, group.gid, home, shell, user.name, surname, position from %s.user, %s.group where user.id=%d and user.server_id=%d and and group.server_id=%d and user.integrity_status=%d and group.id=user.group_id", DBNAME, DBNAME, user_id, server.id, server.id, ITG_NONE);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	if((row=mysql_fetch_row(result))) {
		//printf("%s\n", query);
		sprintf(cmd, "usermod -p %s -u %s -g %s -d %s -s %s -c '%s %s %s' -m %s", row[1], row[2], row[3], row[4], row[5], row[6], row[7], row[8], row[0]);
		exec(cmd);
		sprintf(query, "update %s.user set integrity_status=%d where id=%d", DBNAME, ITG_OK, user_id);
		mysql_query(&mysql, query);
	}
	mysql_free_result(result);
	free(cmd);free(query);
}

void user_system_del(int user_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *query, *cmd;
	cmd=(char*)malloc(100*sizeof(char));
	query=(char*)malloc(100*sizeof(char));
	sprintf(query, "select login from %s.user where id=%d and server_id=%d and integrity_status=%d", DBNAME, user_id, server.id, ITG_DEL);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	sprintf(cmd, "userdel -f -r %s", row[0]);
	exec(cmd);
	sprintf(query, "delete from %s.user where id=%d", DBNAME, user_id);
	mysql_query(&mysql, query);
	free(cmd);free(query);
}

void user_db_system(int user_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(300*sizeof(char));
	struct passwd *u = NULL;
	u=(struct passwd *)malloc(sizeof(struct passwd));
	sprintf(query, "select login, password, uid, group.gid, home, shell, user.name, surname, position from %s.user, %s.group where server_id=%d and user.id=%d and user.integrity_status=%d and group.id=user.group_id", DBNAME, DBNAME, server.id, user_id, ITG_NONE);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	sprintf(cmd, "useradd -p %s -u %s -g %s -d %s -s %s -c '%s %s %s' -m %s", row[1], row[2], row[3], row[4], row[5], row[6] ? row[6] : "", row[7] ? row[7] : "", row[8] ? row[8] : "", row[0]);
	exec(cmd);
	sprintf(query, "update %s.user set integrity_status=%d where id=%d", DBNAME, ITG_OK, user_id);
	mysql_query(&mysql, query);
	mysql_free_result(result);
	free(query); free(cmd);
}

void users_db_system(int user_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(300*sizeof(char));
	struct passwd *u = NULL;
	u=(struct passwd *)malloc(sizeof(struct passwd));
	sprintf(query, "select distinct login, password, uid, group.gid, home, shell, user.name, surname from %s.user, %s.group where server_id=%d and user.integrity_status=%d and group.id=user.group_id and user.name not in (select name from %s.ignored_name where server_id=%d and type=%d)", DBNAME, DBNAME, server.id, ITG_NONE, DBNAME, server.id, IGN_USER);
	printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	while((row = mysql_fetch_row(result))) {
		sprintf(cmd, "useradd -d %s -g %s -m -p %s -s %s -u %s -c '%s %s' %s", row[4], row[3], row[1], row[5], row[2], row[6], row[7], row[0]);
		printf("%s\n", cmd);
		exec(cmd);
		sprintf(query, "update %s.user set integrity_status=%d where uid=%d", DBNAME, ITG_OK, atoi(row[2]));
		mysql_query(&mysql, query);
	}
	mysql_free_result(result);
}

void groups_system_db() {
	struct group *g = NULL;
	while((g = getgrent()) != NULL)
	{
		db_add_group(g);
	}
}

void group_system_mod(int group_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(200*sizeof(char));
	struct group *g = NULL;
	g=(struct group *)malloc(sizeof(struct group));
	sprintf(query, "select gid, name from %s.group where id=%d and server_id=%d and integrity_status=%d", DBNAME, group_id, server.id, ITG_NONE);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	if((row = mysql_fetch_row(result))) {
		sprintf(cmd, "groupmod -g %d %s", atoi(row[0]), row[1]);
		exec(cmd);
		sprintf(query, "update %s.group set integrity_status=%d where id=%d", DBNAME, ITG_OK, group_id);
		mysql_query(&mysql, query);
	}
	mysql_free_result(result);
	free(cmd);free(query);
}

void group_system_del(int group_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *query, *cmd;
	cmd=(char*)malloc(100*sizeof(char));
	query=(char*)malloc(100*sizeof(char));
	sprintf(query, "select name from %s.group where id=%d and server_id=%d and integrity_status=%d", DBNAME, group_id, server.id, ITG_DEL);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	sprintf(cmd, "groupdel %s", row[0]);
	exec(cmd);
	sprintf(query, "delete from %s.group where id=%d", DBNAME, group_id);
	mysql_query(&mysql, query);
}

void group_db_system(int group_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(200*sizeof(char));
	struct group *g = NULL;
	g=(struct group *)malloc(sizeof(struct group));
	sprintf(query, "select gid, name from %s.group where id=%d and server_id=%d and integrity_status=%d", DBNAME, group_id, server.id, ITG_NONE);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	sprintf(cmd, "groupadd -g %d %s", atoi(row[0]), row[1]);
	exec(cmd);
	sprintf(query, "update %s.group set integrity_status=%d where id=%d", DBNAME, ITG_OK, group_id);
	mysql_query(&mysql, query);
	mysql_free_result(result);
}

void groups_db_system(int group_id) {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *cmd, *query;
	cmd=(char*)malloc(200*sizeof(char));
	query=(char*)malloc(200*sizeof(char));
	struct group *g = NULL;
	g=(struct group *)malloc(sizeof(struct group));
	sprintf(query, "select distinct gid, name from %s.group where server_id=%d and integrity_status=%d and name not in (select name from %s.ignored_name where server_id=%d and type=%d)", DBNAME, server.id, ITG_NONE, DBNAME, server.id, IGN_GROUP);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	while((row = mysql_fetch_row(result))) {
		sprintf(cmd, "groupadd -g %d %s", atoi(row[0]), row[1]);
		exec(cmd);
		sprintf(query, "update %s.group set integrity_status=%d", DBNAME, ITG_OK);
		mysql_query(&mysql, query);
	}
	mysql_free_result(result);
}

void execute_commands() {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *query, *msg;
	query = (char*)malloc(200*sizeof(char));
	msg = (char*)malloc(100*sizeof(char));
	sprintf(query, "select command_type, user_id, group_id, extra_arg, id from %s.command where command.server_id='%d'", DBNAME, server.id);
	//printf("%s\n", query);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	while ((row = mysql_fetch_row(result)))
	{
		switch(atoi(row[0])) {
			case 1: {
				sprintf(msg, "USER_ADD");
				if(row[1] != NULL)
					user_db_system(atoi(row[1]));
				break;
			}
			case 2: {
				sprintf(msg, "USER_MOD");
				if(row[1] != NULL)
					user_system_mod(atoi(row[1]));
				break;
			}
			case 3: {
				sprintf(msg, "USER_DEL");
				if(row[1] != NULL)
					user_system_del(atoi(row[1]));
				break;
			}
			case 4: {
				sprintf(msg, "USER_PASS");
				break;
			}
			case 5: {
				sprintf(msg, "USER_QUOTA");
				break;
			}
			case 11: {
				sprintf(msg, "GROUP_ADD");
				if(row[2] != NULL)
					group_db_system(atoi(row[2]));
				break;
			}
			case 12: {
				sprintf(msg, "GROUP_MOD");
				if(row[2] != NULL)
					group_system_mod(atoi(row[2]));
				break;
			}
			case 13: {
				sprintf(msg, "GROUP_DEL");
				if(row[2] != NULL)
					group_system_del(atoi(row[2]));
				break;
			}
			case 101: {
				sprintf(msg, "CUSTOM_CMD");
				exec(row[3]);
				break;
			}
			default: {
				sprintf(msg, "NOT SUPPORTED!");
				break;
			}
		}
		log_message(LOG_FILE, msg);
		sprintf(query, "delete from %s.command where id=%s", DBNAME, row[4]);
		//printf("%s\n", query);
		mysql_query(&mysql, query);
	}
	mysql_free_result(result);
	free(query); free(msg);
}

void socket_server(int port) {
	char msg[100], buffer[255];
	int sock, clsock, n, soc_cmd = -1;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0) {
		sprintf(msg, "ERROR: Unable to open socket!");
		log_message(LOG_FILE, msg);
		return;
	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(port);
	if (bind (sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		sprintf(msg, "ERROR: Unable to bind socket!");
		log_message(LOG_FILE, msg);
		return;
	}
	
	while(soc_cmd!=QUIT) {
		listen(sock, 10);
		clilen = sizeof(cli_addr);
		clsock = accept(sock, (struct sockaddr *) &cli_addr, &clilen);
		if(clsock < 0) {
			sprintf(msg, "ERROR: Something went wrong while accepting connection!");
			log_message(LOG_FILE, msg);
			return;
		}
		bzero(buffer,255);
		n = read(clsock,buffer,255);
      if (n < 0) {
			sprintf(msg, "ERROR: Some problems while reading from socket!");
			log_message(LOG_FILE, msg);
			return;
		}
		soc_cmd = atoi(buffer);
      //printf("Message: %d\n", atoi(buffer));
      switch(soc_cmd) {
			case 0: {
				execute_commands();
				break;
			}
			case 1: {
				users_system_db();
				break;
			}
			case 2: {
				groups_system_db();
				break;
			}
		}
		close(clsock);
	}
	close(sock);
}

void get_server_data() {
	MYSQL_RES *result;
	MYSQL_ROW row;
	char *query;
	server.hostname = (char*)malloc(45*sizeof(char));
	gethostname(server.hostname, 45*sizeof(char));
	query = (char *) malloc(200*sizeof(char));
	sprintf(query, "select id, port_number from server where host_name='%s'", server.hostname);
	mysql_query(&mysql, query);
	result = mysql_store_result(&mysql);
	row = mysql_fetch_row(result);
	server.id = atoi(row[0]);
	server.port = atoi(row[1]);
	mysql_free_result(result);
}

int main(int argc, char **argv) {
	
	char *dbserver, *dbuser, *dbpass, *dbname;
	
	if(argc < 4 && argc > 1) {
		printf("Usage %s <dbserver> <dbuser> <dbpass> <dbname>\n", argv[0]);
		return(EXIT_FAILURE);
	} else if(argc > 4){
		dbserver = (char*)malloc(20*sizeof(char));
		dbuser = (char*)malloc(20*sizeof(char));
		dbpass = (char*)malloc(20*sizeof(char));
		dbname = (char*)malloc(20*sizeof(char));
		dbserver = argv[1];
		dbuser = argv[2];
		dbpass = argv[3];
		dbname = argv[4];
	} else {
		dbserver = DBSERVER;
		dbuser = DBUSER;
		dbpass = DBPASS;
		dbname = DBNAME;
	}
	
	daemonize();	
	
	db_connect(dbserver, dbuser, dbpass, dbname);
	get_server_data();
	socket_server(server.port);
	db_disconnect();
	stop_daemon();
	
	return 0;
}


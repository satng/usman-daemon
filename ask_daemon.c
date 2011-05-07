/*
 * 
 * ASKDaemon v0.0.1.2
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

#define RUNNING_DIR	"/tmp"
#define LOCK_FILE	"ask_daemon.lock"
#define LOG_FILE	"ask_daemon.log"
#define PASSWD		"/etc/passwd"
#define GROUP		"/etc/group"
MYSQL mysql;

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
	
	if(getppid() == 1) return; // juz daemon

	if(daemon(1,0) < 0) exit(-1);

	umask(027); // prawa dostepu do plikow / 750

	chdir(RUNNING_DIR); // zmiana sciezki roboczej

	lfp = open(LOCK_FILE,O_RDWR|O_CREAT,0640);
	if (lfp < 0) exit(0); // blad uchwytu
	if (lockf(lfp,F_TLOCK,0) < 0) exit(0); // blad blokady

	sprintf(str,"%d\n",getpid());
	write(lfp,str,strlen(str)); // zapis pid
	
	// Obsluga sygnalow / ignorowanie
	signal(SIGCHLD,SIG_IGN); // zatrzymanie potomka
	signal(SIGTSTP,SIG_IGN); // zatrzymanie z tty
	signal(SIGTTOU,SIG_IGN); // wyjscie tty
	signal(SIGTTIN,SIG_IGN); // wejscie tty
	signal(SIGHUP,signal_handler); // przechwyt hup
	signal(SIGTERM,signal_handler); // przechwyt term

	log_message(LOG_FILE,"ASKDaemon Start");
}

void db_connect(char *host, char *login, char *pass, char *dbase) {
	char msg[100];
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
}

void db_add_user(struct passwd *u) {
	/*MYSQL_RES *result;
	int num_fields = 0;*/
	char *query;
	char *msg;
	msg = (char *)malloc(100*sizeof(char));
	query = (char *) malloc(sizeof(char));
	sprintf(query, "insert into users(login, password, home, shell, uid, gid, servers_server_id) values('%s', '%s', '%s', '%s', %d, %d, 1)", u->pw_name, u->pw_passwd, u->pw_dir, u->pw_shell, u->pw_uid, u->pw_gid);
	mysql_query(&mysql, query);
	/*if(NULL!=(result = mysql_store_result(&mysql))) {
		num_fields = mysql_num_fields(result);
		//printf("%d / %s\n", num_fields, mysql_error(&mysql));
		sprintf(msg, "Rows affected: %d.", num_fields);
		log_message(LOG_FILE, msg);
	} else {
		//printf("%d / %s\n", mysql_field_count(&mysql), mysql_error(&mysql));
		sprintf(msg, "Rows affected: %d.", mysql_field_count(&mysql));
		log_message(LOG_FILE, msg);
	}*/
}

void db_add_group(struct group *g) {
	char *query;
	query = (char *) malloc(sizeof(char));
	sprintf(query, "insert into groups(name, gid) values('%s', %d)", g->gr_name, g->gr_gid);
	mysql_query(&mysql, query);
}

void db_truncate(char *table) {
	/*MYSQL_RES *result;
	int num_fields = 0;*/
	char *query;
	query = (char *) malloc(100*sizeof(char));
	sprintf(query, "truncate table %s", table);
	mysql_query(&mysql, query);
	/*if(NULL!=(result = mysql_store_result(&mysql))) {
		num_fields = mysql_num_fields(result);
		//printf("%d / %s\n", num_fields, mysql_error(&mysql));
		//sprintf(msg, "Rows affected: %d.", num_fields);
		//log_message(LOG_FILE, msg);
	} else {
		//printf("%d / %s\n", mysql_field_count(&mysql), mysql_error(&mysql));
		//sprintf(msg, "Rows affected: %d\nERROR: %s.", mysql_field_count(&mysql), mysql_error(&mysql));
		//log_message(LOG_FILE, msg);
	}*/
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

int get_count(char *path) {
	FILE *file;
	char sign;
	int c = 0;
		
	file = fopen(path, "r");
    if (file == NULL) exit(EXIT_FAILURE);
    while((sign=getc(file))!=EOF) 
    {
		if(sign == '\n') c++;
	}
	fclose(file);
	return c;
}

void users_system_db(struct passwd **us) {		
	//int i = 0;
	struct passwd *u = NULL;
	
	db_truncate("users");
	while((u = getpwent()) != NULL)
	{
		if(u->pw_uid >= 1000)
			db_add_user(u);
		/*us[i]->pw_uid = u->pw_uid;
		us[i]->pw_gid = u->pw_gid;
		us[i]->pw_name = strdup(u->pw_name);
		us[i]->pw_passwd = strdup(u->pw_passwd);
		us[i]->pw_gecos = strdup(u->pw_gecos);
		us[i]->pw_shell = strdup(u->pw_shell);
		us[i]->pw_dir = strdup(u->pw_dir);
		i++;*/
	}
}

void users_db_system() {
	MYSQL_RES *result;
	MYSQL_ROW row;
	FILE *file= NULL;
	char msg[100];
	if(NULL == (file = fopen(PASSWD, "a"))) {
		sprintf(msg, "File (%s) Error: Insufficient priviliges. Did you run the program as root?", PASSWD);
		log_message(LOG_FILE, msg);
		stop_daemon();
	}
	struct passwd *u = NULL;
	u=(struct passwd *)malloc(sizeof(struct passwd));
	mysql_query(&mysql, "select login, password, uid, gid, home, shell from users");
	result = mysql_store_result(&mysql);
	while((row = mysql_fetch_row(result))) {
		u->pw_name = row[0];
		u->pw_passwd = row[1];
		u->pw_uid = atoi(row[2]);
		u->pw_gid = atoi(row[3]);
		u->pw_dir = row[4];
		u->pw_shell = row[5];
		putpwent(u, file);
	}
	fclose(file);
}

void groups_system_db(struct group **gr) {		
	//int i = 0;
	struct group *g = NULL;

	db_truncate("groups");
	while((g = getgrent()) != NULL)
	{
		if(g->gr_gid >= 1000)
			db_add_group(g);
		/*gr[i]->gr_gid = g->gr_gid;
		gr[i]->gr_name = strdup(g->gr_name);
		i++;*/
	}
}

void groups_db_system() {
	MYSQL_RES *result;
	MYSQL_ROW row;
	FILE *file= NULL;
	char msg[100];
	if(NULL == (file = fopen(GROUP, "a"))) {
		sprintf(msg, "File (%s) Error: Insufficient priviliges. Did you run the program as root?", GROUP);
		log_message(LOG_FILE, msg);
		printf("%s\n", msg);
		stop_daemon();
	}
	struct group *g = NULL;
	g=(struct group *)malloc(sizeof(struct group));
	mysql_query(&mysql, "select name, gid from groups");
	result = mysql_store_result(&mysql);
	while((row = mysql_fetch_row(result))) {
		g->gr_name = row[0];
		g->gr_passwd = strdup("x");
		g->gr_gid = atoi(row[1]);
		putgrent(g, file);
	}
	fclose(file);
}

int main(int argc, char **argv) {
	//daemonize();
	
	int user_count = 0, group_count = 0, i = 0;
	
	user_count = get_count(PASSWD);
	struct passwd *users[user_count];
	for(i = 0; i < user_count; i++)
		users[i]=(struct passwd *)malloc(sizeof(struct passwd));
		
	group_count = get_count(GROUP);
	struct group *groups[group_count];
	for(i = 0; i < group_count; i++)
		groups[i]=(struct group *)malloc(sizeof(struct group));
	
	db_connect("lapix", "ask", "ask", "account_manager_db");
	
	users_system_db(users);
	//groups_system_db(groups);
	//users_db_system();
	//groups_db_system();
	
	/*i = 0;
	while(i < user_count) {
		if(users[i]->pw_uid >= 1000)
			db_add_user(users[i]);
		i++;
	}
	for(i = 0; i < sizeof(groups); i++) {
		db_add_group(groups[i]);
	}*/
	
	db_disconnect();
	
	stop_daemon();
	return 0;
}


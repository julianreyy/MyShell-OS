#include <stdlib.h>
#include <stdio.h>
#include "parser.h" //Lo del enunciado
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SIZE 512
#define NUMPIPES 10

void handler(int sig) {
	waitpid(WAIT_ANY, NULL, WNOHANG);
}

void closePipes(int p[NUMPIPES][2], int n){
    int i;
    for (i=0; i<n; i++){
        close(p[i][0]);
        close(p[i][1]);
    }
}

void exeCommand(tline *line){
    int i;
    int fdin;
    int fdout;
    pid_t pid; //Para que se ejecute el mandato interno de myshell, siendo este el padre
    int p[NUMPIPES][2]; //Numero de pipes máximos, junto a entrada 1 salida 0
	pid_t pids[10];
    for (i = 0; i < line->ncommands-1; i++){
        pipe(p[i]);
    }
    
    for (i = 0; i < line->ncommands; i++){ //Aquí va la ejecución de un solo mandato
        pid = fork();
        if (pid < 0){
            fprintf(stderr, "Error de un fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0){
			if (!line->background) {
				signal(SIGINT, SIG_DFL);
			}
			if (line->ncommands > 1) {
            	if (i==0){
                	dup2(p[0][1], 1);
            	}
            	else if (i == line->ncommands-1){
                	dup2(p[i-1][0],0);
            	}
            	else{
                	dup2(p[i-1][0],0);
                	dup2(p[i][1],1);
            	}
            	closePipes(p, line->ncommands-1);
			}
			if ((i == 0) && (line->redirect_input != NULL)) {
				fdin = open(line->redirect_input, O_RDONLY);
				if (fdin < 0)
				{
					fprintf(stderr, "%s: No puedo abrir el fichero\n", line->redirect_input);
					exit(1);
				}
				dup2(fdin, 0);
				close(fdin);
			}
			if ((i == (line->ncommands - 1)) && (line->redirect_output != NULL)) {
				fdout = creat(line->redirect_output, 0664);
				if (fdout < 0)
				{
					fprintf(stderr, "%s: No puedo abrir el fichero\n", line->redirect_output);
					exit(1);
				}
				dup2(fdout, 1);
				close(fdout);
			}
			if ((i == (line->ncommands - 1)) && (line->redirect_error != NULL)) {
				fdout = creat(line->redirect_error, 0664);
				if (fdout < 0)
				{
					fprintf(stderr, "%s: No puedo abrir el fichero\n",line->redirect_error);
					exit(1);
				}
				dup2(fdout, 2);
				close(fdout);
			}
				if (line->commands[i].filename == NULL){
                fprintf(stderr, "Comando no existe\n");
            }
            execvp(line->commands[i].filename, line->commands[i].argv);
            exit(EXIT_FAILURE);
        }
		else {
			pids[i] = pid;
		}

    }
    closePipes(p, line->ncommands - 1);
	if (!line->background) {
    	for (i = 0; i < line->ncommands; i++){
        	waitpid(pids[i], NULL, 0);
    	}
	}
	else {
		for (i = 0; i < line->ncommands; i++){
        	printf("%d ", pids[i]);
		}
		printf("\n");
	}
}

void exeCD(tline *line){

	char *home;
	int e;

	if (line->commands[0].argc == 1) {
		home=getenv("HOME");

		if (home==NULL){
			fprintf(stderr,"error HOME\n");
        }
        else
		{
			e = chdir(home);
			if (e < 0)
				fprintf(stderr,"error HOME\n");
		}	
	}
	else if (line->commands[0].argc == 2) {
		e = chdir(line->commands[0].argv[1]);
		if (e < 0) {
			fprintf(stderr,"%s: error cd\n",line->commands[0].argv[1]);
        }
	}
	else
	{
		fprintf(stderr,"Usage: cd dir\n");
	}
	
}

void exeExit(tline *line){
    if (line->commands[0].argv[1] == NULL){
        exit(0);
    }
    else {
        exit(atoi(line->commands[0].argv[1]));
    }
}

void exeUmask(tline *line){
    mode_t new_mask;
    mode_t old_mask;
    char *end;

    if (line->commands[0].argv[1] == NULL) {
        old_mask = umask(0);
        umask(old_mask);
        printf("%04o\n", old_mask);
    } else {
        unsigned long val = strtoul(line->commands[0].argv[1], &end, 8);
        if (*end != '\0') {
            fprintf(stderr, "umask: valor octal inválido\n");
            return;
        }
        new_mask = (mode_t)val;
        umask(new_mask);
    }
}

int main(int argc, char *argv[])
{
    tline *line;
    char linea[SIZE];
	signal(SIGINT, SIG_IGN);
	signal(SIGCHLD, handler);
    printf("myshell> ");
    
    while (1) { 
        if (fgets(linea, SIZE, stdin) != NULL) { //comprueba que se escribe algo
			if (strlen(linea) > 1) {
            	line = tokenize(linea); //llama a la función que lee la linea
            	if (line != NULL) { //comprueba que no ha dado error
                	if (strcmp(line->commands[0].argv[0], "cd") == 0){
                    	exeCD(line);
                	}
                	else if (strcmp(line->commands[0].argv[0], "exit") == 0){
                    	exeExit(line);
                	}
					else if (strcmp(line->commands[0].argv[0], "umask") == 0){
                    	exeUmask(line);
                	}
                	else exeCommand(line);
            	}
			}
            printf("myshell> ");
        }
    }


    return 0;
}

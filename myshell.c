#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include "parser.h" //Lo del enunciado

#define SIZE 512
#define NUMPIPES 10

void closePipes(int p[NUMPIPES][2], int n){
    int i;
    for (i=0; i<n; i++){
        close(p[i][0]);
        close(p[i][1]);
    }
}

void exeCommand(tline *line){
    int i;
    pid_t pid; //Para que se ejecute el mandato interno de myshell, siendo este el padre
    int p[NUMPIPES][2]; //Numero de pipes máximos, junto a entrada 1 salida 0
    for (i = 0; i< line->ncommands-1; i++){
        pipe(p[i]);
    }
    
    for (i=0; i < line->ncommands; i++){ //Aquí va la ejecución de un solo mandato
        pid= fork();
        if (pid < 0){
            fprintf(stderr, "Error de un fork");
            exit(EXIT_FAILURE);
        }
        if (pid==0){
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
            if (line->commands[i].filename == NULL){
                fprintf(stderr, "Comando no existe\n");
            }
            execvp(line->commands[i].filename, line->commands[i].argv);
            exit(EXIT_FAILURE);
        }

    }
    closePipes(p, line->ncommands-1);

    for (i=0; i < line->ncommands; i++){
        wait(NULL);
    }
}

void exeCD(tline *line){

	char *home;
	int e;

	if (line->commands[0].argc==1) {
		home=getenv("HOME");

		if (home==NULL){
			fprintf(stderr,"error HOME\n");
        }
        else
		{
			e=chdir(home);
			if (e<0)
				fprintf(stderr,"error HOME\n");
		}	
	}
	else if (line->commands[0].argc==2) {
		e=chdir(line->commands[0].argv[1]);
		if (e<0) {
			fprintf(stderr,"%s: error cd\n",line->commands[0].argv[1]);
        }
	}
	else
	{
		fprintf(stderr,"Usage: cd dir\n");
	}
	
}

void exeExit(tline *line){
    if (line->commands[0].argc == 1){
        exit(0);
    }
    else {
        exit(atoi(line->commands[0].argv[1]));
    }
}



int main(int argc, char *argv[])
{
    tline *line;
    char linea[SIZE];
    printf("myshell> ");
    
    while (1) {
        if (fgets(linea, SIZE, stdin) != NULL) {
            line= tokenize(linea);
            if (line != NULL) {
                if (strcmp(line->commands[0].argv[0], "cd")== 0){
                    exeCD(line);
                }
                else if (strcmp(line->commands[0].argv[0], "exit")==0){
                    exeExit(line);
                }
                exeCommand(line);
            }
            printf("myshell> ");
        }
    }


    return 0;
}
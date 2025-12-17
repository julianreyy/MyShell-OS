#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define SIZE 512

typedef struct job { // Define un nodo trabajo
    pid_t pid;
    char buffer[1024]; // buffer de caracteres
    char estado; // estado del trabajo  R: Running S: Stopped
    struct job *next; // Puntero al siguiente nodo de la lista
} job;

typedef struct jobList { // Define la estructura de una lista de trabajos
    job *head; 
} jobList;

jobList *jobs_list; 

void insert_job(pid_t pid, char *buffer, char estado) {
    job *n = malloc(sizeof(job)); // reserva memoria para un nuevo trabajo
    n->pid = pid;
    strcpy(n->buffer, buffer);
    n->estado = estado;
    n->next = jobs_list->head; // el nodo apunta al primero de la lista
    jobs_list->head = n; // el nodo se convierte en el primero de la lista
    return;
}

void elim_job(pid_t pid) {
    job *current = jobs_list->head; //puntero auxiliar que empieza al principio de la lista
    job *aux = NULL;
    while (current != NULL) {
        if (current->pid == pid) { //si se encuentra el PID buscado
            if (aux == NULL) { // comprueba si es el primer nodo de la lista
                jobs_list->head = current->next;
            } else { //si no es el primero
                aux->next = current->next; 
            }
            free(current); // libera memoria
            return; 
        }
        aux = current; // el auxiliar pasa a ser el actual
        current = current->next;
    }
    return;
}

job *get_job_by_index(int n) {
    job *aux = jobs_list->head; // puntero auxiliar que empieza al principio de la lista
    int i = 1;
    if (aux == NULL) { 
        return NULL;
    }
    if (n == 0){ // si n es 0 se devuelve el último trabajo añadido que es el primero de la lista
        return aux; 
    }
    while (aux != NULL && i < n) {
        aux = aux->next; 
        i++;
    }
    return aux;
}

void handler(int sig) {
    pid_t pid; 
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { //para recoger procesos hijos que hayan terminado
        if (WIFEXITED(status) || WIFSIGNALED(status)) { // Comprueba si el proceso ha terminado de forma normal o ha terminado por una señal
            elim_job(pid); // Borra el trabajo de la lista de jobs

        }
    }
}


void execute_jobs() {
    int i = 1;
    job *aux = jobs_list->head;
    while (aux != NULL) {
        const char *estado_str;
        if (aux->estado == 'R') { //Se comprueba de la lista todo el rato hasta el final si es R o S
            estado_str = "Running";
        } else {
            estado_str = "Stopped";
        }
        printf("[%d] %s %s\n", i, estado_str,aux->buffer); //Imprimir numero de proceso (no PID) y estado
        aux = aux->next; 
        i++;
    }
    return;
}


void execute_bg(tline *line) {
    job *j = NULL;
    if (jobs_list->head == NULL) { // comprueba si hay lista de mandatos
        fprintf(stderr, "bg: no hay trabajos actuales\n");
        return;
    }
    if (line->commands[0].argv[1] == NULL) { // comprueba si no se pasó un argumento
        j = jobs_list->head; // se selecciona el último trabajo añadido
    } else {
        int idx = atoi(line->commands[0].argv[1]); // convierte el argumento a entero (idx para diferenciarlo de un iterador)
        j = get_job_by_index(idx);
    }
    if (j != NULL) { // comprueba si se encontró el trabajo
        if (j->estado == 'S') { // si está parado
            kill(j->pid, SIGCONT); // envía señal para continuarlo
            j->estado = 'R'; // cambia el estado
            printf("[%d]+ %s &\n", j->pid, j->buffer); // escribe el pid junto el mandato
        } else { // si no estaba detenido
            fprintf(stderr, "bg: el mandato ya está ejecutándose\n");
        }
    } else { // si no hay nada así
        fprintf(stderr, "bg: no existe el mandato\n");
    }
    return;
}

void execute_cd(tline *line){
    char *dir;
    char cwd[1024]; // Buffer para la ruta
    int entorno;
    if (line->commands[0].argc == 1) {
        dir = getenv("HOME"); //si no hay nada de argumento es HOME
    } else { 
        dir = line->commands[0].argv[1]; 
    }
    entorno = chdir(dir); // cambia el directorio actual
    if (entorno < 0) { //comprueba si devuelve error
        fprintf(stderr, "cd: No ha sido posible cambiar el directorio\n");
    } 
    else {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {// obtiene el directorio actual
             printf("%s\n", cwd);
        }
    }
    return;
}

void execute_umask(tline *line){
    mode_t old;
    char *end; // Puntero para control de errores en conversión
    unsigned long val; //valor long positivo lo usamos para evitar errores
    if (line->commands[0].argv[1] == NULL) { // comprueba si no hay máscara de argumento
        old = umask(0); // llama a umask(0) para obtener la actual
        umask(old); // restaura la máscara original
        printf("%04o\n", old); // en octal
    }
    else { 
        val = strtoul(line->commands[0].argv[1], &end, 8); // convierte string a long octal
        if (*end != '\0') { // comprueba si la máscara es válida
            fprintf(stderr, "umask: valor inválido\n");
        }
        else {
            umask((mode_t)val);  // pone máscara nueva
        }
    }
    return;
}

void execute_man(tline *line, char *full_line_str){ 
    int i;
    pid_t pid;
    int status;
    int npipes = line->ncommands - 1; // pipes de la línea
    int (*p)[2] = NULL; // puntero al array de pipes (por dos extremos)
    int fde, fds, fderr;

    if (npipes > 0) { 
        p = malloc(sizeof(int[2]) * npipes); // reserva memoria para los pipes
        for (i = 0; i < npipes; i++) { 
            if (pipe(p[i]) < 0) {
                fprintf(stderr, "pipe:Ha surgido un error al crear un pipe\n");;
                exit(1);
            }
        }
    }

    for (i = 0; i < line->ncommands; i++) { 
        pid = fork();     
        if (pid < 0) { // comprueba si hay error al hacer fork
            fprintf(stderr, "fork: error al hacer el fork\n");
            exit(1);
        }

        if (pid == 0) { // hijo
            if (!line->background) { // comprueba si el mandato se debe ejecutar en primer plano
                signal(SIGINT, SIG_DFL); // reactiva CtrlC
                signal(SIGTSTP, SIG_DFL); // reactiva CtrlZ
            } else {
                signal(SIGINT, SIG_IGN); // Ignora CtrlC
                signal(SIGTSTP, SIG_IGN); // Ignora CtrlZ
            }
            if (npipes > 0) { // comprueba si hay tuberías
                if (i == 0) { // comprueba si es el primer mandato
                    dup2(p[0][1], 1); // redirige la salida estándar al primer pipe
                } else if (i == line->ncommands - 1) { //comprueba si es el último
                    dup2(p[i-1][0], 0); //redirige la entrada del último pipe
                } else { // si es un mandato de en medio
                    dup2(p[i-1][0], 0); // lee del pipe anterior
                    dup2(p[i][1], 1); // escribe en el pipe siguiente
                }
                for (int k = 0; k < npipes; k++) { // bucle para cerrar los pipes en el hijo
                    close(p[k][0]); 
                    close(p[k][1]);
                }
            }
            if (i == 0 && line->redirect_input != NULL) { // comprueba si es el primer mandato y hay redirección de entrada
                fde = open(line->redirect_input, O_RDONLY); // abre el archivo solo para lectura
                if (fde < 0) { 
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
                    exit(1); 
                }
                dup2(fde, 0); // redirige la entrada estándar
                close(fde);
            }
            if (i == line->ncommands - 1) { // comprueba si es el último mandato
                if (line->redirect_output != NULL) { // mira si hay redirección de salida
                    fds = creat(line->redirect_output, 0644); // se crea un  fichero con permisos predeterminados
                    if (fds < 0) { 
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno)); 
                        exit(1); 
                    }
                    dup2(fds, 1);
                    close(fds); // cierra el descriptor
                }
                if (line->redirect_error != NULL) { // comprueba si hay redirección de error
                    fderr = creat(line->redirect_error, 0644); //se crea un fichero con unos permisos predeterminados
                    if (fderr < 0) { 
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno)); 
                        exit(1); 
                    }
                    dup2(fderr, 2);
                    close(fderr); // cierra el descriptor
                }
            }
            if (line->commands[i].filename == NULL) { // comprueba si el mandato existe
                fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
                exit(1);
            }
            execvp(line->commands[i].filename, line->commands[i].argv); // función de búsqueda del mandato 
            perror("execvp"); // si hubo error lo imprime
            exit(1);
        }
    }
    
    if (npipes > 0) { 
        for (i = 0; i < npipes; i++) { // recorremos todos los pipes para cerrar ambos extremos
            close(p[i][0]); 
            close(p[i][1]);
        }
        free(p);// libera la memoria de los pipes
    }
    if (line->background) { // comprueba si el mandato se lanzó a background
        printf("[%d]\n", pid); 
        insert_job(pid, full_line_str, 'R'); // se añade a la lista como estado de Running
    } 
    else { // el mandato se lanzó a foreground
        waitpid(pid, &status, WUNTRACED); // espera hasta que el último hijo termine
        if (WIFSTOPPED(status)) {   //comprueba si el hijo se detuvo con Ctrl+Z
            printf("\n[%d]+ Stopped %s\n", pid, full_line_str);
            insert_job(pid, full_line_str, 'S'); //se añade a la lista como estado de Stopped
        }
        for (i = 0; i < line->ncommands - 1; i++) {
            wait(NULL);
        }
    }
    return;
}


int main() {
    char buffer[SIZE]; // buffer para leer mandatos
    tline *line; // puntero a tline
    jobs_list = malloc(sizeof(jobList)); // reserva memoria para la lista
    jobs_list->head = NULL;
    
    signal(SIGINT, SIG_IGN); // desactivar Ctr+C
    signal(SIGTSTP, SIG_IGN); // desactivar CtrlZ
    signal(SIGCHLD, handler); // manejador de señales de procesos hijo

    printf("msh> ");
    while (fgets(buffer, SIZE, stdin)) {
        if (strlen(buffer) > 0) { // si el buffer no está vacío
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0'; //quitar salto de línea y acabar en /0 siempre
            }

            line = tokenize(buffer); // recopila info de la línea        
            if (line != NULL && line->ncommands > 0) {
                if (strcmp(line->commands[0].argv[0], "cd") == 0) //si el mandato es cd
                    execute_cd(line);
                else if (strcmp(line->commands[0].argv[0], "umask") == 0) //si el mandato es umask
                    execute_umask(line);
                else if (strcmp(line->commands[0].argv[0], "jobs") == 0) //si el mandato es jobs
                    execute_jobs();
                else if (strcmp(line->commands[0].argv[0], "bg") == 0) //si el mandato es bg
                    execute_bg(line);
                else if (strcmp(line->commands[0].argv[0], "exit") == 0) {//si el mandato es exit
                    if (line->commands[0].argv[1] == NULL) { //  si no hay argumento junto exit
                        free(jobs_list); // borra espacio de memoria dinámica
                        exit(0); // termina la minishell
                    }
                    else {
                        free(jobs_list); // borra espacio de memoria dinámica
                        exit(atoi(line->commands[0].argv[1])); // termina con el argumento dado
                    }
                }
                else
                    execute_man(line, buffer); //si no es un mandato de los de arriba se usa esto
            }
        }
        printf("msh> ");
    }
    free(jobs_list); // borra espacio de memoria dinámica
    return 0;
}

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

// --- ESTRUCTURAS DE DATOS ---
typedef struct job {
    pid_t pid;
    char buffer[1024];
    char estado; // 'R' = Running, 'S' = Stopped
    struct job *next;
} job; // Faltaba el nombre aquí

typedef struct jobs {
    job *head;
} jobs;

// Variable global para acceder desde los handlers
jobs *jobs_list; 

// --- FUNCIONES DE LISTA ---

void insert_job(pid_t pid, char *buffer, char estado) {
    job *n = malloc(sizeof(job));
    if (n == NULL) { perror("malloc"); return; }
    
    n->pid = pid;
    strcpy(n->buffer, buffer);
    n->estado = estado;
    n->next = jobs_list->head;
    jobs_list->head = n;
}

void elim_job(pid_t pid) {
    job *act = jobs_list->head;
    job *ant = NULL;

    while (act != NULL) {
        if (act->pid == pid) {
            if (ant == NULL) { // Es el primero
                jobs_list->head = act->next;
            } else {
                ant->next = act->next;
            }
            free(act);
            return;
        }
        ant = act;
        act = act->next;
    }
}

// Buscar trabajo por posición (1, 2...) o último si n=0
job *get_job_by_index(int n) {
    job *aux = jobs_list->head;
    int i = 1;
    
    // Si la lista está vacía
    if (aux == NULL) return NULL;

    // Si n es 0 o no se pasa argumento, buscamos el último añadido (que suele ser el head en inserción LIFO)
    // Pero el comando jobs suele listar 1, 2, 3...
    // Para simplificar, si n=0 devolvemos el head (último añadido)
    if (n == 0) return aux;

    while (aux != NULL && i < n) {
        aux = aux->next;
        i++;
    }
    return aux;
}

// --- MANEJADORES DE SEÑALES ---

// Manejador para SIGCHLD (cuando un hijo muere o cambia estado)
void handler_chld(int sig) {
    pid_t pid;
    int status;
    
    // WNOHANG: no bloquear si no hay hijos muertos
    // WUNTRACED: informar también si un hijo se ha parado (aunque aquí principalmente queremos limpiar zombies)
    // WCONTINUED: informar si ha continuado
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // El proceso terminó normal o por señal (ej: kill)
            elim_job(pid);
        }
        else if (WIFSTOPPED(status)) {
            // El proceso fue parado (Ctrl+Z). 
            // Ya debería estar en la lista si fue lanzado en fg y se detectó en exeCommand.
            // Si estaba en bg, cambiamos su estado.
            job *j = jobs_list->head;
            while(j){
                if(j->pid == pid) j->estado = 'S';
                j = j->next;
            }
        }
        else if (WIFCONTINUED(status)) {
            job *j = jobs_list->head;
            while(j){
                if(j->pid == pid) j->estado = 'R';
                j = j->next;
            }
        }
    }
}

// --- COMANDOS INTERNOS ---

void exeJobs() {
    int i = 1;
    job *aux = jobs_list->head;
    while(aux != NULL) {
        printf("[%d] %s %s\n", i, (aux->estado == 'R') ? "Running" : "Stopped", aux->buffer);
        aux = aux->next;
        i++;
    }
}

void exeBg(tline *line) {
    job *j = NULL;
    
    if (jobs_list->head == NULL) {
        fprintf(stderr, "bg: no hay trabajos actuales\n");
        return;
    }

    if (line->commands[0].argv[1] == NULL) {
        // Si no hay argumento, tomamos el primero de la lista (último añadido)
        // Lo ideal sería buscar el último PARADO, pero simplificamos al head.
        j = jobs_list->head;
    } else {
        int idx = atoi(line->commands[0].argv[1]);
        j = get_job_by_index(idx);
    }

    if (j != NULL) {
        if (j->estado == 'S') {
            // Enviamos señal para continuar
            kill(j->pid, SIGCONT);
            j->estado = 'R'; // Actualizamos estado manualmente
            printf("[%d]+ %s &\n", j->pid, j->buffer);
        } else {
            fprintf(stderr, "bg: el trabajo ya está ejecutándose\n");
        }
    } else {
        fprintf(stderr, "bg: no existe ese trabajo\n");
    }
}

void exeCD(tline *line){
    char *dir;
    int e;

    if (line->commands[0].argc == 1) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr,"cd: error obteniendo HOME\n");
            return;
        }
    } else {
        dir = line->commands[0].argv[1];
    }
    
    e = chdir(dir);
    if (e < 0) {
        perror("cd");
    } else {
        // Enunciado dice mostrar ruta absoluta tras cambio sin args (opcional según interpretación)
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
             printf("%s\n", cwd);
    }
}

void exeUmask(tline *line){
    if (line->commands[0].argv[1] == NULL) {
        mode_t old = umask(0);
        umask(old);
        printf("%04o\n", old);
    } else {
        char *end;
        unsigned long val = strtoul(line->commands[0].argv[1], &end, 8);
        if (*end != '\0') {
            fprintf(stderr, "umask: valor octal inválido\n");
            return;
        }
        umask((mode_t)val);
    }
}

void exeExit(tline *line){
    // Liberar memoria si fuera necesario
    exit(0);
}

// --- EJECUCIÓN DE COMANDOS ---

void exeCommand(tline *line, char *full_line_str){
    int i;
    pid_t pid;
    int status;
    int npipes = line->ncommands - 1;
    int (*p)[2] = NULL;

    // Crear pipes si son necesarios
    if (npipes > 0) {
        p = malloc(sizeof(int[2]) * npipes);
        for (i = 0; i < npipes; i++) {
            if (pipe(p[i]) < 0) {
                perror("pipe");
                exit(1);
            }
        }
    }

    // Loop para crear procesos
    for (i = 0; i < line->ncommands; i++) {
        pid = fork();
        
        if (pid < 0) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) { // HIJO
            // 1. Gestión de Señales en el HIJO
            // Si es background, debe ignorar INT y TSTP (heredado del padre en el main, pero aseguramos)
            // Si es foreground, debe restaurar el comportamiento por defecto
            if (!line->background) {
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
            } else {
                signal(SIGINT, SIG_IGN);
                signal(SIGTSTP, SIG_IGN);
            }

            // 2. Redirecciones de Pipes
            if (npipes > 0) {
                if (i == 0) { // Primero
                    dup2(p[0][1], 1);
                } else if (i == line->ncommands - 1) { // Último
                    dup2(p[i-1][0], 0);
                } else { // Intermedios
                    dup2(p[i-1][0], 0);
                    dup2(p[i][1], 1);
                }
                // Cerrar todos los pipes en el hijo
                for (int k = 0; k < npipes; k++) {
                    close(p[k][0]);
                    close(p[k][1]);
                }
            }

            // 3. Redirecciones de Archivos
            // Entrada (solo primer comando)
            if (i == 0 && line->redirect_input != NULL) {
                int fdin = open(line->redirect_input, O_RDONLY);
                if (fdin < 0) { fprintf(stderr, "%s: Error opening file\n", line->redirect_input); exit(1); }
                dup2(fdin, 0); close(fdin);
            }
            // Salida (solo último comando)
            if (i == line->ncommands - 1) {
                if (line->redirect_output != NULL) {
                    int fdout = creat(line->redirect_output, 0666); // 0666 permite ser modificado por umask
                    if (fdout < 0) { fprintf(stderr, "%s: Error creating file\n", line->redirect_output); exit(1); }
                    dup2(fdout, 1); close(fdout);
                }
                if (line->redirect_error != NULL) {
                    int fderr = creat(line->redirect_error, 0666);
                    if (fderr < 0) { fprintf(stderr, "%s: Error creating file\n", line->redirect_error); exit(1); }
                    dup2(fderr, 2); close(fderr);
                }
            }

            // 4. Ejecución
            if (line->commands[i].filename == NULL) {
                fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
                exit(1);
            }
            execvp(line->commands[i].filename, line->commands[i].argv);
            perror("execvp"); // Si falla execvp
            exit(1);
        }
    }

    // PADRE
    
    // Cerrar pipes en el padre
    if (npipes > 0) {
        for (i = 0; i < npipes; i++) {
            close(p[i][0]);
            close(p[i][1]);
        }
        free(p);
    }

    // Gestión del Background / Foreground
    if (line->background) {
        // Agregamos el último PID a la lista de trabajos como 'R'unning
        // Nota: en pipes complejos, lo normal es trackear el último pid o el grupo.
        // Aquí guardamos pid (el último del loop)
        printf("[%d]\n", pid);
        insert_job(pid, full_line_str, 'R');
    } else {
        // Foreground: Esperamos al último proceso
        // WUNTRACED permite detectar si el hijo se paró con Ctrl+Z
        waitpid(pid, &status, WUNTRACED);

        if (WIFSTOPPED(status)) {
            // Se pulsó Ctrl+Z
            printf("\n[%d]+ Stopped %s\n", pid, full_line_str);
            insert_job(pid, full_line_str, 'S');
        }
        
        // Esperamos a los otros hijos del pipe para que no queden zombies
        // (Aunque solo controlamos interactivamente el último)
        for (i = 0; i < line->ncommands - 1; i++) {
            wait(NULL); 
        }
    }
}


int main() {
    char linea[SIZE];
    tline *line;

    // Inicializar lista
    jobs_list = malloc(sizeof(jobs));
    jobs_list->head = NULL;

    // SEÑALES SHELL: Ignorar Ctrl+C y Ctrl+Z en la Shell
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    // Gestionar muerte de hijos (background o zombies)
    signal(SIGCHLD, handler_chld);

    printf("msh> ");
    while (fgets(linea, SIZE, stdin)) {
        // Eliminar el salto de línea al final para guardar bonito el nombre en jobs
        linea[strcspn(linea, "\n")] = 0;

        if (strlen(linea) > 0) {
            line = tokenize(linea); // Asumo que esta funcion no modifica 'linea' string original destructivamente
            
            if (line != NULL && line->ncommands > 0) {
                if (strcmp(line->commands[0].argv[0], "cd") == 0)
                    exeCD(line);
                else if (strcmp(line->commands[0].argv[0], "exit") == 0)
                    exeExit(line);
                else if (strcmp(line->commands[0].argv[0], "umask") == 0)
                    exeUmask(line);
                else if (strcmp(line->commands[0].argv[0], "jobs") == 0)
                    exeJobs();
                else if (strcmp(line->commands[0].argv[0], "bg") == 0)
                    exeBg(line);
                else
                    exeCommand(line, linea); // Pasamos linea original para guardar en jobs
            }
        }
        printf("msh> ");
    }

    return 0;
}
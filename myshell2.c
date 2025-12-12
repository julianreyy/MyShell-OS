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
#include <errno.h> // Necesario para "Descripción del error" 

#define SIZE 512
#define MAX_JOBS 20 // Límite arbitrario para la gestión simple de jobs

// Estructura para gestionar trabajos (jobs) 
typedef struct {
    pid_t pid;
    char command[SIZE];
    int active; // 1 = running, 2 = stopped, 0 = empty
} Job;

Job jobs_list[MAX_JOBS]; 

// Función auxiliar para añadir jobs (Mejora de funcionalidad)
void add_job(pid_t pid, char *cmd_line, int status) {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if (!jobs_list[i].active) {
            jobs_list[i].pid = pid;
            strncpy(jobs_list[i].command, cmd_line, SIZE - 1);
            jobs_list[i].active = status;
            // Mostrar info de background como pide el PDF [cite: 16]
            printf("[%d] %d\n", i + 1, pid); 
            return;
        }
    }
    fprintf(stderr, "Error: Tabla de trabajos llena\n");
}

// Manejador de señales
void handler(int sig) {
    int status;
    pid_t pid;
    
    // WNOHANG para no bloquear la shell.
    // WUNTRACED para detectar procesos parados (Ctrl-Z)
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        int i;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // El proceso ha terminado, limpiar de la lista de jobs si estaba ahí
            for (i = 0; i < MAX_JOBS; i++) {
                if (jobs_list[i].pid == pid) {
                    jobs_list[i].active = 0; // Liberar slot
                    break;
                }
            }
        } else if (WIFSTOPPED(status)) {
            // El proceso fue parado por SIGTSTP
             for (i = 0; i < MAX_JOBS; i++) {
                // Si ya existe, marcar como stopped
                if (jobs_list[i].pid == pid) {
                    jobs_list[i].active = 2; // 2 = Stopped
                    // Formato jobs sistema: [N]+ Stopped
                    printf("\n[%d]+ Stopped\t%s\n", i + 1, jobs_list[i].command);
                    return;
                }
            }
            // Si no estaba en la lista (era foreground), añadirlo
            add_job(pid, "Process", 2);
        }
    }
}

// Cierra tuberías
void closePipes(int (*p)[2], int n){
    int i;
    for (i = 0; i < n; i++){
        close(p[i][0]);
        close(p[i][1]);
    }
}

// Implementación del mandato interno jobs 
void exeJobs() {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs_list[i].active) {
            char *status_str = (jobs_list[i].active == 1) ? "Running" : "Stopped";
            printf("[%d]+ %s\t%s\n", i + 1, status_str, jobs_list[i].command);
        }
    }
}

// Implementación del mandato interno bg 
void exeBg(tline *line) {
    int job_idx = -1;
    int i;

    // Si no hay argumentos, buscar el último parado
    if (line->commands[0].argc == 1) {
        for (i = MAX_JOBS - 1; i >= 0; i--) {
            if (jobs_list[i].active == 2) { // 2 = Stopped
                job_idx = i;
                break;
            }
        }
    } else {
        // Argumento recibido (ej: bg 1)
        job_idx = atoi(line->commands[0].argv[1]) - 1;
    }

    if (job_idx >= 0 && job_idx < MAX_JOBS && jobs_list[job_idx].active == 2) {
        jobs_list[job_idx].active = 1; // Marcar como running
        kill(jobs_list[job_idx].pid, SIGCONT); // Enviar señal para continuar
        printf("[%d]+ %s &\n", job_idx + 1, jobs_list[job_idx].command);
    } else {
        fprintf(stderr, "bg: trabajo no encontrado o no está parado\n");
    }
}

void exeCommand(tline *line){
    int i;
    int fdin, fdout;
    pid_t pid;
    // OPTIMIZACIÓN: Memoria dinámica para pids 
    pid_t *pids = NULL; 

    int npipes = line->ncommands - 1;
    int (*p)[2] = NULL;

    // Asignación dinámica para el array de pids
    pids = malloc(line->ncommands * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc pids");
        exit(EXIT_FAILURE);
    }

    if (npipes > 0) {
        // Asignación dinámica para pipes 
        p = malloc(sizeof(int[2]) * npipes);
        if (p == NULL) {
            perror("malloc pipes");
            free(pids); // Limpiar memoria antes de salir
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < npipes; i++) {
            if (pipe(p[i]) < 0) {
                perror("pipe");
                free(pids);
                free(p);
                exit(EXIT_FAILURE);
            }
        }
    }

    for (i = 0; i < line->ncommands; i++){
        pid = fork();
        if (pid < 0){
            fprintf(stderr, "Error de fork\n");
            // Limpieza en caso de error
            free(pids);
            if(p) free(p);
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // HIJO
            // Configurar señales en el hijo
            if (!line->background) {
                signal(SIGINT, SIG_DFL);  // Foreground responde a Ctrl-C 
                signal(SIGTSTP, SIG_DFL); // Foreground responde a Ctrl-Z
            } else {
                signal(SIGINT, SIG_IGN);  // Background ignora Ctrl-C 
                signal(SIGTSTP, SIG_IGN);
            }

            // --- REDIRECCIÓN DE ENTRADA (<) ---
            if ((i == 0) && (line->redirect_input != NULL)) {
                fdin = open(line->redirect_input, O_RDONLY);
                if (fdin < 0) {
                    // CORRECCIÓN: Mensaje de error específico con descripción 
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
                    exit(1);
                }
                dup2(fdin, 0);
                close(fdin);
            }

            // --- REDIRECCIÓN DE SALIDA (>) ---
            if ((i == line->ncommands - 1) && (line->redirect_output != NULL)) {
                fdout = creat(line->redirect_output, 0664);
                if (fdout < 0) {
                    // CORRECCIÓN: Mensaje de error específico 
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));
                    exit(1);
                }
                dup2(fdout, 1);
                close(fdout);
            }

            // --- REDIRECCIÓN DE ERROR (>&) ---
            if ((i == line->ncommands - 1) && (line->redirect_error != NULL)) {
                fdout = creat(line->redirect_error, 0664);
                if (fdout < 0) {
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno));
                    exit(1);
                }
                dup2(fdout, 2);
                close(fdout);
            }

            // --- GESTIÓN DE PIPES ---
            if (npipes > 0) {
                if (i == 0) {
                    dup2(p[0][1], 1);
                }
                else if (i == line->ncommands - 1) {
                    dup2(p[i-1][0], 0);
                }
                else {
                    dup2(p[i-1][0], 0);
                    dup2(p[i][1], 1);
                }
                closePipes(p, npipes);
            }

            // --- EJECUCIÓN DEL MANDATO ---
            if (line->commands[i].filename == NULL) {
                // CORRECCIÓN: Mensaje específico exigido 
                fprintf(stderr, "mandato: No se encuentra el mandato\n");
                exit(1); // Salida con error controlado
            }

            execvp(line->commands[i].filename, line->commands[i].argv);
            // Si execvp falla, también se debe mostrar el mensaje estándar
            fprintf(stderr, "mandato: No se encuentra el mandato\n");
            exit(1);
        }
        else { // PADRE
            pids[i] = pid;
            // Si es background, añadir a la lista de jobs
            if (line->background) {
                add_job(pid, line->commands[i].argv[0], 1); // 1 = Running
            }
        }
    }

    // PADRE: Cerrar pipes y limpiar
    if (npipes > 0) {
        closePipes(p, npipes);
        free(p); 
    }

    if (!line->background) {
        // Esperar a todos los hijos si es foreground
        for (i = 0; i < line->ncommands; i++) {
             // WUNTRACED permite detectar si un hijo se paró con Ctrl-Z
             int status;
             waitpid(pids[i], &status, WUNTRACED);
             
             // Si el hijo fue parado (Ctrl-Z), añadirlo a jobs
             if (WIFSTOPPED(status)) {
                 add_job(pids[i], line->commands[i].argv[0], 2); // 2 = Stopped
             }
        }
    }
    // Si es background, el prompt sale inmediatamente (gestionado por el bucle main)

    // Liberar la memoria dinámica de pids 
    free(pids);
}

void exeCD(tline *line){
    char *home;
    int e;
    char buffer[SIZE];

    // CORRECCIÓN: cd no debe usarse con pipes [cite: 39]
    if (line->ncommands > 1) {
        fprintf(stderr, "cd: no se puede ejecutar con pipes\n");
        return;
    }

    if (line->commands[0].argc == 1) {
        home = getenv("HOME");
        if (home == NULL)
            fprintf(stderr,"error: variable HOME no definida\n");
        else {
            e = chdir(home);
            if (e < 0)
                fprintf(stderr,"error al cambiar a HOME: %s\n", strerror(errno));
            else {
                // CORRECCIÓN: Mostrar nueva ruta absoluta 
                if (getcwd(buffer, SIZE) != NULL) {
                    printf("%s\n", buffer);
                }
            }
        }
    }
    else if (line->commands[0].argc == 2) {
        e = chdir(line->commands[0].argv[1]);
        if (e < 0)
            // Usar strerror para descripción correcta
            fprintf(stderr,"%s: Error. %s\n", line->commands[0].argv[1], strerror(errno));
    }
    else {
        fprintf(stderr,"Uso: cd [dir]\n");
    }
}

void exeExit(tline *line){
    if (line->commands[0].argv[1] == NULL)
        exit(0);
    else
        exit(atoi(line->commands[0].argv[1]));
}

void exeUmask(tline *line){
    mode_t new_mask;
    mode_t old_mask;
    char *end;

    // CORRECCIÓN: umask no debe ejecutarse con pipes 
    if (line->ncommands > 1) {
        fprintf(stderr, "umask: no se puede ejecutar con pipes\n");
        return;
    }

    if (line->commands[0].argv[1] == NULL) {
        old_mask = umask(0);
        umask(old_mask);
        printf("%04o\n", old_mask);
    }
    else {
        unsigned long val = strtoul(line->commands[0].argv[1], &end, 8);
        if (*end != '\0') {
            fprintf(stderr, "umask: valor octal inválido\n");
            return;
        }
        new_mask = (mode_t) val;
        umask(new_mask);
    }
}

int main(int argc, char *argv[])
{
    tline *line;
    char linea[SIZE];

    // Inicializar lista de jobs
    memset(jobs_list, 0, sizeof(jobs_list));

    // CORRECCIÓN: Ignorar SIGINT y SIGTSTP en la shell [cite: 27, 28]
    // La shell no muere, solo los hijos responden (configurado en exeCommand)
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN); 
    
    signal(SIGCHLD, handler);

    printf("msh> "); // Prompt correcto [cite: 18]

    while (1) {
        if (fgets(linea, SIZE, stdin) != NULL) {
            // Verificar si la línea está vacía o solo enter
            if (strlen(linea) > 1 || (strlen(linea) == 1 && linea[0] != '\n')) {
                line = tokenize(linea);
                if (line != NULL && line->ncommands > 0) { // Verificar ncommands > 0 [cite: 24]
                    if (strcmp(line->commands[0].argv[0], "cd") == 0)
                        exeCD(line);
                    else if (strcmp(line->commands[0].argv[0], "exit") == 0)
                        exeExit(line);
                    else if (strcmp(line->commands[0].argv[0], "umask") == 0)
                        exeUmask(line);
                    else if (strcmp(line->commands[0].argv[0], "jobs") == 0) // Nuevo mandato
                        exeJobs();
                    else if (strcmp(line->commands[0].argv[0], "bg") == 0) // Nuevo mandato
                        exeBg(line);
                    else
                        exeCommand(line);
                }
            }
            printf("msh> ");
        } else {
             // Salir si fgets retorna NULL (ej. Ctrl+D)
             break;
        }
    }
    return 0;
}
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
    pid_t pid; // pid del trabajo
    char buffer[1024]; // buffer de caracteres que guarda el mandato
    char estado; // estado del trabajo  R: Running S: Stopped
    struct job *next; // Puntero al siguiente nodo de la lista
} job;

typedef struct jobList { // Define la estructura de una lista de trabajos
    job *head; // Puntero al primer elemento
} jobList;

// Sacamos jobs_list del main para que las otras funciones la vean.
jobList *jobs_list; 

void insert_job(pid_t pid, char *buffer, char estado) {
    job *n = malloc(sizeof(job)); // reserva memoria para un nuevo trabajo
    n->pid = pid; // asigna PID al nuevo nodo
    strcpy(n->buffer, buffer); // le añade el mandato a través del buffer
    n->estado = estado; // asigna el estado (R ó S)
    n->next = jobs_list->head; // el nodo apunta al primero de la lista
    jobs_list->head = n; // el nodo se convierte en el primero de la lista
    return;
}

void elim_job(pid_t pid) {
    job *current = jobs_list->head; // puntero auxiliar que empieza al principio de la lista
    job *aux = NULL; // puntero auxiliar
    while (current != NULL) { // mientras haya nodos en la lista
        if (current->pid == pid) { // si se encuentra el PID buscado
            if (aux == NULL) { // comprueba si es el primer nodo de la lista
                jobs_list->head = current->next; // la lista apunta al segundo de la lista
            } else { // si no es el primero
                aux->next = current->next; // el nodo anterior al buscado apunta al siguiente del nodo buscado
            }
            free(current); // libera memoria
            return; 
        }
        aux = current; // el auxiliar pasa a ser el actual
        current = current->next; // el actual pasa a ser el siguiente nodo de la lista
    }
    return;
}

job *get_job_by_index(int n) {
    job *aux = jobs_list->head; // puntero auxiliar que empieza al principio de la lista
    int i = 1; // Contador
    if (aux == NULL) { // comprueba si la lista está vacía
        return NULL;
    }
    if (n == 0){ // si n es cero se devuelve el último trabajo añadido que es el primero de la lista
        return aux; 
    }
    while (aux != NULL && i < n) { // mientras haya nodos en la lista y no se haya llegado a n
        aux = aux->next; // avanza al siguiente nodo
        i++; // incrementa el contador
    }
    return aux; // devuelve el nodo encontrado que corresponde con n
}

void handler(int sig) {
    pid_t pid;
    int status;
    job *j = jobs_list->head; // puntero trabajo apuntando al principio de la lista
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) { // bucle para actualizar los hijos
        if (WIFEXITED(status) || WIFSIGNALED(status)) { // comprueba si el hijo terminó
            elim_job(pid); // elimina el proceso de la lista de trabajos
        }
        else if (WIFSTOPPED(status)) {  // comprueba si el proceso fue parado
            j = jobs_list->head; // Reiniciamos el puntero j para buscar desde el principio
            while(j){ // mientras haya trabajos
                if(j->pid == pid) { // si coincide el PID
                    j->estado = 'S'; // cambia estado a Stopped
                    break; // Encontrado, salimos del while interno
                }
                j = j->next; // avanza al siguiente
            }
        }
        else if (WIFCONTINUED(status)) { // comprueba si el proceso fue reanudado
            j = jobs_list->head; // Reiniciamos el puntero j
            while(j){ // mientras haya trabajos
                if(j->pid == pid) { // si coincide el PID
                    j->estado = 'R'; // cambia estado a Running
                    break; // Encontrado, salimos del while interno
                }
                j = j->next; // avanza al siguiente
            }
        }
    }
    return;
}

void execute_jobs() {
    int i = 1;
    job *aux = jobs_list->head;
    while (aux != NULL) {
        const char *estado_str;
        if (aux->estado == 'R') {
            estado_str = "Running";
        } else {
            estado_str = "Stopped";
        }
        printf("[%d] %s %s\n", i, estado_str,aux->buffer);
        aux = aux->next;
        i++;
    }
    return;
}


void execute_bg(tline *line) {
    job *j = NULL; // puntero a trabajo
    if (jobs_list->head == NULL) { // comprueba si hay lista de mandatos
        fprintf(stderr, "bg: no hay trabajos actuales\n");
        return;
    }
    if (line->commands[0].argv[1] == NULL) { // comprueba si no se pasó un número como argumento
        j = jobs_list->head; // selecciona el último trabajo añadido
    } else {
        int idx = atoi(line->commands[0].argv[1]); // convierte el argumento a entero
        j = get_job_by_index(idx); // busca el trabajo con dicho número
    }
    if (j != NULL) { // comprueba si se encontró el trabajo
        if (j->estado == 'S') { // comprueba si estaba detenido
            kill(j->pid, SIGCONT); // envía SIGCONT al proceso para reanudarlo
            j->estado = 'R'; // cambiamos el estado
            printf("[%d]+ %s &\n", j->pid, j->buffer); // escribe el pid y el mandato
        } else { // si no estaba detenido
            fprintf(stderr, "bg: el mandato ya está ejecutándose\n");
        }
    } else { // si no se encontró nada
        fprintf(stderr, "bg: no existe el mandato\n");
    }
    return;
}

void execute_cd(tline *line){
    char *dir; // Puntero a directorio
    char cwd[1024]; // Buffer para la ruta
    int e; // controlador de error
    if (line->commands[0].argc == 1) { // comprueba si no hay ruta
        dir = getenv("HOME"); // bbtiene la ruta de HOME
    } else { // si se ha pasado una ruta
        dir = line->commands[0].argv[1]; 
    }
    e = chdir(dir); // cambia el directorio actual
    if (e < 0) { // comprueba si devuelve error
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
    unsigned long val; //valor long positivo
    if (line->commands[0].argv[1] == NULL) { // comprueba si no hay máscara como argumento
        old = umask(0); // llama a umask(0) para obtener la actual y limpiarla
        umask(old); // restaura la máscara original
        printf("%04o\n", old); // imprime la máscara
    }
    else { 
        val = strtoul(line->commands[0].argv[1], &end, 8); // convierte string a long octal
        if (*end != '\0') { // comprueba si la máscara es válida
            fprintf(stderr, "umask: valor inválido\n");
        }
        else {
            umask((mode_t)val);  // pone la máscara nueva
        }
    }
    return;
}

void execute_man(tline *line, char *full_line_str){ 
    int i;
    pid_t pid;
    int status;
    int npipes = line->ncommands - 1; // pipes de la línea
    int (*p)[2] = NULL; // puntero al array de pipes
    int fde, fds, fderr;

    if (npipes > 0) { // comprueba i hay más de un mandato
        p = malloc(sizeof(int[2]) * npipes); // reserva memoria para los pipes
        for (i = 0; i < npipes; i++) { // Bucle que recorre los pipe
            if (pipe(p[i]) < 0) { // comprueba que los pipe estén bien
                fprintf(stderr, "pipe:Ha surgido un error al crear un pipe\n");;
                exit(1);
            }
        }
    }

    for (i = 0; i < line->ncommands; i++) { // bucle que recorre el número de mandatos
        pid = fork();     
        if (pid < 0) { // comprueba si hay error al hacer fork
            fprintf(stderr, "fork: error al hacer el fork\n");
            exit(1);
        }

        if (pid == 0) { // verifica que sea el hijo
            if (!line->background) {    // comprueba si el mandato se debe ejecutar en primer plano
                signal(SIGINT, SIG_DFL); // reactiva Ctrl+C
                signal(SIGTSTP, SIG_DFL); // reactiva Ctrl+Z
            } else {
                signal(SIGINT, SIG_IGN); // Ignora Ctrl+C
                signal(SIGTSTP, SIG_IGN); // Ignora Ctrl+Z
            }
            if (npipes > 0) { // comprueba si hay tuberías
                if (i == 0) { // comprueba si es el primer mandato
                    dup2(p[0][1], 1); // redirige la salida estándar al primer pipe
                } else if (i == line->ncommands - 1) { //comprueba si es el último
                    dup2(p[i-1][0], 0); // redirige la entrada del último pipe
                } else { // si es un mandato intermedio
                    dup2(p[i-1][0], 0); // lee del pipe anterior
                    dup2(p[i][1], 1); // escribe en el pipe siguiente
                }
                for (int k = 0; k < npipes; k++) { // bucle para cerrar los pipe en el hijo
                    close(p[k][0]); // cierra lectura
                    close(p[k][1]); // cierra escritura
                }
            }
            if (i == 0 && line->redirect_input != NULL) { // comprueba si es el primer mandato y hay redirección de entrada
                fde = open(line->redirect_input, O_RDONLY); // abre el archivo solo para lectura
                if (fde < 0) { // comprueba si hay error
                    // --- CORRECCIÓN 4: fprintf necesita stderr como primer argumento ---
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
                    exit(1); 
                }
                dup2(fde, 0); // redirige la entrada estándar
                close(fde); // cierra el descriptor
            }
            if (i == line->ncommands - 1) { // comprueba si es el último mandato
                if (line->redirect_output != NULL) { // mira si hay redirección de salida
                    fds = creat(line->redirect_output, 0644); // crea un fichero con unos permisos comunes rw-r--r--
                    if (fds < 0) { // comprueba si hay error
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno)); 
                        exit(1); 
                    }
                    dup2(fds, 1); // redirige la salida estándar
                    close(fds); // cierra el descriptor
                }
                if (line->redirect_error != NULL) { // comprueba si hay redirección de error
                    fderr = creat(line->redirect_error, 0644); // crea un fichero con unos permisos comunes rw-r--r--
                    if (fderr < 0) { // comprueba si hay error
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno)); 
                        exit(1); 
                    }
                    dup2(fderr, 2); // redirige la salida de error
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
    // El 'else' estaba huérfano antes porque faltaba cerrar bloques correctamente o el parser se confundió
    // Aquí el bloque padre (que no está en un else respecto al fork, sino fuera del bucle de creación)
    
    if (npipes > 0) {  // comprueba si se crearon pipes
        for (i = 0; i < npipes; i++) { // recorre todos los pipes
            close(p[i][0]); // cierra lectura
            close(p[i][1]); // cierra escritura
        }
        free(p);// libera la memoria de los pipes
    }
    if (line->background) { // comprueba si el mandato se lanzó a background
        printf("[%d]\n", pid); // se imprime el PID del mandato
        insert_job(pid, full_line_str, 'R'); // se añade a la lista como Running
    } 
    else { // el mandato se lanzó a foreground
        waitpid(pid, &status, WUNTRACED); // espera hasta que el último hijo termine
        if (WIFSTOPPED(status)) {       // comprueba si el hijo se detuvo con Ctrl+Z
            printf("\n[%d]+ Stopped %s\n", pid, full_line_str);
            insert_job(pid, full_line_str, 'S'); // se añade a la lista como Stopped
        }
        for (i = 0; i < line->ncommands - 1; i++) { // bucle para el resto de hijos que han finalizado
            wait(NULL);
        }
    }
    return;
}


int main() {
    char buffer[SIZE]; // buffer para leer mandatos
    tline *line; // puntero a tline
    
    // jobs_list ya es global, solo reservamos memoria
    jobs_list = malloc(sizeof(jobList)); // reserva memoria para la lista
    jobs_list->head = NULL; // inicializa a NULL
    
    signal(SIGINT, SIG_IGN); // desactivar Ctrl+C
    signal(SIGTSTP, SIG_IGN); // desactivar Ctrl+Z
    signal(SIGCHLD, handler); // manejador de señales de procesos hijo

    printf("msh> ");
    while (fgets(buffer, SIZE, stdin)) { // mientras reciba líneas
        if (strlen(buffer) > 0) { // si el buffer no está vacío
            // Eliminar el salto de línea al final si existe (buena práctica en shells)
            if (buffer[strlen(buffer)-1] == '\n') buffer[strlen(buffer)-1] = '\0';
            
            line = tokenize(buffer); // recopila info de la línea        
            if (line != NULL && line->ncommands > 0) { // comprueba que se han guardado mandatos
                if (strcmp(line->commands[0].argv[0], "cd") == 0) //comprueba que el mandato sea cd
                    execute_cd(line);
                else if (strcmp(line->commands[0].argv[0], "umask") == 0) //comprueba que el mandato sea umask
                    execute_umask(line);
                else if (strcmp(line->commands[0].argv[0], "jobs") == 0) //comprueba que el mandato sea jobs
                    execute_jobs();
                else if (strcmp(line->commands[0].argv[0], "bg") == 0) //comprueba que el mandato sea bg
                    execute_bg(line);
                else if (strcmp(line->commands[0].argv[0], "exit") == 0) {// comprueba si se quiere salir
                    if (line->commands[0].argv[1] == NULL) { // comprueba si se da un código de salida
                        free(jobs_list); // borra espacio de memoria dinámica
                        exit(0); // termina la minishell
                    }
                    else {
                        free(jobs_list); // borra espacio de memoria dinámica
                        exit(atoi(line->commands[0].argv[1])); // termina con el código proporcionado
                    }
                }
                else
                    execute_man(line, buffer); // en otro caso ejecuta función para mandatos generales
            }
        }
        printf("msh> ");
    }
    free(jobs_list); // borra espacio de memoria dinámica
    return 0;
}

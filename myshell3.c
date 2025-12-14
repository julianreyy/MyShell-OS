#include <stdlib.h>     // Biblioteca estándar para gestión de memoria (malloc, free, exit, etc.)
#include <stdio.h>      // Biblioteca estándar para entrada/salida (printf, fprintf, fgets)
#include "parser.h"     // Cabecera propia que contiene la función tokenize y struct tline
#include <sys/types.h>  // Definiciones de tipos de datos del sistema (pid_t, mode_t)
#include <unistd.h>     // API POSIX (fork, execvp, pipe, dup2, chdir, getcwd)
#include <sys/wait.h>   // Macros y funciones para esperar procesos (waitpid, macros WIF...)
#include <string.h>     // Manipulación de cadenas (strcpy, strcmp, strlen)
#include <sys/stat.h>   // Información de archivos y permisos (umask, open)
#include <fcntl.h>      // Control de archivos (O_RDONLY, O_CREAT, etc.)
#include <signal.h>     // Gestión de señales (signal, kill, SIGINT, SIGCHLD)
#include <errno.h>      // Gestión de errores (perror usa errno)

#define SIZE 512        // Define una constante de tamaño 512 para buffers de lectura

// --- ESTRUCTURAS DE DATOS ---
typedef struct job {    // Define la estructura para un nodo de la lista de trabajos
    pid_t pid;          // Almacena el ID del proceso (PID)
    char buffer[1024];  // Almacena el comando que se ejecutó (texto)
    char estado;        // Almacena el estado: 'R' (Running/Ejecutando) o 'S' (Stopped/Parado)
    struct job *next;   // Puntero al siguiente nodo de la lista
} job;                  // Etiqueta del tipo de dato

typedef struct jobs {   // Define la estructura que contiene la lista
    job *head;          // Puntero a la cabeza (primer elemento) de la lista
} jobs;                 // Etiqueta del tipo de dato

// Variable global para acceder desde los handlers
jobs *jobs_list;        // Puntero global a la lista de trabajos (necesario para acceder desde señales)

// --- FUNCIONES DE LISTA ---

void insert_job(pid_t pid, char *buffer, char estado) { // Función para insertar un nuevo trabajo
    job *n = malloc(sizeof(job));       // Reserva memoria para un nuevo nodo job
    if (n == NULL) { perror("malloc"); return; } // Si falla la reserva, imprime error y sale
    
    n->pid = pid;                       // Asigna el PID al nuevo nodo
    strcpy(n->buffer, buffer);          // Copia el string del comando al buffer del nodo
    n->estado = estado;                 // Asigna el estado inicial ('R' o 'S')
    n->next = jobs_list->head;          // El siguiente del nuevo nodo es la antigua cabeza (inserción LIFO)
    jobs_list->head = n;                // Actualiza la cabeza de la lista para apuntar al nuevo nodo
}

void elim_job(pid_t pid) {              // Función para eliminar un trabajo por su PID
    job *act = jobs_list->head;         // Puntero al nodo actual, empieza en la cabeza
    job *ant = NULL;                    // Puntero al nodo anterior, empieza nulo

    while (act != NULL) {               // Recorre la lista mientras haya nodos
        if (act->pid == pid) {          // Si encuentra el PID buscado
            if (ant == NULL) {          // Si es el primer elemento de la lista
                jobs_list->head = act->next; // La cabeza pasa a ser el siguiente
            } else {                    // Si no es el primero
                ant->next = act->next;  // El anterior salta al siguiente del actual
            }
            free(act);                  // Libera la memoria del nodo encontrado
            return;                     // Termina la función
        }
        ant = act;                      // Avanza el puntero anterior
        act = act->next;                // Avanza el puntero actual
    }
}

// Buscar trabajo por posición (1, 2...) o último si n=0
job *get_job_by_index(int n) {          // Función para buscar un job por su índice visual
    job *aux = jobs_list->head;         // Puntero auxiliar al inicio de la lista
    int i = 1;                          // Contador de índice, empezando en 1
    
    // Si la lista está vacía
    if (aux == NULL) return NULL;       // Retorna NULL si no hay trabajos

    // Si n es 0 o no se pasa argumento, buscamos el último añadido (que suele ser el head en inserción LIFO)
    // Pero el comando jobs suele listar 1, 2, 3...
    // Para simplificar, si n=0 devolvemos el head (último añadido)
    if (n == 0) return aux;             // Retorna el último trabajo añadido (cabeza)

    while (aux != NULL && i < n) {      // Recorre hasta encontrar el índice n o fin de lista
        aux = aux->next;                // Avanza al siguiente nodo
        i++;                            // Incrementa el contador
    }
    return aux;                         // Retorna el nodo encontrado o NULL
}

// --- MANEJADORES DE SEÑALES ---

// Manejador para SIGCHLD (cuando un hijo muere o cambia estado)
void handler_chld(int sig) {            // Función manejadora de la señal SIGCHLD
    pid_t pid;                          // Variable para guardar el PID detectado
    int status;                         // Variable para guardar el estado de terminación
    
    // WNOHANG: no bloquear si no hay hijos muertos
    // WUNTRACED: informar también si un hijo se ha parado (Ctrl+Z)
    // WCONTINUED: informar si ha continuado
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) { // Bucle para limpiar/actualizar hijos
        if (WIFEXITED(status) || WIFSIGNALED(status)) { // Si terminó normal o por señal (ej: kill)
            elim_job(pid);              // Elimina el proceso de la lista de trabajos
        }
        else if (WIFSTOPPED(status)) {  // Si el proceso fue detenido (Ctrl+Z)
            // Ya debería estar en la lista si fue lanzado en fg y se detectó en exeCommand.
            // Si estaba en bg, cambiamos su estado.
            job *j = jobs_list->head;   // Inicia búsqueda en la lista
            while(j){                   // Recorre la lista
                if(j->pid == pid) j->estado = 'S'; // Si coincide el PID, cambia estado a 'S' (Stopped)
                j = j->next;            // Avanza
            }
        }
        else if (WIFCONTINUED(status)) { // Si el proceso fue reanudado (SIGCONT)
            job *j = jobs_list->head;   // Inicia búsqueda
            while(j){                   // Recorre la lista
                if(j->pid == pid) j->estado = 'R'; // Si coincide el PID, cambia estado a 'R' (Running)
                j = j->next;            // Avanza
            }
        }
    }
}

// --- COMANDOS INTERNOS ---

void exeJobs() {                        // Implementación del comando interno 'jobs'
    int i = 1;                          // Contador para enumerar los trabajos
    job *aux = jobs_list->head;         // Puntero auxiliar al inicio
    while(aux != NULL) {                // Recorre toda la lista
        // Imprime índice, estado (Running/Stopped) y el comando
        printf("[%d] %s %s\n", i, (aux->estado == 'R') ? "Running" : "Stopped", aux->buffer);
        aux = aux->next;                // Avanza al siguiente
        i++;                            // Incrementa índice
    }
}

void exeBg(tline *line) {               // Implementación del comando interno 'bg'
    job *j = NULL;                      // Puntero para el trabajo a reanudar
    
    if (jobs_list->head == NULL) {      // Verifica si la lista está vacía
        fprintf(stderr, "bg: no hay trabajos actuales\n"); // Imprime error
        return;                         // Sale de la función
    }

    if (line->commands[0].argv[1] == NULL) { // Si no se pasó argumento (ej: solo 'bg')
        // Si no hay argumento, tomamos el primero de la lista (último añadido)
        // Lo ideal sería buscar el último PARADO, pero simplificamos al head.
        j = jobs_list->head;            // Selecciona el último trabajo añadido
    } else {                            // Si hay argumento
        int idx = atoi(line->commands[0].argv[1]); // Convierte argumento a entero
        j = get_job_by_index(idx);      // Busca el trabajo por ese índice
    }

    if (j != NULL) {                    // Si se encontró el trabajo
        if (j->estado == 'S') {         // Verifica si está detenido
            // Enviamos señal para continuar
            kill(j->pid, SIGCONT);      // Envía SIGCONT al proceso para reanudarlo
            j->estado = 'R';            // Actualizamos estado manualmente a Running
            printf("[%d]+ %s &\n", j->pid, j->buffer); // Informa al usuario
        } else {                        // Si ya estaba corriendo
            fprintf(stderr, "bg: el trabajo ya está ejecutándose\n"); // Error
        }
    } else {                            // Si no se encontró el trabajo
        fprintf(stderr, "bg: no existe ese trabajo\n"); // Error
    }
}

void exeCD(tline *line){                // Implementación del comando interno 'cd'
    char *dir;                          // Puntero para el directorio destino
    int e;                              // Variable para capturar error

    if (line->commands[0].argc == 1) {  // Si no hay argumentos (solo 'cd')
        dir = getenv("HOME");           // Obtiene la variable de entorno HOME
        if (dir == NULL) {              // Si HOME no existe
            fprintf(stderr,"cd: error obteniendo HOME\n"); // Error
            return;                     // Sale
        }
    } else {                            // Si hay argumentos
        dir = line->commands[0].argv[1]; // Toma el primer argumento como directorio
    }
    
    e = chdir(dir);                     // Intenta cambiar el directorio actual
    if (e < 0) {                        // Si devuelve -1 (error)
        perror("cd");                   // Imprime la causa del error
    } else {                            // Si tuvo éxito
        // Enunciado dice mostrar ruta absoluta tras cambio sin args (opcional según interpretación)
        char cwd[1024];                 // Buffer para ruta actual
        if (getcwd(cwd, sizeof(cwd)) != NULL) // Obtiene el directorio actual
             printf("%s\n", cwd);       // Lo imprime
    }
}

void exeUmask(tline *line){             // Implementación del comando interno 'umask'
    if (line->commands[0].argv[1] == NULL) { // Si no hay argumentos
        mode_t old = umask(0);          // Llama a umask(0) para obtener la actual (hack común) y limpiarla
        umask(old);                     // Restaura la máscara original
        printf("%04o\n", old);          // Imprime la máscara en octal
    } else {                            // Si hay argumentos
        char *end;                      // Puntero para control de errores en conversión
        unsigned long val = strtoul(line->commands[0].argv[1], &end, 8); // Convierte string a long octal
        if (*end != '\0') {             // Si quedaron caracteres inválidos
            fprintf(stderr, "umask: valor octal inválido\n"); // Error
            return;                     // Sale
        }
        umask((mode_t)val);             // Aplica la nueva máscara
    }
}

void exeExit(tline *line){              // Implementación del comando interno 'exit'
    // Liberar memoria si fuera necesario (aquí confiamos en el SO al cerrar)
    exit(0);                            // Termina el proceso shell con código 0 (éxito)
}

// --- EJECUCIÓN DE COMANDOS ---

void exeCommand(tline *line, char *full_line_str){ // Función principal para ejecutar comandos externos
    int i;                              // Índice para bucles
    pid_t pid;                          // Para almacenar PID de fork
    int status;                         // Estado para waitpid
    int npipes = line->ncommands - 1;   // Calcula número de pipes necesarios
    int (*p)[2] = NULL;                 // Puntero a array de pipes (file descriptors)

    // Crear pipes si son necesarios
    if (npipes > 0) {                   // Si hay más de un comando
        p = malloc(sizeof(int[2]) * npipes); // Reserva memoria para los descriptores de pipe
        for (i = 0; i < npipes; i++) {  // Bucle para crear cada pipe
            if (pipe(p[i]) < 0) {       // Crea el pipe y verifica error
                perror("pipe");         // Imprime error
                exit(1);                // Termina si falla
            }
        }
    }

    // Loop para crear procesos
    for (i = 0; i < line->ncommands; i++) { // Bucle para cada comando de la línea
        pid = fork();                   // Clona el proceso
        
        if (pid < 0) {                  // Si fork devuelve < 0 es error
            perror("fork");             // Imprime error
            exit(1);                    // Termina
        }

        if (pid == 0) { // HIJO         // Código que ejecuta el proceso hijo
            // 1. Gestión de Señales en el HIJO
            // Si es background, debe ignorar INT y TSTP (heredado del padre en el main, pero aseguramos)
            // Si es foreground, debe restaurar el comportamiento por defecto
            if (!line->background) {    // Si es primer plano (foreground)
                signal(SIGINT, SIG_DFL); // Restaura acción por defecto para Ctrl+C
                signal(SIGTSTP, SIG_DFL); // Restaura acción por defecto para Ctrl+Z
            } else {                    // Si es segundo plano (background)
                signal(SIGINT, SIG_IGN); // Ignora Ctrl+C
                signal(SIGTSTP, SIG_IGN); // Ignora Ctrl+Z
            }

            // 2. Redirecciones de Pipes
            if (npipes > 0) {           // Si hay tuberías
                if (i == 0) { // Primero // Si es el primer comando
                    dup2(p[0][1], 1);   // Redirige su salida estándar al primer pipe
                } else if (i == line->ncommands - 1) { // Último // Si es el último
                    dup2(p[i-1][0], 0); // Redirige su entrada desde el último pipe anterior
                } else { // Intermedios // Si está en medio
                    dup2(p[i-1][0], 0); // Lee del pipe anterior
                    dup2(p[i][1], 1);   // Escribe en el pipe siguiente
                }
                // Cerrar todos los pipes en el hijo
                for (int k = 0; k < npipes; k++) { // Bucle para cerrar descriptores
                    close(p[k][0]);     // Cierra extremo lectura
                    close(p[k][1]);     // Cierra extremo escritura
                }
            }

            // 3. Redirecciones de Archivos
            // Entrada (solo primer comando)
            if (i == 0 && line->redirect_input != NULL) { // Si es el primero y hay redirección <
                int fdin = open(line->redirect_input, O_RDONLY); // Abre archivo solo lectura
                if (fdin < 0) { fprintf(stderr, "%s: Error opening file\n", line->redirect_input); exit(1); } // Error
                dup2(fdin, 0); close(fdin); // Redirige entrada estándar y cierra fd temporal
            }
            // Salida (solo último comando)
            if (i == line->ncommands - 1) { // Si es el último comando
                if (line->redirect_output != NULL) { // Si hay redirección >
                    int fdout = creat(line->redirect_output, 0666); // Crea archivo con permisos base 0666
                    if (fdout < 0) { fprintf(stderr, "%s: Error creating file\n", line->redirect_output); exit(1); } // Error
                    dup2(fdout, 1); close(fdout); // Redirige salida estándar y cierra fd
                }
                if (line->redirect_error != NULL) { // Si hay redirección de error (&>)
                    int fderr = creat(line->redirect_error, 0666); // Crea archivo
                    if (fderr < 0) { fprintf(stderr, "%s: Error creating file\n", line->redirect_error); exit(1); } // Error
                    dup2(fderr, 2); close(fderr); // Redirige salida de error (fd 2) y cierra
                }
            }

            // 4. Ejecución
            if (line->commands[i].filename == NULL) { // Verifica si el comando existe
                fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[i].argv[0]); // Error
                exit(1);                // Termina hijo con error
            }
            execvp(line->commands[i].filename, line->commands[i].argv); // Ejecuta el comando reemplazando imagen
            perror("execvp");           // Si retorna execvp, hubo error, imprime
            exit(1);                    // Termina hijo con error
        }
    }

    // PADRE                            // Código que ejecuta el padre
    
    // Cerrar pipes en el padre
    if (npipes > 0) {                   // Si se crearon pipes
        for (i = 0; i < npipes; i++) {  // Recorre todos los pipes
            close(p[i][0]);             // Cierra lectura en padre
            close(p[i][1]);             // Cierra escritura en padre
        }
        free(p);                        // Libera memoria del array de pipes
    }

    // Gestión del Background / Foreground
    if (line->background) {             // Si el comando se lanzó con &
        // Agregamos el último PID a la lista de trabajos como 'R'unning
        // Nota: en pipes complejos, lo normal es trackear el último pid o el grupo.
        // Aquí guardamos pid (el último del loop)
        printf("[%d]\n", pid);          // Imprime el PID del proceso lanzado
        insert_job(pid, full_line_str, 'R'); // Lo añade a la lista como Running
    } else {                            // Si es Foreground (sin &)
        // Foreground: Esperamos al último proceso
        // WUNTRACED permite detectar si el hijo se paró con Ctrl+Z
        waitpid(pid, &status, WUNTRACED); // Bloquea hasta que el último hijo termine o se pare

        if (WIFSTOPPED(status)) {       // Si el hijo se detuvo (Ctrl+Z)
            // Se pulsó Ctrl+Z
            printf("\n[%d]+ Stopped %s\n", pid, full_line_str); // Informa parada
            insert_job(pid, full_line_str, 'S'); // Añade a la lista como Stopped
        }
        
        // Esperamos a los otros hijos del pipe para que no queden zombies
        // (Aunque solo controlamos interactivamente el último)
        for (i = 0; i < line->ncommands - 1; i++) { // Bucle para el resto de hijos
            wait(NULL);                 // Espera su muerte sin guardar estado
        }
    }
}


int main() {                            // Función principal del programa
    char linea[SIZE];                   // Buffer para leer la línea de comandos del usuario
    tline *line;                        // Puntero a estructura parseada (definida en parser.h)

    // Inicializar lista
    jobs_list = malloc(sizeof(jobs));   // Reserva memoria para la estructura de la lista
    jobs_list->head = NULL;             // Inicializa la cabeza a NULL

    // SEÑALES SHELL: Ignorar Ctrl+C y Ctrl+Z en la Shell
    signal(SIGINT, SIG_IGN);            // La shell no muere con Ctrl+C
    signal(SIGTSTP, SIG_IGN);           // La shell no se detiene con Ctrl+Z
    // Gestionar muerte de hijos (background o zombies)
    signal(SIGCHLD, handler_chld);      // Asocia SIGCHLD al manejador para limpiar zombies

    printf("msh> ");                    // Imprime el prompt inicial
    while (fgets(linea, SIZE, stdin)) { // Lee línea de entrada estándar hasta EOF o error
        // Eliminar el salto de línea al final para guardar bonito el nombre en jobs
        linea[strcspn(linea, "\n")] = 0;// Busca '\n' y lo reemplaza por '\0'

        if (strlen(linea) > 0) {        // Si la línea no está vacía
            line = tokenize(linea);     // Llama al parser para dividir en comandos/argumentos
            
            if (line != NULL && line->ncommands > 0) { // Si el parseo fue correcto y hay comandos
                if (strcmp(line->commands[0].argv[0], "cd") == 0) // Compara si es comando 'cd'
                    exeCD(line);        // Ejecuta cd
                else if (strcmp(line->commands[0].argv[0], "exit") == 0) // Compara si es 'exit'
                    exeExit(line);      // Ejecuta exit
                else if (strcmp(line->commands[0].argv[0], "umask") == 0) // Compara si es 'umask'
                    exeUmask(line);     // Ejecuta umask
                else if (strcmp(line->commands[0].argv[0], "jobs") == 0) // Compara si es 'jobs'
                    exeJobs();          // Ejecuta jobs
                else if (strcmp(line->commands[0].argv[0], "bg") == 0) // Compara si es 'bg'
                    exeBg(line);        // Ejecuta bg
                else
                    exeCommand(line, linea); // Si no es interno, ejecuta externo
            }
        }
        printf("msh> ");                // Vuelve a imprimir el prompt para la siguiente vuelta
    }

    return 0;                           // Retorna 0 al sistema operativo al terminar
}
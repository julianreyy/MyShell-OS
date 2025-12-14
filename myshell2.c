#include <stdlib.h> // Librería estándar para gestión de memoria (malloc, free) y control de procesos (exit).
#include <stdio.h>  // Librería estándar de entrada/salida (printf, fprintf, fgets).
#include "parser.h" // Librería proporcionada en el apéndice para analizar la línea de comandos.
#include <sys/types.h> // Define tipos de datos del sistema como pid_t.
#include <unistd.h> // Proporciona acceso a la API POSIX (fork, pipe, dup2, execvp, chdir).
#include <sys/wait.h> // Macros y funciones para esperar procesos (waitpid, WNOHANG).
#include <string.h> // Funciones para manipulación de cadenas (strcmp, strlen, strncpy).
#include <sys/stat.h> // Funciones para obtener información de archivos y permisos (stat, umask).
#include <fcntl.h> // Control de archivos, necesario para las flags de open (O_RDONLY, etc.).
#include <signal.h> // Manejo de señales del sistema (SIGINT, SIGTSTP, signal, kill).
#include <errno.h> // Define la variable errno para identificar errores del sistema.

// Define una constante para el tamaño máximo de los buffers de texto.
#define SIZE 512  
// Define el número máximo de trabajos en segundo plano que podemos gestionar
#define MAX_JOBS 20 

// Estructura para gestionar trabajos (jobs) requerida para los mandatos jobs y bg
typedef struct {
    pid_t pid;          // Almacena el identificador del proceso (PID).
    char command[SIZE]; // Almacena el texto del mandato ejecutado.
    int active;         // Estado del trabajo: 1 = Ejecutando, 2 = Detenido, 0 = Vacío.
} Job; // Nombre del tipo de dato definido.

Job jobs_list[MAX_JOBS]; // Array global para almacenar la lista de trabajos en segundo plano.

// Función auxiliar para añadir un trabajo a la lista de jobs
void add_job(pid_t pid, char *cmd_line, int status) {
    int i; // Variable contador para el bucle.
    for (i = 0; i < MAX_JOBS; i++) { // Recorre toda la lista de trabajos buscando un hueco libre.
        if (!jobs_list[i].active) { // Si encuentra una posición donde active es 0 (vacío).
            jobs_list[i].pid = pid; // Guarda el PID del proceso en esa posición.
            strncpy(jobs_list[i].command, cmd_line, SIZE - 1); // Copia el nombre del mandato al array.
            jobs_list[i].active = status; // Establece el estado (1 para background, 2 para stopped).
            // Muestra el PID del proceso entre corchetes, requisito para procesos en background.
            printf("[%d] %d\n", i + 1, pid); 
            return; // Sale de la función tras añadir el trabajo.
        }
    }
    fprintf(stderr, "Error: Tabla de trabajos llena\n"); // Mensaje de error si no hay hueco en el array.
}

// Manejador de señales (Handler) para SIGCHLD (cuando un hijo cambia de estado)
void handler(int sig) {
    int status; // Variable para almacenar el estado de salida del proceso hijo.
    pid_t pid;  // Variable para almacenar el PID del proceso que ha cambiado de estado.
    
    // Bucle waitpid con opciones especiales:
    // -1: Espera a cualquier hijo.
    // WNOHANG: No bloquea la shell si no hay hijos que hayan cambiado de estado.
    // WUNTRACED: Detecta también procesos que han sido parados (ej. con Ctrl-Z).
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        int i; // Contador para buscar en la lista de jobs.
        
        // Comprueba si el proceso terminó normalmente (WIFEXITED) o fue matado por una señal (WIFSIGNALED).
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // Recorre la lista de jobs para limpiar el proceso que ha terminado.
            for (i = 0; i < MAX_JOBS; i++) {
                if (jobs_list[i].pid == pid) { // Si encuentra el PID en la lista.
                    jobs_list[i].active = 0; // Lo marca como inactivo (libera el slot).
                    break; // Sale del bucle de búsqueda.
                }
            }
        } 
        // Comprueba si el proceso fue detenido (parado) por una señal (ej. SIGTSTP/Ctrl-Z).
        else if (WIFSTOPPED(status)) {
            // Busca si el proceso ya estaba en la lista de jobs.
             for (i = 0; i < MAX_JOBS; i++) {
                if (jobs_list[i].pid == pid) { // Si ya existía en la lista.
                    jobs_list[i].active = 2; // Actualiza su estado a 2 (Stopped).
                    // Imprime mensaje formateado estilo sistema: [N]+ Stopped Comando.
                    printf("\n[%d]+ Stopped\t%s\n", i + 1, jobs_list[i].command);
                    return; // Sale de la función.
                }
            }
            // Si el proceso no estaba en la lista (era un proceso en foreground que se paró).
            add_job(pid, "Process", 2); // Lo añade a la lista de jobs como detenido.
        }
    }
}

// Función auxiliar para cerrar los descriptores de fichero de las tuberías
void closePipes(int (*p)[2], int n){
    int i; // Contador para el bucle.
    for (i = 0; i < n; i++){ // Itera sobre todas las tuberías creadas.
        close(p[i][0]); // Cierra el extremo de lectura de la tubería i.
        close(p[i][1]); // Cierra el extremo de escritura de la tubería i.
    }
}

// Implementación del mandato interno 'jobs' 
void exeJobs() {
    int i; // Contador para recorrer la lista.
    for (i = 0; i < MAX_JOBS; i++) { // Itera sobre todos los slots posibles.
        if (jobs_list[i].active) { // Si el slot tiene un trabajo activo (running o stopped).
            // Determina la cadena de texto según el estado numérico.
            char *status_str = (jobs_list[i].active == 1) ? "Running" : "Stopped";
            // Imprime la información del trabajo: índice, estado y comando.
            printf("[%d]+ %s\t%s\n", i + 1, status_str, jobs_list[i].command);
        }
    }
}
// Implementación del mandato interno 'bg' 
void exeBg(tline *line) {
    int job_idx = -1; // Índice del trabajo a reanudar.
    int i; // Contador.

    // Caso 1: 'bg' sin argumentos. Debe reanudar el último trabajo parado.
    if (line->commands[0].argc == 1) {
        for (i = MAX_JOBS - 1; i >= 0; i--) { // Busca desde el final hacia el principio.
            if (jobs_list[i].active == 2) { // 2 significa Stopped.
                job_idx = i; // Guarda el índice encontrado.
                break; // Deja de buscar.
            }
        }
    } else {
        // Caso 2: 'bg <id>'. El usuario especifica qué trabajo reanudar.
        job_idx = atoi(line->commands[0].argv[1]) - 1; // Convierte argumento a entero y ajusta a índice 0.
    }

    // Verifica si el índice es válido y si el trabajo está realmente parado.
    if (job_idx >= 0 && job_idx < MAX_JOBS && jobs_list[job_idx].active == 2) {
        jobs_list[job_idx].active = 1; // Cambia el estado en la lista a 1 (Running).
        kill(jobs_list[job_idx].pid, SIGCONT); // Envía la señal SIGCONT al proceso para reanudarlo.
        // Imprime confirmación de que el proceso pasa a background.
        printf("[%d]+ %s &\n", job_idx + 1, jobs_list[job_idx].command);
    } else {
        // Mensaje de error si no se encuentra el trabajo o no está parado.
        fprintf(stderr, "bg: trabajo no encontrado o no está parado\n");
    }
}

// Función principal para ejecutar comandos externos
void exeCommand(tline *line){
    int i; // Variable contador para bucles de mandatos.
    int fdin, fdout; // Descriptores de archivo para redirecciones.
    pid_t pid; // Variable para almacenar el PID del fork.
    pid_t *pids = NULL; // Puntero para array dinámico de PIDs.

    int npipes = line->ncommands - 1; // Calcula el número de tuberías necesarias.
    int (*p)[2] = NULL; // Puntero a array de tuberías (cada tubería tiene 2 enteros).

    // Asignación de memoria dinámica para almacenar los PIDs de los hijos.
    pids = malloc(line->ncommands * sizeof(pid_t));
    if (pids == NULL) { // Comprobación de error de malloc.
        perror("malloc pids"); // Imprime error del sistema.
        exit(EXIT_FAILURE); // Termina el proceso con fallo.
    }

    // Si hay más de un mandato, necesitamos crear tuberías.
    if (npipes > 0) {
         // Reserva memoria dinámica para las tuberías.
        p = malloc(sizeof(int[2]) * npipes);
        if (p == NULL) { // Comprobación de error de malloc.
            perror("malloc pipes");
            free(pids); // Libera memoria anterior antes de salir.
            exit(EXIT_FAILURE);
        }

        // Bucle para inicializar cada tubería.
        for (i = 0; i < npipes; i++) {
            if (pipe(p[i]) < 0) { // Crea la tubería i.
                perror("pipe"); // Error al crear tubería.
                free(pids); // Limpieza de memoria.
                free(p);    // Limpieza de memoria.
                exit(EXIT_FAILURE);
            }
        }
    }

    // Bucle principal para crear un proceso por cada mandato en la línea.
    for (i = 0; i < line->ncommands; i++){
        pid = fork(); // Crea un proceso hijo (clon del padre).
        if (pid < 0){ // Error en fork.
            fprintf(stderr, "Error de fork\n");
            free(pids); // Limpieza.
            if(p) free(p); // Limpieza.
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // --- CÓDIGO DEL PROCESO HIJO ---
            // Configuración de señales según si es background o foreground.
            if (!line->background) { // Si es foreground (primer plano).
                signal(SIGINT, SIG_DFL);   // Restaura comportamiento por defecto ante Ctrl-C (terminar).
                signal(SIGTSTP, SIG_DFL);  // Restaura comportamiento por defecto ante Ctrl-Z (parar).
            } else { // Si es background (segundo plano).
                signal(SIGINT, SIG_IGN);   // Ignora Ctrl-C.
                signal(SIGTSTP, SIG_IGN); // Ignora Ctrl-Z.
            }

            // --- REDIRECCIÓN DE ENTRADA (<) --- 
            if ((i == 0) && (line->redirect_input != NULL)) { // Solo para el primer mandato.
                fdin = open(line->redirect_input, O_RDONLY); // Abre el fichero en modo lectura.
                if (fdin < 0) { // Si falla al abrir.
                    // Imprime error con formato requerido: "fichero: Error. Descripción".
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
                    exit(1); // Termina el hijo con error.
                }
                dup2(fdin, 0); // Redirige la entrada estándar (0) al fichero.
                close(fdin);   // Cierra el descriptor auxiliar.
            }

            // --- REDIRECCIÓN DE SALIDA (>) --- 
            if ((i == line->ncommands - 1) && (line->redirect_output != NULL)) { // Solo último mandato.
                fdout = creat(line->redirect_output, 0664); // Crea fichero con permisos rw-rw-r--.
                if (fdout < 0) {
                    // Error al crear fichero.
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));
                    exit(1);
                }
                dup2(fdout, 1); // Redirige la salida estándar (1) al fichero.
                close(fdout);
            }

            // --- REDIRECCIÓN DE ERROR (>&) --- 
            if ((i == line->ncommands - 1) && (line->redirect_error != NULL)) { // Solo último mandato.
                fdout = creat(line->redirect_error, 0664); // Crea fichero.
                if (fdout < 0) {
                    fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno)); // 
                    exit(1);
                }
                dup2(fdout, 2); // Redirige la salida de error (2) al fichero.
                close(fdout);
            }

            // --- GESTIÓN DE PIPES --- 
            if (npipes > 0) {
                if (i == 0) { // Primer mandato.
                    dup2(p[0][1], 1); // Su salida va a la entrada del siguiente pipe.
                }
                else if (i == line->ncommands - 1) { // Último mandato.
                    dup2(p[i-1][0], 0); // Su entrada viene de la salida del pipe anterior.
                }
                else { // Mandatos intermedios.
                    dup2(p[i-1][0], 0); // Entrada del pipe anterior.
                    dup2(p[i][1], 1);   // Salida al pipe siguiente.
                }
                closePipes(p, npipes); // Cierra todas las tuberías en el hijo (ya duplicadas).
            }

            // --- EJECUCIÓN DEL MANDATO ---
            if (line->commands[i].filename == NULL) { // Si el parser no encontró el ejecutable.
                // Imprime el error literal exigido por el enunciado.
                fprintf(stderr, "mandato: No se encuentra el mandato\n");
                exit(1); // Termina con error.
            }

            // Reemplaza la imagen del proceso actual por el mandato especificado.
            execvp(line->commands[i].filename, line->commands[i].argv);
            
            // Si execvp retorna, significa que hubo un error (ej. comando no existe/permisos).
            fprintf(stderr, "mandato: No se encuentra el mandato\n"); // 
            exit(1);
        }
        else { // --- CÓDIGO DEL PROCESO PADRE ---
            pids[i] = pid; // Guarda el PID del hijo creado en el array.
            // Si toda la línea se ejecuta en background... 
            if (line->background) {
                add_job(pid, line->commands[i].argv[0], 1); // Añade el hijo a la lista de jobs como Running.
            }
        }
    }

    // PADRE: Una vez lanzados todos los hijos, cierra las tuberías (el padre no las usa).
    if (npipes > 0) {
        closePipes(p, npipes);
        free(p); // Libera la memoria de las tuberías.
    }

    // Si la ejecución NO es en background (es foreground).
    if (!line->background) {
        // Espera a todos los hijos creados.
        for (i = 0; i < line->ncommands; i++) {
             // WUNTRACED permite al padre saber si un hijo se paró con Ctrl-Z.
             int status;
             waitpid(pids[i], &status, WUNTRACED);
             
             // Si el hijo fue parado (STOPPED) en lugar de terminar.
             if (WIFSTOPPED(status)) {
                 add_job(pids[i], line->commands[i].argv[0], 2); // Lo añade a jobs como Stopped (2).
             }
        }
    // Si es background, no hacemos waitpid aquí; el prompt sale inmediatamente.

    // Libera la memoria dinámica usada para almacenar los PIDs.
    free(pids);
}

// Implementación del mandato interno 'cd' 
void exeCD(tline *line){
    char *home; // Puntero para almacenar la ruta del directorio home.
    int e; // Variable para recoger errores.
    char buffer[SIZE]; // Buffer para guardar la ruta actual.

    // Comprobación: 'cd' no debe ejecutarse con pipes.
    if (line->ncommands > 1) {
        fprintf(stderr, "cd: no se puede ejecutar con pipes\n");
        return;
    }

    // Caso 1: 'cd' sin argumentos -> ir a HOME.
    if (line->commands[0].argc == 1) {
        home = getenv("HOME"); // Obtiene la variable de entorno HOME.
        if (home == NULL)
            fprintf(stderr,"error: variable HOME no definida\n");
        else {
            e = chdir(home); // Cambia el directorio actual.
            if (e < 0) // Si falla.
                fprintf(stderr,"error al cambiar a HOME: %s\n", strerror(errno));
            else {
                // Requisito: Escribir la ruta absoluta del nuevo directorio.
                if (getcwd(buffer, SIZE) != NULL) {
                    printf("%s\n", buffer);
                }
            }
        }
    }
    // Caso 2: 'cd directorio'.
    else if (line->commands[0].argc == 2) {
        e = chdir(line->commands[0].argv[1]); // Intenta cambiar al directorio especificado.
        if (e < 0)
            // Imprime error con descripción correcta si falla.
            fprintf(stderr,"%s: Error. %s\n", line->commands[0].argv[1], strerror(errno));
    }
    else {
        fprintf(stderr,"Uso: cd [dir]\n"); // Error de sintaxis.
    }
}
// Implementación del mandato interno 'exit' 
void exeExit(tline *line){
    // Si hay un argumento, se usa como código de retorno; si no, 0.
    if (line->commands[0].argv[1] == NULL)
        exit(0); // Termina la minishell ordenadamente con éxito.
    else
        exit(atoi(line->commands[0].argv[1])); // Termina con el código proporcionado.
}
// Implementación del mandato interno 'umask' 
void exeUmask(tline *line){
    mode_t new_mask; // Variable para la nueva máscara.
    mode_t old_mask; // Variable para la máscara antigua.
    char *end; // Puntero auxiliar para conversión numérica.

    // Comprobación: 'umask' no debe ejecutarse con pipes.
    if (line->ncommands > 1) {
        fprintf(stderr, "umask: no se puede ejecutar con pipes\n");
        return;
    }

    // Caso 1: Sin argumentos -> mostrar máscara actual.
    if (line->commands[0].argv[1] == NULL) {
        old_mask = umask(0); // Llama a umask para obtener la anterior.
        umask(old_mask); // Restaura la máscara inmediatamente (solo queríamos leerla).
        printf("%04o\n", old_mask); // Imprime en octal (%o).
    }
    else {
        // Caso 2: Con argumento octal -> establecer nueva máscara.
        unsigned long val = strtoul(line->commands[0].argv[1], &end, 8); // Convierte string octal a long.
        if (*end != '\0') { // Verifica si la conversión fue válida.
            fprintf(stderr, "umask: valor octal inválido\n");
            return;
        }
        new_mask = (mode_t) val; // Casteo a tipo mode_t.
        umask(new_mask); // Aplica la nueva máscara al sistema.
    }
}

int main(int argc, char *argv[])
{
    tline *line; // Puntero a la estructura devuelta por el parser.
    char linea[SIZE]; // Buffer para leer la entrada del teclado.

    // Inicializa la lista de trabajos poniendo todos los bytes a 0.
    memset(jobs_list, 0, sizeof(jobs_list));

    // Configuración de señales para la minishell (Padre).
    // Ignora Ctrl-C y Ctrl-Z para que la shell no muera ni se pare.
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN); 
    
    // Instala el manejador para SIGCHLD para limpiar zombis y gestionar jobs.
    signal(SIGCHLD, handler);

    printf("msh> "); // Muestra el prompt inicial.

    // Bucle infinito principal de la shell.
    while (1) {
        // Lee una línea desde la entrada estándar (stdin).
        if (fgets(linea, SIZE, stdin) != NULL) {
            // Verifica que la línea no esté vacía o sea solo un salto de línea.
            if (strlen(linea) > 1 || (strlen(linea) == 1 && linea[0] != '\n')) {
                line = tokenize(linea); // Llama al parser para analizar la línea.
                
                // Si el parser devuelve algo válido y hay mandatos.
                if (line != NULL && line->ncommands > 0) { 
                    // Compara el primer comando para ver si es interno o externo.
                    if (strcmp(line->commands[0].argv[0], "cd") == 0) // 
                        exeCD(line);
                    else if (strcmp(line->commands[0].argv[0], "exit") == 0) // 
                        exeExit(line);
                    else if (strcmp(line->commands[0].argv[0], "umask") == 0) // 
                        exeUmask(line);
                    else if (strcmp(line->commands[0].argv[0], "jobs") == 0) // Nuevo mandato jobs.
                        exeJobs();
                    else if (strcmp(line->commands[0].argv[0], "bg") == 0) // Nuevo mandato bg.
                        exeBg(line);
                    else
                        // Si no es interno, es un mandato externo (ls, cat, etc.).
                        exeCommand(line);
                }
            }
            printf("msh> "); // Vuelve a mostrar el prompt tras ejecutar.
        } else {
             // Si fgets retorna NULL (ej. fin de fichero o Ctrl+D), sale del bucle.
             break;
        }
    }
    return 0; // Termina el programa main.
}

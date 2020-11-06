/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <wait.h>
#include <fcntl.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

pid_t execute(char **cmd, int fd_in, int fd_out);

/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>
#include <wordexp.h>
#include <stdbool.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */
	struct cmdline *l = parsecmd(&line);
	if (!l || l->seq[0] == NULL)
	    return 0;

	pid_t pid = execute(l->seq[0], STDIN_FILENO, STDOUT_FILENO);
	int status = 0;
	waitpid(pid, &status, 0);
    int exit_code = WEXITSTATUS(status);
    if (exit_code != 0) {
        printf("Process exited with code %d\n", exit_code);
    }
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif

typedef struct process {
    uint pid;
    char *name;
    struct process *next;
} Process_t;

Process_t *bg_processes = NULL;

void insert_bg_process(uint pid, const char *name) {
    Process_t *old_process = bg_processes;
    bg_processes = malloc(sizeof(Process_t));
    bg_processes->pid = pid;
    bg_processes->name = malloc(strlen(name));
    strcpy(bg_processes->name, name);
    bg_processes->next = old_process;
}

void update_bg_processes() {
    int status = 0;
    Process_t *previous = NULL;
    Process_t *current = bg_processes;

    while (current != NULL) {
        if (waitpid(current->pid, &status, WNOHANG) > 0 && WIFEXITED(status)) { // Le process s'est terminé
            int exit_code = WEXITSTATUS(status);
            printf("Process \"%s\" (PID %d) exited with code %d\n", current->name, current->pid, exit_code);
            if (previous != NULL)
                previous->next = current->next;
            else if (current == bg_processes)
                bg_processes = current->next;
            free(current->name);
            free(current);
            if (previous != NULL)
                current = previous->next;
            else {
                current = bg_processes;
            }
        } else {
            previous = current;
            current = current->next;
        }
    }
}

void print_bg_processes() {
    if (bg_processes == NULL) {
        printf("No jobs are currently running.\n");
        return;
    }

    printf("Current jobs:\n");
    printf("PID     Name\n");
    printf("---------------------------------------------\n");
    Process_t *current = bg_processes;
    while (current != NULL) {
        printf("%-7d %s\n", current->pid, current->name);
        current = current->next;
    }
}

void clear_bg_processes() {
    Process_t *next;
    while (bg_processes != NULL) {
        next = bg_processes->next;
        free(bg_processes->name);
        free(bg_processes);
        bg_processes = next;
    }
}

pid_t execute(char **cmd, int fd_in, int fd_out) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fd_in != STDIN_FILENO)
        {
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (fd_out != STDOUT_FILENO)
        {
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        char* const *params = cmd;
        execvp(cmd[0], params);
        exit(errno);
    }

    if (fd_in != STDIN_FILENO)
        close(fd_in);
    if (fd_out != STDOUT_FILENO)
        close(fd_out);

    return pid;
}

void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	clear_bg_processes();
	printf("exit\n");
	exit(0);
}

int main() {
    printf("Variante %d: %s\n", VARIANTE, VARIANTE_STRING);

#if USE_GUILE == 1
    scm_init_guile();
    /* register "executer" function in scheme */
    scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif


    char *code = 0;
	while (1) {
		struct cmdline *l;
		char *line=0;
		int i;
		char *prompt = "ensishell>";

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || ! strncmp(line,"exit", 4)) {
			code = line;
            break;
		}

#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
        if (line[0] == '(') {
            char catchligne[strlen(line) + 256];
            sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
            scm_eval_string(scm_from_locale_string(catchligne));
            free(line);
            continue;
        }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
		  
			break;
		}
		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}

		if (l->seq[0] != NULL && !strcmp(l->seq[0][0], "jobs")) {
		    update_bg_processes();
		    print_bg_processes();
            continue;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		int pipe_left[2];
		int pipe_right[2];

		int nb_cmd = 0;
        for (i = 0; l->seq[i] != 0; i++)
            nb_cmd++;

        pid_t pids[nb_cmd];

		/* Display each command of the pipe */
		for (i = 0; l->seq[i] != 0; i++) {
		    pids[i] = -1;

		    int fd_in = STDIN_FILENO;
		    int fd_out = STDOUT_FILENO;

		    if (nb_cmd > 1) { // We need pipes
		        if (i > 0) { // We need to connect to left pipe
		            fd_in = pipe_left[0];
		        }
		        if (i < nb_cmd - 1) { // We need to create a right pipe
		            pipe(pipe_right);
		            fd_out = pipe_right[1];
		        }
		    }

		    if (i == 0 && l->in) {
		        fd_in = open(l->in, O_RDONLY);
		        if (fd_in < 0) {
		            printf("Error: Cannot open file \"%s\"\n", l->in);
		            if (nb_cmd > 1) {
                        close(pipe_right[0]);
                        close(pipe_right[1]);
                    }
                    break;
		        }
		    }

            if (i == nb_cmd - 1 && l->out) {
                fd_out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0) {
                    printf("Error: Cannot open file \"%s\"\n", l->out);
                    if (nb_cmd > 1) {
                        close(pipe_left[0]);
                    }
                    break;
                }
            }

            // On compte le nombre de mots dans la commande de base
            uint word_count = 0;
            for (int j = 0; l->seq[i][j] != 0; j++)
                word_count++;

            // On applique wordexp quand on peut et on en profite pour calculer le nouveau total de mots en conséquence
            uint total_word_count = 0;
            wordexp_t wexps[word_count];
            bool is_wordexp[word_count];
            for (int j = 0; j < word_count; j++) {
                if ( // wordexp ne supporte pas certains caractères
                        strstr(l->seq[i][j], "|") == NULL && strstr(l->seq[i][j], "&") == NULL && strstr(l->seq[i][j], ";") == NULL
                        && strstr(l->seq[i][j], "<") == NULL && strstr(l->seq[i][j], ">") == NULL && strstr(l->seq[i][j], "(") == NULL
                        && strstr(l->seq[i][j], ")") == NULL && strstr(l->seq[i][j], "{") == NULL && strstr(l->seq[i][j], "}") == NULL
                        ) {
                    wordexp(l->seq[i][j], &wexps[j], WRDE_SHOWERR);
                    total_word_count += wexps[j].we_wordc;
                    is_wordexp[j] = true;
                } else {
                    total_word_count++;
                    is_wordexp[j] = false;
                }
            }

            // On prépare notre nouveau tableau de mot, de la bonne taille (+ 1 pour le NULL à la fin)
            char **words = malloc((total_word_count + 1) * sizeof(char *));
            int current_index = 0;
            for (int j = 0; j < word_count; j++) {
                if (is_wordexp[j]) { // Si c'est un wordexp, on place toutes les valeurs possibles dans le nouveau tableau
                    for (int k = 0; k < wexps[j].we_wordc; k++)
                        words[current_index++] = wexps[j].we_wordv[k];
                } else words[current_index++] = l->seq[i][j]; // Sinon, on remet juste le mot d'origine
            }
            words[current_index] = NULL; // Le NULL de fin

		    pids[i] = execute((char **)words, fd_in, fd_out);
		    // On libère tout ça...
            free(words);
            for (int j = 0; j < word_count; j++)
                if (is_wordexp[j]) wordfree(&wexps[j]);
		    pipe_left[0] = pipe_right[0];
		    pipe_left[1] = pipe_right[1];
		}

        int status;
        for (i = 0; i < nb_cmd; i++) {
            if (pids[i] == -1)
                continue;

            waitpid(pids[i], &status, l->bg ? WNOHANG : 0);

            if (!l->bg) { // On check le status tout de suite car on a attendu et le processus est terminé
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0)
                    printf("Process exited with code %d\n", exit_code);
            } else {
                insert_bg_process(pids[i], l->seq[i][0]);
            }
        }
	}

    terminate(code);

}

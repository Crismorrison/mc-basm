
/** \file background.h
 *  \brief Header: Background support
 */

#ifndef MC_BACKGROUND_H
#define MC_BACKGROUND_H

#ifdef WITH_BACKGROUND

#include <sys/types.h> /* pid_t */

enum TaskState {
    Task_Running,
    Task_Stopped
};

typedef struct TaskList {
    int fd;
    int to_child_fd;
    pid_t pid;
    int state;
    char *info;
    struct TaskList *next;
} TaskList;

extern struct TaskList *task_list;

struct FileOpContext;
int do_background (struct FileOpContext *ctx, char *info);
int parent_call (void *routine, struct FileOpContext *ctx, int argc, ...);
char *parent_call_string (void *routine, int argc, ...);

void unregister_task_running (pid_t pid, int fd);
void unregister_task_with_pid (pid_t pid);
extern int we_are_background;

#endif				/* !WITH_BACKGROUND */

#endif				/* MC_BACKGROUND_H */

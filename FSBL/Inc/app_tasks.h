#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "app_status.h"

#ifdef __cplusplus
extern "C" {
#endif

AppStatus_t AppTasks_Create(void);
void AppTasks_StartScheduler(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */

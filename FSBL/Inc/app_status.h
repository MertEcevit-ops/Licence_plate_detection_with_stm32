#ifndef APP_STATUS_H
#define APP_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  APP_STATUS_OK = 0,
  APP_STATUS_ERROR,
  APP_STATUS_INVALID_ARG,
  APP_STATUS_BUSY,
  APP_STATUS_TIMEOUT,
  APP_STATUS_SECURITY_ERROR,
  APP_STATUS_PROTOCOL_ERROR
} AppStatus_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_STATUS_H */

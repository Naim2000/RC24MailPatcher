#ifndef NETWORK_H
#define NETWORK_H

#include <gccore.h>

s32 post_request(char *url, char *post_arguments, char **response);
const char *cURL_GetLastError(void);

#endif

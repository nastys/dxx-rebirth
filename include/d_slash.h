//created on 9/4/99 by Victor Rachels

#ifndef _D_SLASH
#define _D_SLASH

#ifdef _WIN32
#define USEDSLASH '\\'
#define CHANGESLASH '/'
#else
#define USEDSLASH '/'
#define CHANGESLASH '\\'
#endif

void d_slash(char *path);

#endif

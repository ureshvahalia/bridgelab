#ifndef _TRANSLATIONS_H_
#define _TRANSLATIONS_H_

#include "consts.h"

#ifdef __cplusplus
extern "C" {
#endif
char* PBN2oneHand (const char*, oneHand);
char* writePbnHand (char* where, int* nh, int* eh, int* sh, int* wh);
/* writes hand described by nh,eh,sh,wh in pbn format at where, returns pointer to end of string */
char* writeLINboard (char* where, int* nh, int* eh, int* sh, int* wh, int board_num);
/* writes a BBO LIN board record; board_num is 1-based; returns pointer past terminating newline */
#ifdef __cplusplus
}
#endif

#endif  // _TRANSLATIONS_H_

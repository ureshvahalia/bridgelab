#ifndef _CONSTS_H_
#define _CONSTS_H_
#define NCOMMA	        26
#define NVOID	        27
#define MAXTRIES	    500000000
#define MAX_REPS	    1000000
#define MAXNAMELEN	    256
#define LINE_LENGTH     1024
#define MAXCONDITIONS	64
#define NSUITS          4
#define NHANDS          4
#define NSTRAINS        5
#define NCARDS_IN_SUIT  13
#define NCARDS_IN_HAND  13
#define MAX_TRICKS      NCARDS_IN_HAND
#define PACK_SIZE       52
#define SUIT_SELECTOR   8
#define ACE     0
#define KING    1
#define QUEEN   2
#define JACK    3

#define SPADES      0
#define HEARTS      1
#define DIAMONDS    2
#define CLUBS       3

typedef char record[NSUITS][16];
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif /* min */
enum outputFormat   {
    normal,
    longForm,
    pbn
};

typedef int oneHand[NCARDS_IN_HAND];

#endif

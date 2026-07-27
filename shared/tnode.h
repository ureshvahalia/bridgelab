#ifndef _TNODE_H_
#define _TNODE_H_

/* Values of t_type */
enum nodeType	{
    TINT,
    TASSIGN,
    TKWORD,
    TSUITFUNC,
    TSPOT,
    TSHAPE,
    TPATTERN,
    TNEG,
    TPLUS,
    TMINUS,
    TMULT,
    TDIV,
    TMOD,
    TBITAND,
    TBITOR,
    TEQU,
    TNEQ,
    TGT,
    TGEQ,
    TLT,
    TLEQ,
    TNOT,
    TAND,
    TOR,
    TXOR,
    TDEFINE,
    TDEFNAME
};

#define TNODE_LEN 1024
struct tnode {
    struct tnode*	t_left;
    struct tnode*	t_right;
    enum nodeType	t_type;
    long long       t_val;
    long long       t_result;
    char            t_desc[TNODE_LEN];
};
typedef struct tnode* TPTR;
extern TPTR defroot;

#define TOPNODE		0
#define LEFTNODE	1
#define RIGHTNODE	2

#ifdef __cplusplus
extern "C"  {
#endif
extern int traverse_lrt (TPTR, void (*)(TPTR, int, int, void*), int, int, int (*)(TPTR, int), void*);
extern TPTR make_leaf (enum nodeType, long long);
extern TPTR add_leaves (TPTR, TPTR, TPTR);
extern void* find_rule (void*, const char*);
extern void* find_def_node (void*, const char*);
extern TPTR match_string (char*);
#ifdef BIDDER
extern void* combineRule (void* l, void* r);
extern void  setSystem (void* sp);
extern void* next_rule (void*);
extern char* rule_name (void*);
extern void* rule_def (void*);
#endif /* BIDDER */
extern void write_leaf (TPTR);
extern void write_node (TPTR);
#ifdef __cplusplus
}
#endif
#endif /* _TNODE_H_ */

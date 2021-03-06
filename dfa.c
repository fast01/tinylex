#include <stdio.h>
#include <stdlib.h>
#include <set.h>
#include <nfa.h>
#include <dfa.h>
#include <lib.h>

/* dfa sets auxiliary function */
static int currentdfa;
int ndfas;
struct dfa *dfastates = NULL;

void free_dfa(struct dfa *dfa)
{
	if (dfa->states) {
		freeset(dfa->states);
		dfa->states = NULL;
	}
	if (dfa->accept) {
		freeaccept(dfa->accept);
		dfa->accept = NULL;
	}
}

static void free_dfas(void)
{
	int i;
	for (i = 0; i < ndfas; i++)
		free_dfa(&dfastates[i]);
	free(dfastates);
}

static void init_dfas(struct nfa *sstate, struct set *accept)
{
	struct accept *acp;
	struct set *first;
	int i;
	/* alloc dfa buffer */
	dfastates = xmalloc(MAX_DFAS * sizeof(struct dfa));
	/* init dfas */
	for (i = 0; i < MAX_DFAS; i++) {
		dfastates[i].group = -1;
		dfastates[i].states = NULL;
		dfastates[i].accept = NULL;
	}
	/* init first dfa state */
	first = newset();
	addset(first, nfastate(sstate));
	epsilon_closure(first, &acp, 0);
	/* NOTE: first dfa can be accepted, such as regexp: `.*` */
	if (acp) {
		dfastates[ndfas].accept = getaccept(acp);
		addset(accept, 0);
	}
	dfastates[0].states = first;
	/* some internal parmaters */
	ndfas = 1;
	currentdfa = 0;
}

static int add_dfa(struct set *nfastates, struct accept *accept)
{
	if (ndfas >= MAX_DFAS)
		errexit("dfas overflows");
	if (!dfastates)
		errexit("not init dfas");

	if (nfastates) {
		dfastates[ndfas].states = nfastates;
		dfastates[ndfas].accept = getaccept(accept);
	}
	return ndfas++;
}

static int in_dfa(struct set *states)
{
	int i;
	/* no safe check */
	for (i = 0; i < ndfas; i++) {
		if (equset(states, dfastates[i].states))
			return i;
	}
	return -1;
}

int state_dfa(struct dfa *dfa)
{
	return dfa - dfastates;
}

static struct dfa *next_dfa(void)
{
	if (currentdfa >= ndfas)
		return NULL;
	return &dfastates[currentdfa++];
}

void subsetconstruct(int (*dfatable)[128], struct set *acceptset)
{
	struct dfa *dfa;
	struct set *next;
	int nextstate = 0;
	int c, state;
	struct accept *accept;

	while (dfa = next_dfa()) {
		for (c = 0; c < MAX_CHARS; c++) {
			/* compute next dfa, to which dfa move on c */
			next = move(dfa->states, c);
			next = epsilon_closure(next, &accept, 0);
			/* no transition */
			if (!next)
				state = F;
			/* transition from current to next */
			else if ((state = in_dfa(next)) >= 0)
				freeset(next);	/* next is alloced by move()*/
			else
				state = add_dfa(next, accept);
			/* real assign the dfatable: [0->ndfas][0->MAX_CHARS] */
			dfatable[state_dfa(dfa)][c] = state;
			/* NOTE: using state, not ndfas - 1 */
			if (accept)
				addset(acceptset, state);
		}
	}
}

/*
 * subset construction:
 * convert NFA directed graph to DFA table
 *
 * RETURN:
 * 	@table        dfa state transtion table
 * 	@acceptset    dfa acceptable state set
 * 	return value  dfa state transtion table size
 */
int construct_dfa(struct nfa *sstate, int (**table)[], struct set **acceptset)
{
	/* dfatable[STATES][CHARS] */
	int (*dfatable)[MAX_CHARS];
	struct set *accept;
	int i;

	/* init dfa table */
	dfatable = xmalloc(MAX_TABLESIZE * sizeof(int));

	/* alloc accept set */
	accept = newset();

	/*
	 * init internal dfa auxiliary method,
	 *  which is used in subsetconstruct()
	 */
	init_dfas(sstate, accept);

	/* subset construction */
	subsetconstruct(dfatable, accept);

	/* adjust dfatable real size */
	dfatable = realloc(dfatable, ndfas * MAX_CHARS * sizeof(int));

	/* return value */
	if (table)
		*table = dfatable;
	else
		free(dfatable);
	if (acceptset)
		*acceptset = accept;
	else
		freeset(accept);

	return ndfas;
}

void traverse_dfatable(int (*dfatable)[128], int size, struct set *accept)
{
	int i, c;
	printf("\n");
	fprintf(stderr, "\n\n[==== DFA Transition Table ====]\n");
	for (i = 0; i < size; i++) {
		for (c = 0; c < MAX_CHARS; c++)
			if (dfatable[i][c] >= 0)
				fprintf(stderr, "  %d --> %d on %c\n",
					i, dfatable[i][c], c);
			if (dfatable[i][c] >= size)
				errexit("dfa table corrupt");
	}
	/* check and output accept information */
	for_each_member(i, accept) {
		if (!dfastates[i].accept) {
			fprintf(stderr, "dfastate[%d] has no accept", i);
			exit(EXIT_FAILURE);
		}

		fprintf(stderr, " accept state:%d ", i);
		if (dfastates[i].accept->anchor & AC_START)
			fprintf(stderr, "^ ");
		fprintf(stderr,"%s", dfastates[i].accept->action);
		if (dfastates[i].accept->anchor & AC_END)
			fprintf(stderr, " $");
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "[==== END DFA Transition Table ====]\n");
}

#ifdef DFA_TABLE_TEST

int main(int argc, char **argv)
{
	char line[256];
	struct nfa *nfa;
	struct set *accept;
	/*
	 * NOTE: int *table1[x] --> size = x*4
	 *       int (*table2)[x] --> size = 4
	 *       typeof(&table2) --> int (**table2p)[x]
	 */
	int (*table)[MAX_CHARS];	/* dfa table */
	int size;	/* dfa table size */

	if (argc != 2)
		errexit("ARGC != 2");

	/* init token stream: interpreting regular expression */
	text_open(argv[1]);
	/* construct NFA from regular expression */
	init_nfa_buffer();
	nfa = machine();
	traverse_nfa(nfa);
	/* construct dfa table */
	size = construct_dfa(nfa, &table, &accept);
	traverse_dfatable(table, size, accept);

	/* free dfa state sets */
	free_dfas();

	return 0;
}

#endif	/* NFA_TABLE_TEST */

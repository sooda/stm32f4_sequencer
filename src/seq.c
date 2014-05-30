#include <string.h>
#include "seq.h"

#define SEQ_BUFSIZE 512
#define SEQ_LENGTH 128
#define SIZEMASK(sz) (sz - 1)
#define ARRMOD(val, sz) ((val) & SIZEMASK(sz))

struct seqevent seqstore[SEQ_BUFSIZE];
struct seqevent* seqqueue[SEQ_LENGTH];
int next_free;

static struct seqevent* alloc_ev(void) {
	struct seqevent* ret;
	int next;

	if (next_free == -1)
		return NULL;

	ret = &seqstore[next_free];
	ret->flags = SEQ_FLAG_USED;

	next = ARRMOD(next_free + 1, SEQ_BUFSIZE);
	while ((seqstore[next].flags & SEQ_FLAG_USED) && (next != next_free)) {
		next = ARRMOD(next + 1, SEQ_BUFSIZE);
	}
	next_free = next != next_free ? next : -1;

	return ret;
}

int seq_add_event(int time, int instrument, int type, int param) {
	return seq_add_event2(time, instrument, type, param, 0);
}
int seq_add_event2(int time, int instrument, int type, int param1, int param2) {
	struct seqevent* next = alloc_ev();
	struct seqevent* queue;

	if (!next)
		return 0;

	next->instrument = instrument;
	next->type = type;
	next->param1 = param1;
	next->param2 = param2;

	queue = seqqueue[ARRMOD(time, SEQ_LENGTH)];
	if (queue) {
		while (queue->next) {
			queue = queue->next;
		}
		queue->next = next;
	} else {
		seqqueue[ARRMOD(time, SEQ_LENGTH)] = next;
	}
	return 1;
}

struct seqevent* seq_events_at(int time) {
	return seqqueue[ARRMOD(time, SEQ_LENGTH)];
}

void seq_init(void) {
	memset(seqstore, 0, sizeof(seqstore));
	memset(seqqueue, 0, sizeof(seqqueue));
	next_free = 0;
}

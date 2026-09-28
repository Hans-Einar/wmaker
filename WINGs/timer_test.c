/* Regression coverage for timer callbacks that enter a nested event loop. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <WINGs/WUtil.h>

extern void W_CheckTimerHandlers(void);

static WMHandlerID outer_timer;
static int outer_calls, inner_calls, leaf_calls;
static int cancel_mode;

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

static void dispatch(void)
{
	struct timespec delay = {0, 5000000};

	nanosleep(&delay, NULL);
	W_CheckTimerHandlers();
}

static void leaf(void *data)
{
	(void) data;
	leaf_calls++;
	/* Active callbacks must survive arbitrarily nested timer checks. */
	W_CheckTimerHandlers();
}

static void inner(void *data)
{
	(void) data;
	inner_calls++;
	WMAddTimerHandler(0, leaf, NULL);
	dispatch();
	if (cancel_mode == 1)
		WMDeleteTimerHandler(outer_timer);
	else if (cancel_mode == 2)
		WMDeleteTimerWithClientData(&outer_calls);
}

static void outer(void *data)
{
	(void) data;
	outer_calls++;
	WMAddTimerHandler(0, inner, NULL);
	dispatch();
	check(inner_calls == outer_calls && leaf_calls == outer_calls,
	      "nested timers run without re-entering an active callback");
}

static void reset(void)
{
	outer_calls = inner_calls = leaf_calls = 0;
}

static void nested_persistent(void *data)
{
	WMHandlerID timer;

	(void) data;
	timer = WMAddPersistentTimerHandler(1, leaf, NULL);
	dispatch();
	dispatch();
	check(leaf_calls == 2, "other persistent timers keep running in a nested event loop");
	WMDeleteTimerHandler(timer);
}

int main(void)
{
	outer_timer = WMAddTimerHandler(0, outer, &outer_calls);
	dispatch();
	dispatch();
	check(outer_calls == 1, "one-shot callback runs exactly once");

	reset();
	outer_timer = WMAddPersistentTimerHandler(1, outer, &outer_calls);
	dispatch();
	check(outer_calls == 1, "persistent callback is not repeated while active");
	dispatch();
	check(outer_calls == 2, "persistent callback is rescheduled after returning");
	WMDeleteTimerHandler(outer_timer);
	dispatch();
	check(outer_calls == 2, "pending persistent timer can be cancelled");

	for (cancel_mode = 1; cancel_mode <= 2; cancel_mode++) {
		reset();
		outer_timer = WMAddPersistentTimerHandler(1, outer, &outer_calls);
		dispatch();
		dispatch();
		check(outer_calls == 1, "nested callback can cancel an active persistent timer");
	}
	reset();
	WMAddTimerHandler(0, nested_persistent, NULL);
	dispatch();
	dispatch();
	check(leaf_calls == 2, "cancelled nested timer is not rescheduled");
	puts("PASS: nested timer dispatch and cancellation");
	return 0;
}
